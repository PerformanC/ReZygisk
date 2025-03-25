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

#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <poll.h>

#include "root_impl/common.h"
#include "constants.h"
#include "utils.h"

// Forward declarations
struct Module;
struct Context;

// Struct definitions
struct Module {
  int companion;
  void *handle;
  char *name;
  int api;
  void *preload_lib;
  void *api_table;
};

struct Context {
  struct Module *modules;
  size_t len;
  struct root_impl root_impl;
};

// Function declarations
static void cleanup_daemon_resources(struct Context *context);
static void free_modules(struct Context *context);
static void load_modules(enum Architecture arch, struct Context *context);

#define MODULE_OPERATION_TIMEOUT 5 // 5 seconds timeout for module operations

static volatile sig_atomic_t daemon_shutdown = 0;

static void handle_shutdown_signal(int sig) {
  LOGI("Daemon received signal %d, initiating shutdown\n", sig);
  daemon_shutdown = 1;
}

static void setup_daemon_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handle_shutdown_signal;
  sigemptyset(&sa.sa_mask);
  
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
}

static void cleanup_daemon_resources(struct Context *context) {
  if (!context) return;
  
  // Close all module sockets
  for (size_t i = 0; i < context->len; i++) {
    if (context->modules[i].companion != -1) {
      close(context->modules[i].companion);
      context->modules[i].companion = -1;
    }
  }
  
  // Free module memory
  if (context->modules) {
    free(context->modules);
    context->modules = NULL;
    context->len = 0;
  }
  
  LOGI("Daemon resources cleaned up\n");
}

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

  return so_fd;
}

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

    // Use calloc instead of realloc to avoid potential memory fragmentation
    struct Module *new_modules = calloc(context->len + 1, sizeof(struct Module));
    if (new_modules == NULL) {
      LOGE("Failed allocating memory for modules.\n");
      close(lib_fd);
      continue;
    }

    // Copy existing modules if any
    if (context->modules != NULL) {
      memcpy(new_modules, context->modules, context->len * sizeof(struct Module));
      free(context->modules);
    }
    
    context->modules = new_modules;
    context->modules[context->len].name = strdup(name);
    context->modules[context->len].lib_fd = lib_fd;
    context->modules[context->len].companion = -1;
    context->len++;
  }
  
  closedir(dir);
}

