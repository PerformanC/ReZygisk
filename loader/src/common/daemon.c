#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <linux/un.h>
#include <sys/socket.h>

#include "logging.h"
#include "misc.h"
#include "socket_utils.h"

#include "daemon.h"

#define SOCKET_FILE_NAME LP_SELECT("cp32", "cp64") ".sock"

int rezygiskd_connect(uint8_t retry) {
  const char *sock_path = TMP_PATH "/" SOCKET_FILE_NAME;

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX,
    .sun_path = { 0 }
  };

  /*
    INFO: Application must assume that sun_path can hold _POSIX_PATH_MAX characters.

    Sources:
     - https://pubs.opengroup.org/onlinepubs/009696699/basedefs/sys/un.h.html
  */
  strcpy(addr.sun_path, sock_path);
  socklen_t socklen = sizeof(addr);

  retry++;
  while (--retry) {
    int fd = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd == -1) {
      PLOGE("socket create");

      return -1;
    }

    int ret = connect(fd, (struct sockaddr *)&addr, socklen);
    if (ret == 0) return fd;

    close(fd);

    if (retry) {
      PLOGE("Failed to connect to ReZygiskd, retrying...");

      sleep(1);
    }
  }

  return -1;
}

/* TODO: We should unify all of those */
#define safe_write(fn, name, ret_type)             \
  if (!fn) {                                       \
    LOGE("Failed to write " name " to ReZygiskd"); \
                                                   \
    close(fd);                                     \
                                                   \
    ret_type;                                      \
  }

#define safe_read(fn, name, ret_type)              \
  if (!fn) {                                       \
    LOGE("Failed to read " name " from ReZygiskd"); \
                                                   \
    close(fd);                                     \
                                                   \
    ret_type;                                      \
  }

bool rezygiskd_zygote_injected() {
  int fd = rezygiskd_connect(5);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return false;
  }

  safe_write(write_uint8_t(fd, (uint8_t)ZygoteInjected), "ZygoteInjected action", return false);

  close(fd);

  return true;
}

uint32_t rezygiskd_get_process_flags(uid_t uid, const char *const process) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return 0;
  }

  safe_write(write_uint8_t(fd, (uint8_t)GetProcessFlags), "GetProcessFlags action", return 0);
  safe_write(write_uint32_t(fd, (uint32_t)uid), "uid", return 0);
  safe_write(write_string(fd, process), "process name", return 0);

  uint32_t res = 0;
  safe_read(read_uint32_t(fd, &res), "process flags", return 0);

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

  safe_write(write_uint8_t(fd, (uint8_t)GetInfo), "GetInfo action", return);

  uint32_t flags = 0;
  safe_read(read_uint32_t(fd, &flags), "info flags", return);

  if (flags & (1 << 27)) info->root_impl = ROOT_IMPL_APATCH;
  else if (flags & (1 << 29)) info->root_impl = ROOT_IMPL_KERNELSU;
  else if (flags & (1 << 30)) info->root_impl = ROOT_IMPL_MAGISK;
  else info->root_impl = ROOT_IMPL_NONE;

  safe_read(read_uint32_t(fd, (uint32_t *)&info->pid), "pid", return);

  safe_read(read_size_t(fd, &info->modules.modules_count), "modules count", return);
  if (info->modules.modules_count == 0) {
    info->modules.modules = NULL;

    close(fd);

    return;
  }

  info->modules.modules = (char **)malloc(sizeof(char *) * info->modules.modules_count);
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

    if (info->modules.modules[i] == NULL) {
      PLOGE("failed to read module name from %s", module_path);

      fclose(module_prop);

      goto info_cleanup;
    }

    fclose(module_prop);

    continue;

    info_cleanup:
      info->modules.modules_count = i;
      free_rezygisk_info(info);

      break;
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

  safe_write(write_uint8_t(fd, (uint8_t)ReadModules), "ReadModules action", return false);

  size_t len = 0;
  safe_read(read_size_t(fd, &len), "modules count", return false);

  modules->modules = malloc(len * sizeof(char *));
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

  safe_write(write_uint8_t(fd, (uint8_t)RequestCompanionSocket), "RequestCompanionSocket action", return -1);
  safe_write(write_size_t(fd, index), "companion index", return -1);

  uint8_t res = 0;
  safe_read(read_uint8_t(fd, &res), "companion socket result", return -1);

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

  safe_write(write_uint8_t(fd, (uint8_t)GetModuleDir), "GetModuleDir action", return -1);
  safe_write(write_size_t(fd, index), "module index", return -1);

  int dirfd = read_fd(fd);

  close(fd);

  return dirfd;
}

void rezygiskd_zygote_restart() {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    if (errno == ENOENT) LOGD("Failed to connect to connect, file nonexistent (ReZygiskd not running?)");
    else PLOGE("connection to ReZygiskd");

    return;
  }

  safe_write(write_uint8_t(fd, (uint8_t)ZygoteRestart), "ZygoteRestart action", return);

  close(fd);
}

void rezygiskd_system_server_started() {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("Failed to report system server started");

    return;
  }

  safe_write(write_uint8_t(fd, (uint8_t)SystemServerStarted), "SystemServerStarted action", return);

  close(fd);
}

bool rezygiskd_update_mns(enum mount_namespace_state nms_state, char *buf, size_t buf_size) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return false;
  }

  safe_write(write_uint8_t(fd, (uint8_t)UpdateMountNamespace), "UpdateMountNamespace action", return false);
  safe_write(write_uint32_t(fd, (uint32_t)getpid()), "pid", return false);
  safe_write(write_uint8_t(fd, (uint8_t)nms_state), "mount namespace state", return false);

  uint32_t target_pid = 0;
  safe_read(read_uint32_t(fd, &target_pid), "target pid", return false);

  uint32_t target_fd = 0;
  safe_read(read_uint32_t(fd, &target_fd), "target fd", return false);

  if (target_fd == 0) {
    LOGE("Failed to get target fd");

    close(fd);

    return false;
  }

  snprintf(buf, buf_size, "/proc/%u/fd/%u", target_pid, target_fd);

  close(fd);

  return true;
}

bool rezygiskd_remove_module(size_t index) {
  int fd = rezygiskd_connect(1);
  if (fd == -1) {
    PLOGE("connection to ReZygiskd");

    return false;
  }

  safe_write(write_uint8_t(fd, (uint8_t)RemoveModule), "RemoveModule action", return false);
  safe_write(write_size_t(fd, index), "module index", return false);

  uint8_t res = 0;
  safe_read(read_uint8_t(fd, &res), "remove module result", return false);

  close(fd);

  return res == 1;
}

#undef safe_read
#undef safe_write
