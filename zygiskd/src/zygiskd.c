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

#include <unistd.h>
#include <linux/limits.h>

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
  int len;
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
#define ZYGISKD_FILE "zygiskd" lp_select("32", "64")
#define ZYGISKD_PATH "/data/adb/modules/zygisksu/bin/zygiskd" lp_select("32", "64")

#define ASSURE_SIZE_WRITE(area_name, subarea_name, sent_size, expected_size)                                    \
  if (sent_size != (ssize_t)(expected_size)) {                                                                             \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                \
    return;                                                                                                     \
  }

#define ASSURE_SIZE_READ(area_name, subarea_name, sent_size, expected_size)                                     \
  if (sent_size != (ssize_t)(expected_size)) {                                                                             \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                \
    return;                                                                                                     \
  }

#define ASSURE_SIZE_WRITE_BREAK(area_name, subarea_name, sent_size, expected_size)                               \
  if (sent_size != (ssize_t)(expected_size)) {                                                                              \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    break;                                                                                                       \
  }

#define ASSURE_SIZE_READ_BREAK(area_name, subarea_name, sent_size, expected_size)                                \
  if (sent_size != (ssize_t)(expected_size)) {                                                                              \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    break;                                                                                                       \
  }

#define ASSURE_SIZE_WRITE_WR(area_name, subarea_name, sent_size, expected_size)                                 \
  if (sent_size != (ssize_t)(expected_size)) {                                                                             \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                \
    return -1;                                                                                                  \
  }

#define ASSURE_SIZE_READ_WR(area_name, subarea_name, sent_size, expected_size)                                  \
  if (sent_size != (ssize_t)(expected_size)) {                                                                             \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                \
    return -1;                                                                                                  \
  }

static enum Architecture get_arch(void) {
  char system_arch[32];
  get_property("ro.product.cpu.abi", system_arch);

  if (strstr(system_arch, "arm") != NULL) return lp_select(ARM32, ARM64);
  if (strstr(system_arch, "x86") != NULL) return lp_select(X86, X86_64);

  LOGE("Unsupported system architecture: %s\n", system_arch);
  exit(1);
}

int create_library_fd(const char *so_path) {
  int memfd = memfd_create("jit-cache-zygisk", MFD_ALLOW_SEALING);
  if (memfd == -1) {
    perror("memfd_create");

    return -1;
  }

  int so_fd = open(so_path, O_RDONLY);
  if (so_fd == -1) {
    perror("open");
    close(memfd);

    return -1;
  }

  struct stat st;
  if (fstat(so_fd, &st) == -1) {
    perror("fstat");
    close(so_fd);
    close(memfd);

    return -1;
  }

  if (sendfile(memfd, so_fd, NULL, st.st_size) == -1) {
    perror("sendfile");
    close(so_fd);
    close(memfd);

    return -1;
  }

  close(so_fd);

  if (fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) == -1) {
    perror("fcntl");
    close(memfd);

    return -1;
  }

  return memfd;
}


/* WARNING: Dynamic memory based */
static void load_modules(enum Architecture arch, struct Context *context) {
  context->len = 0;
  context->modules = malloc(1);

  DIR *dir = opendir(PATH_MODULES_DIR);
  if (dir == NULL) {
    LOGE("Failed opening modules directory: %s.", PATH_MODULES_DIR);

    return;
  }

  char arch_str[32];
  switch (arch) {
    case ARM32: {
      strcpy(arch_str, "armeabi-v7a");

      break;
    }
    case ARM64: {
      strcpy(arch_str, "arm64-v8a");

      break;
    }
    case X86: {
      strcpy(arch_str, "x86");
  
      break;
    }
    case X86_64: {
      strcpy(arch_str, "x86_64");

      break;
    }
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

    if (stat(disabled, &st) != -1) {
      errno = 0;

      continue;
    }

    LOGI("Loading module `%s`...\n", name);
    int lib_fd = create_library_fd(so_path);
    if (lib_fd == -1) {
      LOGE("Failed loading module `%s`\n", name);

      continue;
    }

    LOGI("Loaded module lib fd: %d\n", lib_fd);

    context->modules = realloc(context->modules, ((context->len + 1) * sizeof(struct Module)));
    context->modules[context->len].name = strdup(name);
    context->modules[context->len].lib_fd = lib_fd;
    context->modules[context->len].companion = -1;
    context->len++;
  }
}

