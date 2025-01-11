#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#include <unistd.h>
#include <linux/limits.h>
#include <sys/syscall.h>
#include <linux/memfd.h>

#include <pthread.h>

#include "root_impl/common.h"
#include "constants.h"
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

enum Architecture {
  ARM32,
  ARM64,
  X86,
  X86_64,
};

#define PATH_MODULES_DIR "/data/adb/modules"
#define TMP_PATH "/data/adb/rezygisk"
#define CONTROLLER_SOCKET TMP_PATH "/init_monitor"
#define PATH_CP_NAME TMP_PATH "/" lp_select("cp32.sock", "cp64.sock")
#define ZYGISKD_FILE PATH_MODULES_DIR "/zygisksu/bin/zygiskd" lp_select("32", "64")
#define ZYGISKD_PATH "/data/adb/modules/zygisksu/bin/zygiskd" lp_select("32", "64")

static enum Architecture get_arch(void) {
  char system_arch[32];
  get_property("ro.product.cpu.abi", system_arch);

  if (strstr(system_arch, "arm") != NULL) return lp_select(ARM32, ARM64);
  if (strstr(system_arch, "x86") != NULL) return lp_select(X86, X86_64);

  LOGE("Unsupported system architecture: %s\n", system_arch);
  exit(1);
}

int create_library_fd(const char *restrict so_path) {
  int so_fd = open(so_path, O_RDONLY);
  if (so_fd == -1) {
    LOGE("Failed opening so file: %s\n", strerror(errno));

    return -1;
  }

  off_t so_size = lseek(so_fd, 0, SEEK_END);
  if (so_size == -1) {
    LOGE("Failed getting so file size: %s\n", strerror(errno));

    close(so_fd);

    return -1;
  }

  if (lseek(so_fd, 0, SEEK_SET) == -1) {
    LOGE("Failed seeking so file: %s\n", strerror(errno));

    close(so_fd);

    return -1;
  }

  /* INFO: This is required as older implementations of glibc may not
             have the memfd_create function call, causing a crash. */
  int memfd = (int)syscall(SYS_memfd_create, "jit-cache-zygisk", MFD_ALLOW_SEALING);
  if (memfd == -1) {
    LOGE("Failed creating memfd: %s\n", strerror(errno));

    return -1;
  }

  if (sendfile(memfd, so_fd, NULL, (size_t)so_size) == -1) {
    LOGE("Failed copying so file to memfd: %s\n", strerror(errno));

    close(so_fd);
    close(memfd);

    return -1;
  }

  close(so_fd);

  if (fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) == -1) {
    LOGE("Failed sealing memfd: %s\n", strerror(errno));

    close(memfd);

    return -1;
  }

  return memfd;
}

/* WARNING: Dynamic memory based */
static void load_modules(enum Architecture arch, struct Context *restrict context) {
  context->len = 0;
  context->modules = NULL;

  DIR *dir = opendir(PATH_MODULES_DIR);
  if (dir == NULL) {
    LOGE("Failed opening modules directory: %s.", PATH_MODULES_DIR);

    return;
  }

  char arch_str[32];
  switch (arch) {
    case ARM64: { strcpy(arch_str, "arm64-v8a"); break; }
    case X86_64: { strcpy(arch_str, "x86_64"); break; }
    case ARM32: { strcpy(arch_str, "armeabi-v7a"); break; }
    case X86: { strcpy(arch_str, "x86"); break; }
  }

  LOGI("Loading modules for architecture: %s\n", arch_str);

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type != DT_DIR) continue; /* INFO: Only directories */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "zygisksu") == 0) continue;

    char *name = entry->d_name;
    char so_path[PATH_MAX];
    snprintf(so_path, PATH_MAX, "/data/adb/modules/%s/zygisk/%s.so", name, arch_str);

    struct stat st;
    if (stat(so_path, &st) == -1) {
      errno = 0;

      continue;
    }

    char disabled[PATH_MAX];
    snprintf(disabled, PATH_MAX, "/data/adb/modules/%s/disable", name);

    if (stat(disabled, &st) == -1) {
      if (errno != ENOENT) {
        LOGE("Failed checking if module `%s` is disabled: %s\n", name, strerror(errno));
        errno = 0;

        continue;
      }

      errno = 0;
    } else continue;

    int lib_fd = create_library_fd(so_path);
    if (lib_fd == -1) {
      LOGE("Failed loading module `%s`\n", name);

      continue;
    }


    context->modules = realloc(context->modules, (size_t)((context->len + 1) * sizeof(struct Module)));
    if (context->modules == NULL) {
      LOGE("Failed reallocating memory for modules.\n");

      return;
    }

    context->modules[context->len].name = strdup(name);
    context->modules[context->len].lib_fd = lib_fd;
    context->modules[context->len].companion = -1;
    context->len++;
  }
}

