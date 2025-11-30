#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <linux/un.h>

#include "logging.h"
#include "socket_utils.h"

#include "daemon.h"

int rezygiskd_connect(uint8_t retry) {
  retry++;

  int fd = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd == -1) {
    PLOGE("socket create");

    return -1;
  }

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX,
    .sun_path = { 0 }
  };

  /*
    INFO: Application must assume that sun_path can hold _POSIX_PATH_MAX characters.

    Sources:
     - https://pubs.opengroup.org/onlinepubs/009696699/basedefs/sys/un.h.html
  */
  strcpy(addr.sun_path, TMP_PATH "/" SOCKET_FILE_NAME);
  socklen_t socklen = sizeof(addr);

  while (--retry) {
    int ret = connect(fd, (struct sockaddr *)&addr, socklen);
    if (ret == 0) return fd;
    if (retry) {
      PLOGE("Retrying to connect to ReZygiskd, sleep 1s");

      sleep(1);
    }
  }

  close(fd);

  return -1;
}

bool rezygiskd_ping() {
  int fd = rezygiskd_connect(5);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return false;
  }

  write_uint8_t(fd, PingHeartbeat);

  close(fd);

  return true;
}

uint32_t rezygiskd_get_process_flags(uid_t uid, const char *const process) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return 0;
  }

  write_uint8_t(fd, GetProcessFlags);
  write_uint32_t(fd, uid);
  write_string(fd, process);

  uint32_t res = 0;
  read_uint32_t(fd, &res);

  close(fd);

  return res;
}

void rezygiskd_get_info(struct rezygisk_info *info) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    info->running = false;

    return;
  }

  info->running = true;

  write_uint8_t(fd, GetInfo);

  uint32_t flags = 0;
  read_uint32_t(fd, &flags);

  if (flags & (1 << 27)) info->root_impl = ROOT_IMPL_APATCH;
  else if (flags & (1 << 29)) info->root_impl = ROOT_IMPL_KERNELSU;
  else if (flags & (1 << 30)) info->root_impl = ROOT_IMPL_MAGISK;
  else info->root_impl = ROOT_IMPL_NONE;

  read_uint32_t(fd, (uint32_t *)&info->pid);

  read_size_t(fd, &info->modules.modules_count);
  if (info->modules.modules_count == 0) {
    info->modules.modules = NULL;

    close(fd);

    return;
  }

  info->modules.modules = malloc(sizeof(*info->modules.modules) * info->modules.modules_count);
  if (!info->modules.modules) {
    PLOGE("allocating modules name memory");

    info->modules.modules_count = 0;

    close(fd);

    return;
  }

  for (size_t i = 0; i < info->modules.modules_count; i++) {
    char *module_name = read_string(fd);
    if (module_name == NULL) {
      PLOGE("reading module name");

      goto info_cleanup;
    }

    char module_path[PATH_MAX];
    snprintf(module_path, sizeof(module_path), "/data/adb/modules/%s/module.prop", module_name);

    free(module_name);

    FILE *module_prop = fopen(module_path, "r");
    if (!module_prop) {
      PLOGE("failed to open module prop file %s", module_path);

      goto info_cleanup;
    }

    info->modules.modules[i] = NULL;

    char line[1024];
    while (fgets(line, sizeof(line), module_prop) != NULL) {
      if (strncmp(line, "name=", strlen("name=")) != 0) continue;

      info->modules.modules[i] = strndup(line + 5, strlen(line) - 6);

      break;
    }
    fclose(module_prop);

    if (info->modules.modules[i] == NULL) {
      PLOGE("failed to read module name from %s", module_path);

      goto info_cleanup;
    }

    continue;

    info_cleanup:
      info->modules.modules_count = i;
      free_rezygisk_info(info);
  }

  close(fd);
}

void free_rezygisk_info(struct rezygisk_info *info) {
  for (size_t i = 0; i < info->modules.modules_count; i++) {
    free(info->modules.modules[i]);
  }

  free(info->modules.modules);
  info->modules.modules = NULL;
}

bool rezygiskd_read_modules(struct zygisk_modules *modules) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return false;
  }

  write_uint8_t(fd, ReadModules);

  size_t len = 0;
  read_size_t(fd, &len);

  modules->modules = malloc(len * sizeof(*modules->modules));
  if (!modules->modules) {
    PLOGE("allocating modules name memory");

    close(fd);

    return false;
  }
  modules->modules_count = len;

  for (size_t i = 0; i < len; i++) {
    char *lib_path = read_string(fd);
    if (!lib_path) {
      PLOGE("reading module lib_path");

      close(fd);

      return false;
    }

    modules->modules[i] = lib_path;
  }

  close(fd);

  return true;
}

void free_modules(struct zygisk_modules *modules) {
  for (size_t i = 0; i < modules->modules_count; i++) {
    free(modules->modules[i]);
  }

  free(modules->modules);
}

int rezygiskd_connect_companion(size_t index) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return -1;
  }

  write_uint8_t(fd, RequestCompanionSocket);
  write_size_t(fd, index);

  uint8_t res = 0;
  read_uint8_t(fd, &res);

  if (res == 1) return fd;
  else {
    close(fd);

    return -1;
  }
}

int rezygiskd_get_module_dir(size_t index) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return -1;
  }

  write_uint8_t(fd, GetModuleDir);
  write_size_t(fd, index);

  int dirfd = read_fd(fd);

  close(fd);

  return dirfd;
}

void rezygiskd_zygote_restart() {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    if (errno == ENOENT) LOGD("Could not notify ZygoteRestart (maybe it hasn't been created)");
    else PLOGE("Could not notify ZygoteRestart");

    return;
  }

  if (!write_uint8_t(fd, ZygoteRestart))
    PLOGE("Failed to request ZygoteRestart");

  close(fd);
}

void rezygiskd_system_server_started() {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("Failed to report system server started");

    return;
  }

  if (!write_uint8_t(fd, SystemServerStarted))
    PLOGE("Failed to request SystemServerStarted");

  close(fd);
}

bool rezygiskd_update_mns(enum mount_namespace_state nms_state, char *buf, size_t buf_size) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return false;
  }

  write_uint8_t(fd, UpdateMountNamespace);
  write_uint32_t(fd, getpid());
  write_uint8_t(fd, nms_state);

  uint32_t target_pid = 0;
  if (read_uint32_t(fd, &target_pid) < 0) {
    PLOGE("Failed to read target pid");

    close(fd);

    return false;
  }

  uint32_t target_fd = 0;
  if (read_uint32_t(fd, &target_fd) < 0) {
    PLOGE("Failed to read target fd");

    close(fd);

    return false;
  }

  if (target_fd == 0) {
    LOGE("Failed to get target fd");

    close(fd);

    return false;
  }

  snprintf(buf, buf_size, "/proc/%u/fd/%u", target_pid, target_fd);

  close(fd);

  return true;
}