static void free_modules(struct Context *context) {
  for (int i = 0; i < context->len; i++) {
    free(context->modules[i].name);
    if (context->modules[i].companion != -1) close(context->modules[i].companion);
  }
}

static int create_daemon_socket(void) {
  set_socket_create_context("u:r:zygote:s0");
  
  return unix_listener_from_path(PATH_CP_NAME);
}

static int spawn_companion(char *name, int lib_fd) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
    LOGE("Failed creating socket pair.\n");

    return -1;
  }

  int daemon_fd = sockets[0];
  int companion_fd = sockets[1];

  LOGI("Companion fd: %d\n", companion_fd);
  LOGI("Daemon fd: %d\n", daemon_fd);

  pid_t pid = fork();
  LOGI("Forked: %d\n", pid);
  if (pid < 0) {
    LOGE("Failed forking companion: %s\n", strerror(errno));

    close(companion_fd);
    close(daemon_fd);

    exit(1);
  } else if (pid > 0) {
    close(companion_fd);

    LOGI("Waiting for companion to start (%d)\n", pid);

    int status = 0;
    // waitpid(pid, &status, 0);

    LOGI("Companion exited with status %d\n", status);

    // if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      if (write_string(daemon_fd, name) == -1) return -1;

      if (send_fd(daemon_fd, lib_fd) == -1) return -1;

      LOGI("Sent module name and lib fd\n");
      
      uint8_t response_buf[1];
      ssize_t ret = read(daemon_fd, &response_buf, sizeof(response_buf));
      ASSURE_SIZE_READ_WR("companion", "response", ret, sizeof(response_buf));

      uint8_t response = response_buf[0];

      LOGI("Companion response: %hhu\n", response);

      if (response == 0) return -2;
      else if (response == 1) return daemon_fd;
      else return -2;
    // } else {
    //   LOGE("Exited with status %d\n", status);

    //   close(daemon_fd);

    //   return -1;
    // }
  /* INFO: if pid == 0: */
  } else {
    LOGI("Companion started (%d)\n", pid);
    /* INFO: There is no case where this will fail with a valid fd. */
    fcntl(companion_fd, F_SETFD, 0);
  }

  char companion_fd_str[32];
  snprintf(companion_fd_str, 32, "%d", companion_fd);

  LOGI("Executing companion...\n");

  char *argv[] = { ZYGISKD_FILE, "companion", companion_fd_str, NULL };
  if (execv(ZYGISKD_PATH, argv) == -1) {
    LOGE("Failed executing companion: %s\n", strerror(errno));

    close(companion_fd);

    exit(1);
  }

  exit(0);
}

/* TODO: Is packed attribute really necessary? */
struct __attribute__((__packed__)) MsgHead {
  unsigned int cmd;
  int length;
  char data[0];
};

