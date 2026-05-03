#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <linux/limits.h>
#include <unistd.h>

#include "constants.h"
#include "root_impl/common.h"
#include "utils.h"

struct Module {
  char *name;
  int lib_fd;
  int companion;
};

struct Context {
  struct Module *modules;
  size_t len;
};

#define PATH_MODULES_DIR "/data/adb/modules"
#define TMP_PATH "/data/adb/rezygisk"
#define CONTROLLER_SOCKET TMP_PATH "/init_monitor"
#define PATH_CP_NAME TMP_PATH "/" LP_SELECT("cp32.sock", "cp64.sock")
#define ZYGISKD_FILE PATH_MODULES_DIR "/rezygisk/bin/zygiskd" LP_SELECT("32", "64")
#define ZYGISKD_PATH "/data/adb/modules/rezygisk/bin/zygiskd" LP_SELECT("32", "64")

#ifdef __aarch64__
  #define ARCH_STR "arm64-v8a"
#elif __arm__
  #define ARCH_STR "armeabi-v7a"
#elif __x86_64__
  #define ARCH_STR "x86_64"
#elif __i386__
  #define ARCH_STR "x86"
#else
  #error "Unsupported architecture"
  #define ARCH_STR "unknown"
#endif

/* WARNING: Dynamic memory based */
static void load_modules(struct Context *restrict context) {
  context->len = 0;
  context->modules = NULL;

  DIR *dir = opendir(PATH_MODULES_DIR);
  if (dir == NULL) {
    LOGE("Failed opening modules directory: %s.", PATH_MODULES_DIR);

    return;
  }

  LOGI("Loading modules for architecture: " ARCH_STR);

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type != DT_DIR) continue; /* INFO: Only directories */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "rezygisk") == 0) continue;

    char *name = entry->d_name;
    char so_path[PATH_MAX];
    snprintf(so_path, PATH_MAX, "/data/adb/modules/%s/zygisk/" ARCH_STR ".so", name);

    if (access(so_path, R_OK) == -1) continue;

    char disabled[PATH_MAX];
    snprintf(disabled, PATH_MAX, "/data/adb/modules/%s/disable", name);

    if (access(disabled, F_OK) == 0) continue;

    int lib_fd = open(so_path, O_RDONLY | O_CLOEXEC);
    if (lib_fd == -1) {
      LOGE("Failed loading module \"%s\"", name);

      continue;
    }

    struct Module *tmp_modules = realloc(context->modules, (context->len + 1) * sizeof(struct Module));
    if (tmp_modules == NULL) {
      LOGE("Failed reallocating memory for modules.");

      close(lib_fd);

      for (size_t i = 0; i < context->len; i++) {
        free(context->modules[i].name);
        if (context->modules[i].companion >= 0) close(context->modules[i].companion);
        if (context->modules[i].lib_fd >= 0) close(context->modules[i].lib_fd);
      }

      free(context->modules);
      context->modules = NULL;
      context->len = 0;

      closedir(dir);

      return;
    }
    context->modules = tmp_modules;

    context->modules[context->len].name = strdup(name);
    if (context->modules[context->len].name == NULL) {
      LOGE("Failed to strdup for the module \"%s\": %s", name, strerror(errno));

      close(lib_fd);

      return;
    }

    context->modules[context->len].lib_fd = lib_fd;
    context->modules[context->len].companion = -1;
    context->len++;
  }

  closedir(dir);
}

static void free_modules(struct Context *restrict context) {
  for (size_t i = 0; i < context->len; i++) {
    free(context->modules[i].name);
    if (context->modules[i].companion >= 0) close(context->modules[i].companion);
    if (context->modules[i].lib_fd >= 0) close(context->modules[i].lib_fd);
  }

  free(context->modules);
}

static int create_daemon_socket(void) {
  set_socket_create_context("u:r:zygote:s0");

  return unix_listener_from_path(PATH_CP_NAME);
}

