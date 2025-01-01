#include <linux/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#include "daemon.h"
#include "dl.h"
#include "socket_utils.h"

namespace zygiskd {
  static std::string TMP_PATH;
  void Init(const char *path) {
    TMP_PATH = path;
  }

  std::string GetTmpPath() {
    return TMP_PATH;
  }

  int Connect(uint8_t retry) {
    retry++;

    int fd = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = {
      .sun_family = AF_UNIX,
      .sun_path = { 0 }
    };

    auto socket_path = TMP_PATH + kCPSocketName;
    strcpy(addr.sun_path, socket_path.c_str());
    socklen_t socklen = sizeof(addr);

    while (--retry) {
      int r = connect(fd, (struct sockaddr *)&addr, socklen);
      if (r == 0) return fd;
      if (retry) {
        PLOGE("Retrying to connect to zygiskd, sleep 1s");

        sleep(1);
      }
    }

    close(fd);

    return -1;
  }

  bool PingHeartbeat() {
    int fd = Connect(5);
    if (fd == -1) {
      PLOGE("Connect to zygiskd");

      return false;
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::PingHeartBeat);

    close(fd);

    return true;
  }

  int RequestLogcatFd() {
    int fd = Connect(1);
    if (fd == -1) {
      PLOGE("RequestLogcatFd");

      return -1;
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::RequestLogcatFd);

    return fd;
  }

  uint32_t GetProcessFlags(uid_t uid) {
    int fd = Connect(1);
    if (fd == -1) {
      PLOGE("GetProcessFlags");

      return 0;
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::GetProcessFlags);
    socket_utils::write_u32(fd, uid);

    uint32_t res = socket_utils::read_u32(fd);

    close(fd);

    return res;
  }

  std::vector<ModuleInfo> ReadModules() {
    std::vector<ModuleInfo> modules;
    int fd = Connect(1);
    if (fd == -1) {
      PLOGE("ReadModules");

      return modules;
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::ReadModules);
    size_t len = socket_utils::read_usize(fd);
    for (size_t i = 0; i < len; i++) {
      std::string name = socket_utils::read_string(fd);
      int module_fd = socket_utils::recv_fd(fd);
      modules.emplace_back(name, module_fd);
    }

    close(fd);

    return modules;
  }

  int ConnectCompanion(size_t index) {
    int fd = Connect(1);
    if (fd == -1) {
      PLOGE("ConnectCompanion");

      return -1;
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::RequestCompanionSocket);
    socket_utils::write_usize(fd, index);

    uint8_t res = socket_utils::read_u8(fd);

    if (res == 1) return fd;
    else {
      close(fd);

      return -1;
    }
  }

  int GetModuleDir(size_t index) {
    int fd = Connect(1);
    if (fd == -1) {
      PLOGE("GetModuleDir");

      return -1;
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::GetModuleDir);
    socket_utils::write_usize(fd, index);
    int nfd = socket_utils::recv_fd(fd);

    close(fd);

    return nfd;
  }

  void ZygoteRestart() {
    int fd = Connect(1);
    if (fd == -1) {
      if (errno == ENOENT) LOGD("Could not notify ZygoteRestart (maybe it hasn't been created)");
      else PLOGE("Could not notify ZygoteRestart");

      return;
    }

    if (!socket_utils::write_u8(fd, (uint8_t) SocketAction::ZygoteRestart))
      PLOGE("Failed to request ZygoteRestart");

    close(fd);
  }

  void SystemServerStarted() {
    int fd = Connect(1);
    if (fd == -1) PLOGE("Failed to report system server started");
    else {
      if (!socket_utils::write_u8(fd, (uint8_t) SocketAction::SystemServerStarted))
        PLOGE("Failed to report system server started");
    }

    close(fd);
  }

  void GetInfo(struct zygote_info *info) {
    /* TODO: Optimize and avoid re-connect twice here */
    int fd = Connect(1);

    if (fd != -1) {
      info->running = true;

      socket_utils::write_u8(fd, (uint8_t) SocketAction::GetInfo);

      int flags = socket_utils::read_u32(fd);

      if (flags & (1 << 27)) {
        info->root_impl = ZYGOTE_ROOT_IMPL_APATCH;
      } else if (flags & (1 << 29)) {
        info->root_impl = ZYGOTE_ROOT_IMPL_KERNELSU;
      } else if (flags & (1 << 30)) {
        info->root_impl = ZYGOTE_ROOT_IMPL_MAGISK;
      } else {
        info->root_impl = ZYGOTE_ROOT_IMPL_NONE;
      }

      info->pid = socket_utils::read_u32(fd);

      info->modules = (struct zygote_modules *)malloc(sizeof(struct zygote_modules));
      if (info->modules == NULL) {
        info->modules->modules_count = 0;

        close(fd);

        return;
      }

      info->modules->modules_count = socket_utils::read_usize(fd);

      if (info->modules->modules_count == 0) {
        info->modules->modules = NULL;

        close(fd);

        return;
      }

      info->modules->modules = (char **)malloc(sizeof(char *) * info->modules->modules_count);
      if (info->modules->modules == NULL) {
        free(info->modules);
        info->modules = NULL;
        info->modules->modules_count = 0;

        close(fd);

        return;
      }

      for (size_t i = 0; i < info->modules->modules_count; i++) {
        /* INFO by ThePedroo: Ugly solution to read with std::string existance (temporary) */
        std::string name = socket_utils::read_string(fd);

        char module_path[PATH_MAX];
        snprintf(module_path, sizeof(module_path), "/data/adb/modules/%s/module.prop", name.c_str());

        FILE *module_prop = fopen(module_path, "r");
        if (module_prop == NULL) {
          info->modules->modules[i] = strdup(name.c_str());
        } else {
          char line[1024];
          while (fgets(line, sizeof(line), module_prop) != NULL) {
            if (strncmp(line, "name=", 5) == 0) {
              info->modules->modules[i] = strndup(line + 5, strlen(line) - 6);

              break;
            }
          }

          fclose(module_prop);
        }
      }

      close(fd);
    } else info->running = false;
  }

  std::string UpdateMountNamespace(enum mount_namespace_state nms_state) {
    int fd = Connect(1);
    if (fd == -1) {
      PLOGE("UpdateMountNamespace");

      return "";
    }

    socket_utils::write_u8(fd, (uint8_t) SocketAction::UpdateMountNamespace);
    socket_utils::write_u32(fd, getpid());
    socket_utils::write_u8(fd, (uint8_t)nms_state);

    uint32_t target_pid = socket_utils::read_u32(fd);
    int target_fd = 0;

    if (target_pid == 0) goto error;

    target_fd = (int)socket_utils::read_u32(fd);
    if (target_fd == 0) goto error;

    close(fd);
  
    return "/proc/" + std::to_string(target_pid) + "/fd/" + std::to_string(target_fd);

    error:
      close(fd);

      return "";
  }
}
