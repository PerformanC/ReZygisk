#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <errno.h>

#include <unistd.h>

#include "../constants.h"
#include "../utils.h"

#include "magisk.h"

char *supported_variants[] = {
  "kitsune"
};

char *magisk_managers[] = {
  "com.topjohnwu.magisk",
  "io.github.huskydg.magisk"
};

#define SBIN_MAGISK lp_select("/sbin/magisk32", "/sbin/magisk64")
#define DEBUG_RAMDISK_MAGISK lp_select("/debug_ramdisk/magisk32", "/debug_ramdisk/magisk64")
#define BITLESS_DEBUG_RAMDISK_MAGISK "/debug_ramdisk/magisk"

enum magisk_variants variant = Official;
/* INFO: Longest path */
static char path_to_magisk[sizeof(DEBUG_RAMDISK_MAGISK)];

enum RootImplState magisk_get_existence(void) {
  struct stat s;
  if (stat(SBIN_MAGISK, &s) != 0) {
    LOGE("Failed to stat Magisk /sbin/magisk binary: %s\n", strerror(errno));
  
    if (errno != ENOENT) {
      LOGE("Failed to stat Magisk /sbin/magisk binary: %s\n", strerror(errno));
    }
    errno = 0;

    if (stat(DEBUG_RAMDISK_MAGISK, &s) != 0) {
      LOGE("Failed to stat Magisk %s binary: %s\n", DEBUG_RAMDISK_MAGISK, strerror(errno));
    
      if (errno != ENOENT) {
        LOGE("Failed to stat Magisk %s binary: %s\n", DEBUG_RAMDISK_MAGISK, strerror(errno));
      }
      errno = 0;

      if (stat(BITLESS_DEBUG_RAMDISK_MAGISK, &s) != 0) {
        LOGE("Failed to stat Magisk %s binary: %s\n", BITLESS_DEBUG_RAMDISK_MAGISK, strerror(errno));
    
        if (errno != ENOENT) {
          LOGE("Failed to stat Magisk /debug_ramdisk/magisk binary: %s\n", strerror(errno));
        }
        errno = 0;

        return Inexistent;
      }

      /* INFO: /debug_ramdisk/magisk64 (or 32) doesn't exist but /debug_ramdisk/magisk does */
      strcpy(path_to_magisk, BITLESS_DEBUG_RAMDISK_MAGISK);
    } else {
      /* INFO: /sbin/magisk doesn't exist but /debug_ramdisk/magisk does */
      strcpy(path_to_magisk, DEBUG_RAMDISK_MAGISK);
    }
  } else {
    /* INFO: /sbin/magisk exists */
    strcpy(path_to_magisk, SBIN_MAGISK);
  }

  char *argv[] = { "magisk", "-v", NULL };

  char magisk_info[128];
  if (!exec_command(magisk_info, sizeof(magisk_info), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    return Abnormal;
  }

  for (unsigned long i = 0; i < sizeof(supported_variants) / sizeof(supported_variants[0]); i++) {
    if (strstr(magisk_info, supported_variants[i])) variant = (enum magisk_variants)(i + 1);
  }

  argv[1] = "-V";

  char magisk_version[32];
  if (!exec_command(magisk_version, sizeof(magisk_version), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    return Abnormal;
  }

  if (atoi(magisk_version) >= MIN_MAGISK_VERSION) return Supported;
  else return TooOld;
}

bool magisk_uid_granted_root(uid_t uid) {
  char sqlite_cmd[256];
  snprintf(sqlite_cmd, sizeof(sqlite_cmd), "select 1 from policies where uid=%d and policy=2 limit 1", uid);

  char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char result[32];
  if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    return false;
  }

  return result[0] != '\0';
}

bool magisk_uid_should_umount(uid_t uid) {
  struct dirent *entry;
  DIR *proc = opendir("/proc");
  if (!proc) {
    LOGE("Failed to open /proc: %s\n", strerror(errno));
    errno = 0;

    return false;
  }

  while ((entry = readdir(proc))) {
    if (entry->d_type != DT_DIR) continue;

    if (atoi(entry->d_name) == 0) continue;

    char stat_path[32];
    snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", entry->d_name);

    struct stat s;
    if (stat(stat_path, &s) == -1) continue;

    if (s.st_uid != uid) continue;

    char package_name[255 + 1];

    char cmdline_path[32];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);

    int cmdline = open(cmdline_path, O_RDONLY);
    if (cmdline == -1) {
      LOGE("Failed to open %s: %s\n", cmdline_path, strerror(errno));
      errno = 0;

      closedir(proc);

      continue;
    }

    ssize_t read_bytes = read(cmdline, package_name, sizeof(package_name) - 1);
    if (read_bytes == -1) {
      LOGE("Failed to read from %s: %s\n", cmdline_path, strerror(errno));
      errno = 0;

      close(cmdline);
      closedir(proc);

      continue;
    }

    close(cmdline);
    closedir(proc);

    package_name[read_bytes] = '\0';

    char sqlite_cmd[256];
    snprintf(sqlite_cmd, sizeof(sqlite_cmd), "select 1 from denylist where package_name=\"%s\" limit 1", package_name);

    char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

    char result[32];
    if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
      LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
      errno = 0;

      return false;
    }

    return result[0] != '\0';
  }

  return false;
}

bool magisk_uid_is_manager(uid_t uid) {
  char sqlite_cmd[256];
  snprintf(sqlite_cmd, sizeof(sqlite_cmd), "select value from strings where key=\"requester\" limit 1");

  char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char output[128] = { 0 };
  if (!exec_command(output, sizeof(output), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    return false;
  }

  char stat_path[PATH_MAX];
  if (output[0] == '\0')
    snprintf(stat_path, sizeof(stat_path), "/data/user_de/0/%s", magisk_managers[(int)variant]);
  else
    snprintf(stat_path, sizeof(stat_path), "/data/user_de/0/%s", output + strlen("value="));

  struct stat s;
  if (stat(stat_path, &s) == -1) {
    LOGE("Failed to stat %s: %s\n", stat_path, strerror(errno));
    errno = 0;

    return false;
  }

  return s.st_uid == uid;
}