static int spawn_companion(char *restrict argv[], char *restrict name, int lib_fd) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
    LOGE("Failed creating socket pair.");

    return -1;
  }

  int daemon_fd = sockets[0];
  int companion_fd = sockets[1];

  pid_t pid = fork();
  if (pid < 0) {
    LOGE("Failed forking companion: %s", strerror(errno));

    close(companion_fd);
    close(daemon_fd);

    return -1;
  }

  if (pid > 0) {
    close(companion_fd);

    int status = 0;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      LOGE("Exited with status %d", status);

      close(daemon_fd);

      return -1;
    }

    if (write_string(daemon_fd, name) == -1) {
      LOGE("Failed writing module name.");

      close(daemon_fd);

      return -1;
    }

    if (write_fd(daemon_fd, lib_fd) == -1) {
      LOGE("Failed sending library fd.");

      close(daemon_fd);

      return -1;
    }

    uint8_t response = 0;
    ssize_t ret = read_uint8_t(daemon_fd, &response);
    if (ret <= 0) {
      LOGE("Failed reading companion response.");

      close(daemon_fd);

      return -1;
    }

    switch (response) {
      /* INFO: Even without any entry, we should still just deal with it */
      case 0: {
        close(daemon_fd);

        return -2;
      }
      case 1: { return daemon_fd; }
      /* TODO: Should we be closing daemon socket here? (in non-0-and-1 case) */
      default: {
        close(daemon_fd);

        return -1;
      }
    }
  /* INFO: if pid == 0: */
  }

  close(daemon_fd);

  /* INFO: There is no case where this will fail with a valid fd. */
  /* INFO: Remove FD_CLOEXEC flag to avoid closing upon exec */
  if (fcntl(companion_fd, F_SETFD, 0) == -1) {
    LOGE("Failed removing FD_CLOEXEC flag: %s", strerror(errno));

    close(companion_fd);

    exit(1);
  }

  char *process = argv[0];
  char nice_name[256];
  char *last = strrchr(process, '/');
  if (last == NULL) {
    snprintf(nice_name, sizeof(nice_name), "%s", process);
  } else {
    snprintf(nice_name, sizeof(nice_name), "%s", last + 1);
  }

  char process_name[256];
  snprintf(process_name, sizeof(process_name), "%s-%s", nice_name, name);

  char companion_fd_str[32];
  snprintf(companion_fd_str, sizeof(companion_fd_str), "%d", companion_fd);

  char companion[] = "companion";
  char *eargv[] = { process_name, companion, companion_fd_str, NULL };
  if (non_blocking_execv(ZYGISKD_PATH, eargv) == -1) {
    LOGE("Failed executing companion: %s", strerror(errno));

    close(companion_fd);

    exit(1);
  }

  exit(0);
}