void zygiskd_start(void) {
  LOGI("Welcome to ReZygisk %s!", ZKSU_VERSION);

  enum Architecture arch = get_arch();

  struct Context context;
  load_modules(arch, &context);

  struct MsgHead *msg = NULL;
  size_t msg_sz = 0;

  switch (get_impl()) {
    case None: {
      /* INFO: Stop, compiler. */

      break;
    }
    case Multiple: {
      /* INFO: Stop, compiler. */

      break;
    }

    case KernelSU: {
      if (context.len == 0) {
        msg_sz = sizeof(struct MsgHead) + strlen("Root: KernelSU, Modules: None") + 1;
        msg = malloc(msg_sz);

        msg->cmd = DAEMON_SET_INFO;
        msg->length = strlen("Root: KernelSU, Modules: None") + 1;
        memcpy(msg->data, "Root: KernelSU, Modules: None", strlen("Root: KernelSU, Modules: None"));
      } else {
        char *module_list = malloc(1);
        size_t module_list_len = 0;

        for (int i = 0; i < context.len; i++) {
          if (i != context.len - 1) {
            module_list = realloc(module_list, module_list_len + strlen(context.modules[i].name) + strlen(", ") + 1);
            memcpy(module_list + module_list_len, context.modules[i].name, strlen(context.modules[i].name));

            module_list_len += strlen(context.modules[i].name);

            memcpy(module_list + module_list_len, ", ", strlen(", "));

            module_list_len += strlen(", ");
          } else {
            module_list = realloc(module_list, module_list_len + strlen(context.modules[i].name) + 1);
            memcpy(module_list + module_list_len, context.modules[i].name, strlen(context.modules[i].name));

            module_list_len += strlen(context.modules[i].name);
          }
        }

        msg_sz = sizeof(struct MsgHead) + strlen("Root: KernelSU, Modules: ") + module_list_len + 1;
        msg = malloc(msg_sz);

        msg->cmd = DAEMON_SET_INFO;
        msg->length = strlen("Root: KernelSU, Modules: ") + module_list_len + 1;
        memcpy(msg->data, "Root: KernelSU, Modules: ", strlen("Root: KernelSU, Modules: "));
        memcpy(msg->data + strlen("Root: KernelSU, Modules: "), module_list, module_list_len);

        free(module_list);
      }

      break;
    }
    default: {
      msg_sz = sizeof(struct MsgHead) + strlen("Invalid root implementation") + 1;
      msg = malloc(msg_sz);

      msg->cmd = DAEMON_SET_ERROR_INFO;
      msg->length = strlen("Invalid root implementation") + 1;
      memcpy(msg->data, "Invalid root implementation", strlen("Invalid root implementation"));

      break;
    }
  }

  unix_datagram_sendto(CONTROLLER_SOCKET, (void *)msg, msg_sz);

  free(msg);

  int socket_fd = create_daemon_socket();
  if (socket_fd == -1) {
    LOGE("Failed creating daemon socket\n");

    return;
  }

  while (1) {
    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
      LOGE("accept: %s\n", strerror(errno));

      return;
    }

    LOGI("Accepted client: %d\n", client_fd);

    unsigned char buf[1];
    ssize_t len = read(client_fd, buf, sizeof(buf));
    if (len == -1) {
      LOGE("read: %s\n", strerror(errno));

      return;
    } else if (len == 0) {
      LOGI("Client disconnected\n");

      return;
    }

    LOGI("Action: %hhu\n", (uint8_t)buf[0]);
    enum DaemonSocketAction action = (enum DaemonSocketAction)buf[0];

    switch (action) {
      case PingHeartbeat: {
        enum DaemonSocketAction msgr = ZYGOTE_INJECTED;
        unix_datagram_sendto(CONTROLLER_SOCKET, &msgr, sizeof(enum DaemonSocketAction));

        break;
      }
      case ZygoteRestart: {
        LOGI("Zygote restart\n");

        for (int i = 0; i < context.len; i++) {
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

        break;
      }
      case RequestLogcatFd: {
        char level_buf[1];
        ssize_t ret = read(client_fd, &level_buf, sizeof(level_buf));
        ASSURE_SIZE_READ_BREAK("RequestLogcatFd", "level", ret, sizeof(level_buf));

        char level = level_buf[0];

        char tag[128 + 1];
        ret = read_string(client_fd, tag, sizeof(tag) - 1);
        if (ret == -1) break;

        tag[ret] = '\0';

        char message[1024];
        ret = read_string(client_fd, message, sizeof(message));
        if (ret == -1) break;

        __android_log_print(level, tag, "%.*s", (int)ret, message);

        break;
      }
      case GetProcessFlags: {
        LOGI("Getting process flags\n");

        uid_t uid_buf[1];
        ssize_t ret = read(client_fd, &uid_buf, sizeof(uid_buf));
        ASSURE_SIZE_READ_BREAK("GetProcessFlags", "uid", ret, sizeof(uid_buf));

        uid_t uid = uid_buf[0];

        LOGI("Checking flags for uid: %d\n", uid);

        uint32_t flags = 0;
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

        LOGI("Flags for uid %d: %d\n", uid, flags);

        switch (get_impl()) {
          case None: {
            break;
          }
          case Multiple: {
            break;
          }
          case KernelSU: {
            flags |= PROCESS_ROOT_IS_KSU;
          }
        }

        // LOGI("Flags for uid %d: %d\n", uid, flags);

        LOGI("Sending flags\n");

        ret = write(client_fd, &flags, sizeof(flags));
        // ASSURE_SIZE_WRITE_BREAK("GetProcessFlags", "flags", ret, sizeof(flags));

        LOGI("Sent flags\n");

        break;
      }
      case GetInfo: {
        uint32_t flags = 0;

        LOGI("Getting info\n");

        switch (get_impl()) {
          case None: {
            break;
          }
          case Multiple: {
            break;
          }
          case KernelSU: {
            flags |= PROCESS_ROOT_IS_KSU;
          }
        }

        LOGI("Flags: %d\n", flags);

        ssize_t ret = write(client_fd, &flags, sizeof(flags));
        ASSURE_SIZE_WRITE_BREAK("GetInfo", "flags", ret, sizeof(flags));

        pid_t pid = getpid();

        LOGI("Getting pid: %d\n", pid);
    
        ret = write(client_fd, &pid, sizeof(pid));
        ASSURE_SIZE_WRITE_BREAK("GetInfo", "pid", ret, sizeof(pid));

        LOGI("Sent pid\n");

        break;
      }
      case ReadModules: {
        LOGI("Reading modules to stream\n");

        size_t clen = context.len;
        ssize_t ret = write(client_fd, &clen, sizeof(clen));
        ASSURE_SIZE_WRITE_BREAK("ReadModules", "len", ret, sizeof(clen));

        for (int i = 0; i < (int)clen; i++) {
          LOGI("Hey, we're talking about: %d\n", i);
          LOGI("Writing module `%s` to stream\n", context.modules[i].name);
          LOGI("Lib fd: %d\n", context.modules[i].lib_fd);

          size_t name_len = strlen(context.modules[i].name);

          LOGI("Name length: %zu\n", name_len);
          ret = write(client_fd, &name_len, sizeof(name_len));
          ASSURE_SIZE_WRITE_BREAK("ReadModules", "name length", ret, sizeof(name_len));

          LOGI("Writing name: %s\n", context.modules[i].name);
          ret = write(client_fd, context.modules[i].name, name_len);
          ASSURE_SIZE_WRITE_BREAK("ReadModules", "name", ret, name_len);

          LOGI("Writing lib fd: %d\n", context.modules[i].lib_fd);
          if (send_fd(client_fd, context.modules[i].lib_fd) == -1) break;
        }

        LOGI("Finished reading modules to stream\n");

        break;
      }
      case RequestCompanionSocket: {
        LOGI("Requesting companion socket\n");
      
        size_t index_buf[1];
        ssize_t ret = read(client_fd, &index_buf, sizeof(index_buf));
        ASSURE_SIZE_READ_BREAK("RequestCompanionSocket", "index", ret, sizeof(index_buf));

        size_t index = index_buf[0];

        struct Module *module = &context.modules[index];
        int companion_fd = module->companion;

        if (companion_fd != -1) {
          LOGI("Companion for module `%s` already exists\n", module->name);

          if (fcntl(companion_fd, F_GETFD) == -1) {
            LOGE("Poll companion for module `%s` crashed\n", module->name);
            close(companion_fd);

            module->companion = -1;
          }
        }

        if (companion_fd == -1) {
          LOGI("Spawning companion for `%s`\n", module->name);

          companion_fd = spawn_companion(module->name, module->lib_fd);

          if (companion_fd != -1) {
            LOGI("Spawned companion for `%s`\n", module->name);

            module->companion = companion_fd;

            if (send_fd(client_fd, companion_fd) == -1) break;
          } else if (companion_fd == -2) {
            LOGI("Could not spawn companion for `%s` as it has no entry\n", module->name);

            /* TODO: Avoid duplicated code -- Merge this and the one below. */
            uint8_t response = 0;
            ret = write(client_fd, &response, sizeof(response));
            ASSURE_SIZE_WRITE_BREAK("RequestCompanionSocket", "response", ret, sizeof(response));
          } else {
            LOGE("Failed to spawn companion for `%s`\n", module->name);

            uint8_t response = 0;
            ret = write(client_fd, &response, sizeof(response));
            ASSURE_SIZE_WRITE_BREAK("RequestCompanionSocket", "response", ret, sizeof(response));
          }

          LOGI("Companion fd: %d\n", companion_fd);
        }

        break;
      }
      case GetModuleDir: {
        LOGI("Getting module directory\n");

        size_t index_buf[1];
        ssize_t ret = read(client_fd, &index_buf, sizeof(index_buf));
        ASSURE_SIZE_READ_BREAK("GetModuleDir", "index", ret, sizeof(index_buf));

        size_t index = index_buf[0];

        LOGI("Index: %zu\n", index);

        char dir[PATH_MAX];
        snprintf(dir, PATH_MAX, "%s/%s", PATH_MODULES_DIR, context.modules[index].name);

        LOGI("Module directory: %s\n", dir);
        int dir_fd = open(dir, O_RDONLY);

        LOGI("Module directory fd: %d\n", dir_fd);

        if (send_fd(client_fd, dir_fd) == -1) break;

        LOGI("Sent module directory fd\n");

        break;
      }



      close(client_fd);
    }

    continue;
  }

  close(socket_fd);
  free_modules(&context);
}
