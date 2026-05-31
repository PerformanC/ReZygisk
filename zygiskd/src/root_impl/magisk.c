#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "magisk.h"

#include "../constants.h"
#include "../utils.h"
#include "common.h"

#define SBIN_MAGISK LP_SELECT("/sbin/magisk32", "/sbin/magisk64")
#define BITLESS_SBIN_MAGISK "/sbin/magisk"
#define DEBUG_RAMDISK_MAGISK LP_SELECT("/debug_ramdisk/magisk32", "/debug_ramdisk/magisk64")
#define BITLESS_DEBUG_RAMDISK_MAGISK "/debug_ramdisk/magisk"

/* INFO: Longest path */
static char path_to_magisk[sizeof(DEBUG_RAMDISK_MAGISK)] = { 0 };

void magisk_get_existence(struct root_impl_state *state) {
  const char *magisk_files[] = {
    SBIN_MAGISK,
    BITLESS_SBIN_MAGISK,
    DEBUG_RAMDISK_MAGISK,
    BITLESS_DEBUG_RAMDISK_MAGISK
  };

  for (size_t i = 0; i < sizeof(magisk_files) / sizeof(magisk_files[0]); i++) {
    if (access(magisk_files[i], F_OK) != 0) continue;

    snprintf(path_to_magisk, sizeof(path_to_magisk), "%s", magisk_files[i]);

    break;
  }

  if (path_to_magisk[0] == '\0') {
    state->state = Inexistent;

    return;
  }

  const char *argv[] = { "magisk", "-V", NULL };

  char magisk_version[32];
  if (!exec_command(magisk_version, sizeof(magisk_version), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s", strerror(errno));

    state->state = Abnormal;

    return;
  }

  if (atoi(magisk_version) >= MIN_MAGISK_VERSION) state->state = Supported;
  else state->state = TooOld;
}

bool magisk_uid_granted_root(uid_t uid) {
  char sqlite_cmd[256];
  snprintf(sqlite_cmd, sizeof(sqlite_cmd), "select 1 from policies where uid=%d and policy=2 limit 1", uid);

  const char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char result[32];
  if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s", strerror(errno));

    return false;
  }

  return result[0] != '\0';
}

bool magisk_uid_should_umount(const char *const process) {
  /* INFO: Validate process name to prevent SQL injection */
  for (const char *p = process; *p != '\0'; p++) {
    if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != ':' && *p != '-') return false;
  }

  /* INFO: PROCESS_NAME_MAX_LEN already has a +1 for NULL */
  char sqlite_cmd[59 + PROCESS_NAME_MAX_LEN];
  /* INFO: Find if process string starts with any data in "process" column */
  snprintf(sqlite_cmd, sizeof(sqlite_cmd), "SELECT 1 FROM denylist WHERE \"%s\" LIKE process || '%%' LIMIT 1", process);

  const char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char result[sizeof("1=1")];
  if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s", strerror(errno));

    return false;
  }

  return result[0] != '\0';
}

bool magisk_uid_is_manager(uid_t uid) {
  const char *const argv[] = { "magisk", "--sqlite", "select value from strings where key=\"requester\" limit 1", NULL };

  char output[128];
  if (!exec_command(output, sizeof(output), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s", strerror(errno));

    return false;
  }

  char stat_path[PATH_MAX] = "/data/user_de/0/com.topjohnwu.magisk";
  if (output[0] != '\0')
    snprintf(stat_path, sizeof(stat_path), "/data/user_de/0/%s", output + strlen("value="));

  struct stat st;
  if (stat(stat_path, &st) == -1) {
    if (errno != ENOENT) {
      LOGE("Failed to stat %s: %s", stat_path, strerror(errno));
    }

    return false;
  }

  return st.st_uid == uid;
}