/* WARNING: Dynamic memory based */
void zygiskd_start(char *restrict argv[]) {
  /* INFO: When implementation is None or Multiple, it won't set the values
            for the context, causing it to have garbage values. In response
            to that, "= { 0 }" is used to ensure that the values are clean. */
  struct Context context = { 0 };

  struct root_impl impl;
  get_impl(&impl);
  if (impl.impl == None || impl.impl == Multiple) {
    unix_datagram_sendto(CONTROLLER_SOCKET, &(uint8_t){ DAEMON_SET_ERROR_INFO }, sizeof(uint8_t));

    const char *msg = NULL;
    if (impl.impl == None) msg = "Unsupported environment: Unknown root implementation";
    else msg = "Unsupported environment: Multiple root implementations found";

    LOGE("%s", msg);

    uint32_t msg_len = (uint32_t)strlen(msg);
    unix_datagram_sendto(CONTROLLER_SOCKET, &msg_len, sizeof(msg_len));
    unix_datagram_sendto(CONTROLLER_SOCKET, msg, msg_len);

    exit(EXIT_FAILURE);
  } else {
    load_modules(&context);

    unix_datagram_sendto(CONTROLLER_SOCKET, &(uint8_t){ DAEMON_SET_INFO }, sizeof(uint8_t));

    char impl_name[LONGEST_ROOT_IMPL_NAME];
    stringify_root_impl_name(impl, impl_name);

    uint32_t root_impl_len = (uint32_t)strlen(impl_name);
    unix_datagram_sendto(CONTROLLER_SOCKET, &root_impl_len, sizeof(root_impl_len));
    unix_datagram_sendto(CONTROLLER_SOCKET, impl_name, root_impl_len);

    uint32_t modules_len = (uint32_t)context.len;
    unix_datagram_sendto(CONTROLLER_SOCKET, &modules_len, sizeof(modules_len));

    for (size_t i = 0; i < context.len; i++) {
      uint32_t module_name_len = (uint32_t)strlen(context.modules[i].name);
      unix_datagram_sendto(CONTROLLER_SOCKET, &module_name_len, sizeof(module_name_len));
      unix_datagram_sendto(CONTROLLER_SOCKET, context.modules[i].name, module_name_len);
    }

    LOGI("Sent root implementation and modules information to controller socket");
  }

  int socket_fd = create_daemon_socket();
  if (socket_fd == -1) {
    LOGE("Failed creating daemon socket");

    free_modules(&context);

    root_impl_cleanup();

    return;
  }

  struct sigaction sa = { .sa_handler = SIG_IGN };
  sigaction(SIGPIPE, &sa, NULL);

  bool first_process = true;
  while (1) {
    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
      LOGE("accept: %s", strerror(errno));

      break;
    }

    uint8_t action8 = 0;
    ssize_t len = read_uint8_t(client_fd, &action8);
    if (len == -1) {
      LOGE("read: %s", strerror(errno));

      close(client_fd);

      break;
    } else if (len == 0) {
      LOGI("Client disconnected");

      close(client_fd);

      break;
    }

    enum DaemonSocketAction action = (enum DaemonSocketAction)action8;

    switch (action) {
      case ZygoteInjected: {
        unix_datagram_sendto(CONTROLLER_SOCKET, &(uint8_t){ ZYGOTE_INJECTED }, sizeof(uint8_t));

        break;
      }
      case ZygoteRestart: {
        for (size_t i = 0; i < context.len; i++) {
          if (context.modules[i].companion <= -1) continue;

          close(context.modules[i].companion);
          context.modules[i].companion = -1;
        }

        break;
      }
      case GetProcessFlags: {
        uint32_t uid = 0;
        ssize_t ret = read_uint32_t(client_fd, &uid);
        ASSURE_SIZE_READ("GetProcessFlags", "uid", ret, sizeof(uid), break);

        /* INFO: Only used for Magisk, as it saves process names and not UIDs. */
        char process[PROCESS_NAME_MAX_LEN];
        ret = read_string(client_fd, process, sizeof(process));
        if (ret == -1) {
          LOGE("Failed reading process name.");

          break;
        }

        uint32_t flags = 0;
        if (first_process) {
          flags |= PROCESS_IS_FIRST_STARTED;

          first_process = false;
        }

        if (uid_is_manager(uid)) {
          flags |= PROCESS_IS_MANAGER;
        } else {
          if (uid_granted_root(uid)) {
            flags |= PROCESS_GRANTED_ROOT;
          }
          if (uid_should_umount(uid, (const char *const)process)) {
            flags |= PROCESS_ON_DENYLIST;
          }
        }

        switch (impl.impl) {
          case None: { break; }
          case Multiple: { break; }
          case KernelSU: {
            flags |= PROCESS_ROOT_IS_KSU;

            break;
          }
          case APatch: {
            flags |= PROCESS_ROOT_IS_APATCH;

            break;
          }
          case Magisk: {
            flags |= PROCESS_ROOT_IS_MAGISK;

            break;
          }
        }

        ret = write_uint32_t(client_fd, flags);
        ASSURE_SIZE_WRITE("GetProcessFlags", "flags", ret, sizeof(flags), break);

        break;
      }
      case GetInfo: {
        uint32_t flags = 0;

        switch (impl.impl) {
          case None: { break; }
          case Multiple: { break; }
          case KernelSU: {
            flags |= PROCESS_ROOT_IS_KSU;

            break;
          }
          case APatch: {
            flags |= PROCESS_ROOT_IS_APATCH;

            break;
          }
          case Magisk: {
            flags |= PROCESS_ROOT_IS_MAGISK;

            break;
          }
        }

        ssize_t ret = write_uint32_t(client_fd, flags);
        ASSURE_SIZE_WRITE("GetInfo", "flags", ret, sizeof(flags), break);

        /* TODO: Use pid_t */
        uint32_t pid = (uint32_t)getpid();
        ret = write_uint32_t(client_fd, pid);
        ASSURE_SIZE_WRITE("GetInfo", "pid", ret, sizeof(pid), break);

        size_t modules_len = context.len;
        ret = write_size_t(client_fd, modules_len);
        ASSURE_SIZE_WRITE("GetInfo", "modules_len", ret, sizeof(modules_len), break);

        for (size_t i = 0; i < modules_len; i++) {
          ret = write_string(client_fd, context.modules[i].name);
          if (ret == -1) {
            LOGE("Failed writing module name.");

            break;
          }
        }

        break;
      }
      case ReadModules: {
        size_t clen = context.len;
        ssize_t ret = write_size_t(client_fd, clen);
        ASSURE_SIZE_WRITE("ReadModules", "len", ret, sizeof(clen), break);

        for (size_t i = 0; i < clen; i++) {
          char lib_path[PATH_MAX];
          snprintf(lib_path, PATH_MAX, "/data/adb/modules/%s/zygisk/" ARCH_STR ".so", context.modules[i].name);

          if (write_string(client_fd, lib_path) == -1) {
            LOGE("Failed writing module path.");

            break;
          }
        }

        break;
      }
      case RequestCompanionSocket: {
        size_t index = 0;
        ssize_t ret = read_size_t(client_fd, &index);
        ASSURE_SIZE_READ("RequestCompanionSocket", "index", ret, sizeof(index), break);

        if (index >= context.len) {
          LOGE("Invalid module index: %zu", index);

          ret = write_uint8_t(client_fd, 0);
          ASSURE_SIZE_WRITE("RequestCompanionSocket", "response", ret, sizeof(uint8_t), break);

          break;
        }

        struct Module *module = &context.modules[index];
        if (module->companion >= 0) {
          if (!check_unix_socket(module->companion, false)) {
            LOGE(" - Companion for module \"%s\" crashed", module->name);

            close(module->companion);
            module->companion = -1;
          }
        }

        if (module->companion <= -1) {
          module->companion = spawn_companion(argv, module->name, module->lib_fd);

          if (module->companion >= 0) {
            LOGI(" - Spawned companion for \"%s\": %d", module->name, module->companion);
          } else if (module->companion == -2) {
            LOGE(" - No companion spawned for \"%s\" because it has no entry.", module->name);
          } else {
            LOGE(" - Failed to spawn companion for \"%s\": %s", module->name, strerror(errno));
          }
        }

        /*
          INFO: Companion already exists or was created. In any way,
                 it should be in the while loop to receive fds now,
                 so just sending the file descriptor of the client is
                 safe.
        */
        if (module->companion >= 0) {
          LOGI(" - Sending companion fd socket of module \"%s\"", module->name);

          if (write_fd(module->companion, client_fd) == -1) {
            LOGE(" - Failed to send companion fd socket of module \"%s\"", module->name);

            ret = write_uint8_t(client_fd, 0);
            ASSURE_SIZE_WRITE("RequestCompanionSocket", "response", ret, sizeof(uint8_t), break);

            close(module->companion);
            module->companion = -1;
          }
        } else {
          LOGE(" - Failed to spawn companion for module \"%s\"", module->name);

          ret = write_uint8_t(client_fd, 0);
          ASSURE_SIZE_WRITE("RequestCompanionSocket", "response", ret, sizeof(uint8_t), break);
        }

        break;
      }
      case GetModuleDir: {
        size_t index = 0;
        ssize_t ret = read_size_t(client_fd, &index);
        ASSURE_SIZE_READ("GetModuleDir", "index", ret, sizeof(index), break);

        if (index >= context.len) {
          LOGE("Invalid module index: %zu", index);

          ret = write_uint8_t(client_fd, 0);
          ASSURE_SIZE_WRITE("GetModuleDir", "response", ret, sizeof(uint8_t), break);

          break;
        }

        char module_dir[PATH_MAX];
        snprintf(module_dir, PATH_MAX, "%s/%s", PATH_MODULES_DIR, context.modules[index].name);

        int fd = open(module_dir, O_RDONLY);
        if (fd == -1) {
          LOGE("Failed opening module directory \"%s\": %s", module_dir, strerror(errno));

          break;
        }

        if (write_fd(client_fd, fd) == -1) {
          LOGE("Failed sending module directory \"%s\" fd: %s", module_dir, strerror(errno));

          close(fd);

          break;
        }

        break;
      }
      case UpdateMountNamespace: {
        pid_t pid = 0;
        ssize_t ret = read_uint32_t(client_fd, (uint32_t *)&pid);
        ASSURE_SIZE_READ("UpdateMountNamespace", "pid", ret, sizeof(pid), break);

        uint8_t mns_state = 0;
        ret = read_uint8_t(client_fd, &mns_state);
        ASSURE_SIZE_READ("UpdateMountNamespace", "mns_state", ret, sizeof(mns_state), break);

        uint32_t our_pid = (uint32_t)getpid();
        ret = write_uint32_t(client_fd, our_pid);
        ASSURE_SIZE_WRITE("UpdateMountNamespace", "our_pid", ret, sizeof(our_pid), break);

        if ((enum MountNamespaceState)mns_state == Clean)
          save_mns_fd(pid, Mounted, impl);

        int ns_fd = save_mns_fd(pid, (enum MountNamespaceState)mns_state, impl);
        if (ns_fd == -1) {
          LOGE("Failed to save mount namespace fd for pid %d: %s", pid, strerror(errno));

          ret = write_uint32_t(client_fd, (uint32_t)0);
          ASSURE_SIZE_WRITE("UpdateMountNamespace", "ns_fd", ret, sizeof(ns_fd), break);

          break;
        }

        ret = write_uint32_t(client_fd, (uint32_t)ns_fd);
        ASSURE_SIZE_WRITE("UpdateMountNamespace", "ns_fd", ret, sizeof(ns_fd), break);

        break;
      }
      case RemoveModule: {
        size_t index = 0;
        ssize_t ret = read_size_t(client_fd, &index);
        ASSURE_SIZE_READ("RemoveModule", "index", ret, sizeof(index), break);

        if (index >= context.len) {
          LOGE("Invalid module index: %zu", index);

          ret = write_uint8_t(client_fd, 0);
          ASSURE_SIZE_WRITE("RemoveModule", "response", ret, sizeof(uint8_t), break);

          break;
        }

        struct Module *module = &context.modules[index];
        if (module->companion >= 0) {
          close(module->companion);
          module->companion = -1;
        }

        free(module->name);
        module->name = NULL;

        if (module->lib_fd >= 0) {
          close(module->lib_fd);
          module->lib_fd = -1;
        }

        memmove(&context.modules[index], &context.modules[index + 1], (context.len - index - 1) * sizeof(struct Module));
        context.len--;

        ret = write_uint8_t(client_fd, 1);
        ASSURE_SIZE_WRITE("RemoveModule", "response", ret, sizeof(uint8_t), break);

        break;
      }
    }

    close(client_fd);
  }

  close(socket_fd);
  free_modules(&context);
  root_impl_cleanup();
}