static void free_modules(struct Context *restrict context) {
  for (size_t i = 0; i < context->len; i++) {
    free(context->modules[i].name);
    if (context->modules[i].companion != -1) close(context->modules[i].companion);
  }
}

static int create_daemon_socket(void) {
  set_socket_create_context("u:r:zygote:s0");

  return unix_listener_from_path(PATH_CP_NAME);
}

static int spawn_companion(char *restrict argv[], char *restrict name, int lib_fd) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
    LOGE("Failed creating socket pair.\n");

    return -1;
  }

  int daemon_fd = sockets[0];
  int companion_fd = sockets[1];

  pid_t pid = fork();
  if (pid < 0) {
    LOGE("Failed forking companion: %s\n", strerror(errno));

    close(companion_fd);
    close(daemon_fd);

    exit(1);
  } else if (pid > 0) {
    close(companion_fd);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      if (write_string(daemon_fd, name) == -1) {
        LOGE("Failed writing module name.\n");

        close(daemon_fd);

        return -1;
      }
      if (write_fd(daemon_fd, lib_fd) == -1) {
        LOGE("Failed sending library fd.\n");

        close(daemon_fd);

        return -1;
      }
      
      uint8_t response = 0;
      ssize_t ret = read_uint8_t(daemon_fd, &response);
      if (ret <= 0) {
        LOGE("Failed reading companion response.\n");

        close(daemon_fd);

        return -1;
      }

      switch (response) {
        /* INFO: Even without any entry, we should still just deal with it */
        case 0: { return -2; }
        case 1: { return daemon_fd; }
        /* TODO: Should we be closing daemon socket here? (in non-0-and-1 case) */
        default: {
          return -1;
        }
      }
      /* TODO: Should we be closing daemon socket here? */
    } else {
      LOGE("Exited with status %d\n", status);

      return -1;
    }
  /* INFO: if pid == 0: */
  } else {
    /* INFO: There is no case where this will fail with a valid fd. */
    /* INFO: Remove FD_CLOEXEC flag to avoid closing upon exec */
    if (fcntl(companion_fd, F_SETFD, 0) == -1) {
      LOGE("Failed removing FD_CLOEXEC flag: %s\n", strerror(errno));

      close(companion_fd);
      close(daemon_fd);

      exit(1);
    }
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

  char *eargv[] = { process_name, "companion", companion_fd_str, NULL };
  if (non_blocking_execv(ZYGISKD_PATH, eargv) == -1) {
    LOGE("Failed executing companion: %s\n", strerror(errno));

    close(companion_fd);

    exit(1);
  }

  exit(0);
}

struct __attribute__((__packed__)) MsgHead {
  unsigned int cmd;
  int length;
  char data[0];
};