static void free_modules(struct Context *restrict context) {
  for (size_t i = 0; i < context->len; i++) {
    free(context->modules[i].name);
    if (context->modules[i].companion != -1) close(context->modules[i].companion);
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
    LOGE("Failed creating socket pair: %s\n", strerror(errno));
    return -1;
  }

  int daemon_fd = sockets[0];
  int companion_fd = sockets[1];

  pid_t pid = fork();
  if (pid < 0) {
    LOGE("Failed forking companion: %s\n", strerror(errno));
    close(companion_fd);
    close(daemon_fd);
    return -1;
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
        LOGE("Failed reading companion response: %s\n", strerror(errno));
        close(daemon_fd);
        return -1;
      }

      switch (response) {
        /* INFO: Even without any entry, we should still just deal with it */
        case 0: { return -2; }
        case 1: { return daemon_fd; }
        /* Close daemon socket for any unexpected response */
        default: {
          close(daemon_fd);
          return -1;
        }
      }
    } else {
      LOGE("Companion exited with status %d\n", status);
      close(daemon_fd);
      return -1;
    }
  /* INFO: if pid == 0: */
  } else {
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

void zygiskd_start(char *restrict argv[]) {
  setup_daemon_signals();

  struct Context context = { 0 };

  struct root_impl impl;
  get_impl(&impl);
  if (impl.impl == None || impl.impl == Multiple) {
    struct MsgHead *msg = NULL;
    const char *error_msg = (impl.impl == None) ? 
      "Unsupported environment: Unknown root implementation" : 
      "Unsupported environment: Multiple root implementations found";
    
    size_t msg_len = strlen(error_msg) + 1;
    msg = malloc(sizeof(struct MsgHead) + msg_len);
    if (msg == NULL) {
      LOGE("Failed allocating memory for message.\n");
      return;
    }

    msg->cmd = DAEMON_SET_ERROR_INFO;
    msg->length = sprintf(msg->data, "%s", error_msg);

    unix_datagram_sendto(CONTROLLER_SOCKET, (void *)msg, (size_t)((int)sizeof(struct MsgHead) + msg->length));

    free(msg);
  } else {
    enum Architecture arch = get_arch();
    load_modules(arch, &context);

    char *module_list = NULL;
    size_t module_list_len = 0;
    
    // Improved module list building with better memory handling
    if (context.len == 0) {
      module_list = strdup("None");
      if (module_list == NULL) {
        LOGE("Failed allocating memory for module list.\n");
        return;
      }
      module_list_len = strlen("None");
    } else {
      // Calculate required buffer size first
      for (size_t i = 0; i < context.len; i++) {
        module_list_len += strlen(context.modules[i].name);
        if (i != context.len - 1) {
          module_list_len += strlen(", ");
        }
      }
      
      // Allocate buffer once with the right size
      module_list = malloc(module_list_len + 1);
      if (module_list == NULL) {
        LOGE("Failed allocating memory for module list.\n");
        free_modules(&context);
        return;
      }
      
      // Build the string
      char *p = module_list;
      for (size_t i = 0; i < context.len; i++) {
        size_t name_len = strlen(context.modules[i].name);
        memcpy(p, context.modules[i].name, name_len);
        p += name_len;
        
        if (i != context.len - 1) {
          memcpy(p, ", ", 2);
          p += 2;
        }
      }
      *p = '\0';
    }

    char impl_name[LONGEST_ROOT_IMPL_NAME];
    stringify_root_impl_name(impl, impl_name);

    size_t msg_length = strlen("Root: , Modules: ") + strlen(impl_name) + module_list_len + 1;

    struct MsgHead *msg = malloc(sizeof(struct MsgHead) + msg_length);
    if (msg == NULL) {
      LOGE("Failed allocating memory for message.\n");
      free(module_list);
      free_modules(&context);
      return;
    }
    
    msg->length = snprintf(msg->data, msg_length, "Root: %s, Modules: %s", impl_name, module_list) + 1;
    msg->cmd = DAEMON_SET_INFO;

    unix_datagram_sendto(CONTROLLER_SOCKET, (void *)msg, (size_t)((int)sizeof(struct MsgHead) + msg->length));

    free(msg);
    free(module_list);
  }

  int socket_fd = create_daemon_socket();
  if (socket_fd == -1) {
    LOGE("Failed creating daemon socket\n");
    free_modules(&context);
    return;
  }

  // Set socket to non-blocking mode to prevent hanging
  int flags = fcntl(socket_fd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
  }

  // Setup poll for socket monitoring with timeout
  struct pollfd pfd;
  pfd.fd = socket_fd;
  pfd.events = POLLIN;
  
  while (!daemon_shutdown) {
    // Use poll with timeout to prevent CPU spinning
    int poll_result = poll(&pfd, 1, 1000); // 1 second timeout
    
    if (poll_result < 0) {
      if (errno == EINTR) continue;
      LOGE("poll error: %s\n", strerror(errno));
      break;
    }
    
    if (poll_result == 0) continue; // Timeout, continue waiting
    
    if (!(pfd.revents & POLLIN)) continue;
    
    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      LOGE("accept: %s\n", strerror(errno));
      continue;
    }

    uint8_t action8 = 0;
    ssize_t len = read_uint8_t(client_fd, &action8);
    if (len == -1) {
      LOGE("read: %s\n", strerror(errno));
      close(client_fd);
      continue;
    } else if (len == 0) {
      LOGI("Client disconnected\n");
      close(client_fd);
      continue;
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
      /* TODO: Move to another thread and save client fds to an epoll list
                 so that we can, in a single-thread, deal with multiple logcats */
      case RequestLogcatFd: {
        uint8_t level = 0;
        ssize_t ret = read_uint8_t(client_fd, &level);
        ASSURE_SIZE_READ_BREAK("RequestLogcatFd", "level", ret, sizeof(level));

        char tag[128 + 1];
        ret = read_string(client_fd, tag, sizeof(tag));
        if (ret == -1) {
          LOGE("Failed reading logcat tag.\n");

          close(client_fd);

          break;
        }

        char message[1024 + 1];
        ret = read_string(client_fd, message, sizeof(message));
        if (ret == -1) {
          LOGE("Failed reading logcat message.\n");

          close(client_fd);

          break;
        }

        __android_log_print(level, tag, "%s", message);

        break;
      }
      case GetProcessFlags: {
        uint32_t uid = 0;
        ssize_t ret = read_uint32_t(client_fd, &uid);
        ASSURE_SIZE_READ_BREAK("GetProcessFlags", "uid", ret, sizeof(uid));

        uint32_t process_flags = 0;
        
        if (uid_is_manager(uid)) {
          process_flags |= PROCESS_IS_MANAGER;
        } else {
          if (uid_granted_root(uid)) {
            process_flags |= PROCESS_GRANTED_ROOT;
          }
          if (uid_should_umount(uid)) {
            process_flags |= PROCESS_ON_DENYLIST;
          }
        }
        
        switch (impl.impl) {
          case None: { break; }
          case Multiple: { break; }
          case KernelSU: {
            process_flags |= PROCESS_ROOT_IS_KSU;

            break;
          }
          case APatch: {
            process_flags |= PROCESS_ROOT_IS_APATCH;

            break;
          }
          case Magisk: {
            process_flags |= PROCESS_ROOT_IS_MAGISK;

            break;
          }
        }

        ret = write_uint32_t(client_fd, process_flags);
        ASSURE_SIZE_WRITE_BREAK("GetProcessFlags", "process_flags", ret, sizeof(process_flags));

        break;
      }
      case GetInfo: {
        uint32_t info_flags = 0;

        switch (impl.impl) {
          case None: { break; }
          case Multiple: { break; }
          case KernelSU: {
            info_flags |= PROCESS_ROOT_IS_KSU;

            break;
          }
          case APatch: {
            info_flags |= PROCESS_ROOT_IS_APATCH;

            break;
          }
          case Magisk: {
            info_flags |= PROCESS_ROOT_IS_MAGISK;

            break;
          }
        }

        ssize_t ret = write_uint32_t(client_fd, info_flags);
        ASSURE_SIZE_WRITE_BREAK("GetInfo", "info_flags", ret, sizeof(info_flags));

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
        if (ret == -1 || index >= context.len) {
          LOGE("Invalid module index: %zu\n", index);
          ret = write_uint8_t(client_fd, 0);
          close(client_fd);
          break;
        }

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
            LOGE(" - Failed to send companion fd socket of module \"%s\": %s\n", module->name, strerror(errno));

            ret = write_uint8_t(client_fd, 0);
            close(module->companion);
            module->companion = -1;
            close(client_fd);
          }
        } else {
          LOGE(" - Failed to spawn companion for module \"%s\"\n", module->name);
          ret = write_uint8_t(client_fd, 0);
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
    }

    if (action != RequestCompanionSocket && action != RequestLogcatFd) close(client_fd);

    continue;
  }

  // Proper cleanup on exit
  close(socket_fd);
  cleanup_daemon_resources(&context);
}