/* WARNING: Dynamic memory based */
void zygiskd_start(char *restrict argv[]) {
  /* INFO: When implementation is None or Multiple, it won't set the values 
            for the context, causing it to have garbage values. In response
            to that, "= { 0 }" is used to ensure that the values are clean. */
  struct Context context = { 0 };

  struct root_impl impl;
  get_impl(&impl);
  if (impl.impl == None || impl.impl == Multiple) {
    struct MsgHead *msg = NULL;
  
    if (impl.impl == None) {
      msg = malloc(sizeof(struct MsgHead) + strlen("Unsupported environment: Unknown root implementation") + 1);
    } else {
      msg = malloc(sizeof(struct MsgHead) + strlen("Unsupported environment: Multiple root implementations found") + 1);
    }
    if (msg == NULL) {
      LOGE("Failed allocating memory for message.\n");

      return;
    }

    msg->cmd = DAEMON_SET_ERROR_INFO;
    if (impl.impl == None) {
      msg->length = sprintf(msg->data, "Unsupported environment: Unknown root implementation");
    } else {
      msg->length = sprintf(msg->data, "Unsupported environment: Multiple root implementations found");
    }

    unix_datagram_sendto(CONTROLLER_SOCKET, (void *)msg, (size_t)((int)sizeof(struct MsgHead) + msg->length));

    free(msg);
  } else {
    enum Architecture arch = get_arch();
    load_modules(arch, &context);

    char *module_list = NULL;
    size_t module_list_len = 0;
    if (context.len == 0) {
      module_list = strdup("None");
      module_list_len = strlen("None");
    } else {
      for (size_t i = 0; i < context.len; i++) {
        if (i != context.len - 1) {
          module_list = realloc(module_list, module_list_len + strlen(context.modules[i].name) + strlen(", ") + 1);
          if (module_list == NULL) {
            LOGE("Failed reallocating memory for module list.\n");

            return;
          }

          strcpy(module_list + module_list_len, context.modules[i].name);

          module_list_len += strlen(context.modules[i].name);

          strcpy(module_list + module_list_len, ", ");

          module_list_len += strlen(", ");
        } else {
          module_list = realloc(module_list, module_list_len + strlen(context.modules[i].name) + 1);
          if (module_list == NULL) {
            LOGE("Failed reallocating memory for module list.\n");

            return;
          }

          strcpy(module_list + module_list_len, context.modules[i].name);

          module_list_len += strlen(context.modules[i].name);
        }
      }
    }

    char impl_name[LONGEST_ROOT_IMPL_NAME];
    stringify_root_impl_name(impl, impl_name);

    size_t msg_length = strlen("Root: , Modules: ") + strlen(impl_name) + module_list_len + 1;

    struct MsgHead *msg = malloc(sizeof(struct MsgHead) + msg_length);
    msg->length = snprintf(msg->data, msg_length, "Root: %s, Modules: %s", impl_name, module_list) + 1;
    msg->cmd = DAEMON_SET_INFO;

    unix_datagram_sendto(CONTROLLER_SOCKET, (void *)msg, (size_t)((int)sizeof(struct MsgHead) + msg->length));

    free(msg);
    free(module_list);
  }

  int socket_fd = create_daemon_socket();
  if (socket_fd == -1) {
    LOGE("Failed creating daemon socket\n");

    return;
  }

  bool first_process = true;
  while (1) {
    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
      LOGE("accept: %s\n", strerror(errno));

      return;
    }

    uint8_t action8 = 0;
    ssize_t len = read_uint8_t(client_fd, &action8);
    if (len == -1) {
      LOGE("read: %s\n", strerror(errno));

      return;
    } else if (len == 0) {
      LOGI("Client disconnected\n");

      return;
    }

    enum DaemonSocketAction action = (enum DaemonSocketAction)action8;

    switch (action) {
      case PingHeartbeat: {
        enum DaemonSocketAction msgr = ZYGOTE_INJECTED;
        unix_datagram_sendto(CONTROLLER_SOCKET, &msgr, sizeof(enum DaemonSocketAction));

        break;
      }
      case ZygoteRestart: {
        for (size_t i = 0; i < context.len; i++) {
          if (context.modules[i].companion != -1) {
            close(context.modules[i].companion);
            context.modules[i].companion = -1;
          }
        }

        break;
      }
      case SystemServerStarted: {
        enum DaemonSocketAction msgr = SYSTEM_SERVER_STARTED;
        unix_datagram_sendto(CONTROLLER_SOCKET, &msgr, sizeof(enum DaemonSocketAction));

        if (impl.impl == None || impl.impl == Multiple) {
          LOGI("Unsupported environment detected. Exiting.\n");

          close(client_fd);
          close(socket_fd);
          free_modules(&context);

          exit(1);
        }

        break;
      }
      case GetProcessFlags: {
        uint32_t uid = 0;
        ssize_t ret = read_uint32_t(client_fd, &uid);
        ASSURE_SIZE_READ_BREAK("GetProcessFlags", "uid", ret, sizeof(uid));

        uint32_t flags = 0;
        if (first_process) {
          flags |= PROCESS_IS_FIRST_STARTED;

          first_process = false;
        } else {
          if (uid_is_manager(uid)) {
            flags |= PROCESS_IS_MANAGER;
          } else {
            if (uid_granted_root(uid)) {
              flags |= PROCESS_GRANTED_ROOT;
            }
            if (uid_should_umount(uid)) {
              flags |= PROCESS_ON_DENYLIST;
            }
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
        ASSURE_SIZE_WRITE_BREAK("GetProcessFlags", "flags", ret, sizeof(flags));

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
        ASSURE_SIZE_WRITE_BREAK("GetInfo", "flags", ret, sizeof(flags));

        /* TODO: Use pid_t */
        uint32_t pid = (uint32_t)getpid();    
        ret = write_uint32_t(client_fd, pid);
        ASSURE_SIZE_WRITE_BREAK("GetInfo", "pid", ret, sizeof(pid));

        size_t modules_len = context.len;
        ret = write_size_t(client_fd, modules_len);
        ASSURE_SIZE_WRITE_BREAK("GetInfo", "modules_len", ret, sizeof(modules_len));
        
        for (size_t i = 0; i < modules_len; i++) {
          ret = write_string(client_fd, context.modules[i].name);
          if (ret == -1) {
            LOGE("Failed writing module name.\n");

            break;
          }
        }

        break;
      }
      case ReadModules: {
        size_t clen = context.len;
        ssize_t ret = write_size_t(client_fd, clen);
        ASSURE_SIZE_WRITE_BREAK("ReadModules", "len", ret, sizeof(clen));

        for (size_t i = 0; i < clen; i++) {
          if (write_string(client_fd, context.modules[i].name) == -1) {
            LOGE("Failed writing module name.\n");

            break;
          }
          if (write_fd(client_fd, context.modules[i].lib_fd) == -1) {
            LOGE("Failed writing module fd.\n");

            break;
          }
        }

        break;
      }
      case RequestCompanionSocket: {
        size_t index = 0;
        ssize_t ret = read_size_t(client_fd, &index);
        ASSURE_SIZE_READ_BREAK("RequestCompanionSocket", "index", ret, sizeof(index));

        struct Module *module = &context.modules[index];

        if (module->companion != -1) {
          if (!check_unix_socket(module->companion, false)) {
            LOGE(" - Companion for module \"%s\" crashed\n", module->name);

            close(module->companion);
            module->companion = -1;
          }
        }

        if (module->companion == -1) {
          module->companion = spawn_companion(argv, module->name, module->lib_fd);

          if (module->companion > 0) {
            LOGI(" - Spawned companion for \"%s\": %d\n", module->name, module->companion);
          } else {
            if (module->companion == -2) {
              LOGE(" - No companion spawned for \"%s\" because it has no entry.\n", module->name);
            } else {
              LOGE(" - Failed to spawn companion for \"%s\": %s\n", module->name, strerror(errno));
            }
          }
        }

        /* 
          INFO: Companion already exists or was created. In any way,
                 it should be in the while loop to receive fds now,
                 so just sending the file descriptor of the client is
                 safe.
        */
        if (module->companion != -1) {
          LOGI(" - Sending companion fd socket of module \"%s\"\n", module->name);

          if (write_fd(module->companion, client_fd) == -1) {
            LOGE(" - Failed to send companion fd socket of module \"%s\"\n", module->name);

            ret = write_uint8_t(client_fd, 0);
            ASSURE_SIZE_WRITE_BREAK("RequestCompanionSocket", "response", ret, sizeof(int));

            close(module->companion);
            module->companion = -1;

            /* INFO: RequestCompanionSocket by default doesn't close the client_fd */
            close(client_fd);
          }
        } else {
          LOGE(" - Failed to spawn companion for module \"%s\"\n", module->name);

          ret = write_uint8_t(client_fd, 0);
          ASSURE_SIZE_WRITE_BREAK("RequestCompanionSocket", "response", ret, sizeof(int));

          /* INFO: RequestCompanionSocket by default doesn't close the client_fd */
          close(client_fd);
        }

        break;
      }
      case GetModuleDir: {
        size_t index = 0;
        ssize_t ret = read_size_t(client_fd, &index);
        ASSURE_SIZE_READ_BREAK("GetModuleDir", "index", ret, sizeof(index));

        char module_dir[PATH_MAX];
        snprintf(module_dir, PATH_MAX, "%s/%s", PATH_MODULES_DIR, context.modules[index].name);

        int fd = open(module_dir, O_RDONLY);
        if (fd == -1) {
          LOGE("Failed opening module directory \"%s\": %s\n", module_dir, strerror(errno));

          break;
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
          LOGE("Failed getting module directory \"%s\" stats: %s\n", module_dir, strerror(errno));

          close(fd);

          break;
        }

        if (write_fd(client_fd, fd) == -1) {
          LOGE("Failed sending module directory \"%s\" fd: %s\n", module_dir, strerror(errno));

          close(fd);

          break;
        }

        break;
      }
      case GetCleanNamespace: {
        pid_t pid = 0;
        ssize_t ret = read_uint32_t(client_fd, (uint32_t *)&pid);
        ASSURE_SIZE_READ_BREAK("GetCleanNamespace", "pid", ret, sizeof(pid));

        uint8_t mns_state = 0;
        ret = read_uint8_t(client_fd, &mns_state);
        ASSURE_SIZE_READ_BREAK("GetCleanNamespace", "mns_state", ret, sizeof(mns_state));

        uint32_t our_pid = (uint32_t)getpid();
        ret = write_uint32_t(client_fd, (uint32_t)our_pid);
        ASSURE_SIZE_WRITE_BREAK("GetCleanNamespace", "our_pid", ret, sizeof(our_pid));

        if ((enum MountNamespaceState)mns_state == Clean) {
          save_mns_fd(pid, Rooted, impl);
          save_mns_fd(pid, Module, impl);
        }

        uint32_t clean_namespace_fd = (uint32_t)save_mns_fd(pid, (enum MountNamespaceState)mns_state, impl);
        ret = write_uint32_t(client_fd, clean_namespace_fd);
        ASSURE_SIZE_WRITE_BREAK("GetCleanNamespace", "clean_namespace_fd", ret, sizeof(clean_namespace_fd));

        break;
      }
    }

    if (action != RequestCompanionSocket) close(client_fd);

    continue;
  }

  close(socket_fd);
  free_modules(&context);
}
