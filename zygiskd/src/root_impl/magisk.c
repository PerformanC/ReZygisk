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
#include "common.h"

#include "magisk.h"

const char *supported_variants[] = {
  "kitsune"
};

const char *magisk_managers[] = {
  "com.topjohnwu.magisk",
  "io.github.huskydg.magisk"
};

#define SBIN_MAGISK lp_select("/sbin/magisk32", "/sbin/magisk64")
#define BITLESS_SBIN_MAGISK "/sbin/magisk"
#define DEBUG_RAMDISK_MAGISK lp_select("/debug_ramdisk/magisk32", "/debug_ramdisk/magisk64")
#define BITLESS_DEBUG_RAMDISK_MAGISK "/debug_ramdisk/magisk"

static enum magisk_variants variant = MOfficial;
/* INFO: Longest path */
static char path_to_magisk[sizeof(DEBUG_RAMDISK_MAGISK)] = { 0 };
bool is_using_sulist = false;

void magisk_get_existence(struct root_impl_state *state) {
  const char *magisk_files[] = {
    SBIN_MAGISK,
    BITLESS_SBIN_MAGISK,
    DEBUG_RAMDISK_MAGISK,
    BITLESS_DEBUG_RAMDISK_MAGISK
  };

  for (size_t i = 0; i < sizeof(magisk_files) / sizeof(magisk_files[0]); i++) {
    if (access(magisk_files[i], F_OK) != 0) {
      if (errno != ENOENT) {
        LOGE("Failed to access Magisk binary: %s\n", strerror(errno));
      }
      errno = 0;

      continue;
    }

    strcpy(path_to_magisk, magisk_files[i]);

    break;
  }

  if (path_to_magisk[0] == '\0') {
    state->state = Inexistent;

    return;
  }

  char *argv[4] = { "magisk", "-v", NULL, NULL };

  char magisk_info[128];
  if (!exec_command(magisk_info, sizeof(magisk_info), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    state->state = Abnormal;

    return;
  }

  state->variant = (uint8_t)MOfficial;

  for (unsigned long i = 0; i < sizeof(supported_variants) / sizeof(supported_variants[0]); i++) {
    if (strstr(magisk_info, supported_variants[i])) {
      variant = (enum magisk_variants)(i + 1);
      state->variant = (uint8_t)variant;

      break;
    }
  }

  argv[1] = "-V";

  char magisk_version[32];
  if (!exec_command(magisk_version, sizeof(magisk_version), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    state->state = Abnormal;

    return;
  }

  /* INFO: Magisk Kitsune has a feature called SuList, which is a whitelist of
             which processes are allowed to see root. Although only Kitsune has
             this option, there are Kitsune forks without "-kitsune" suffix, so
             to avoid excluding them from taking advantage of that feature, we
             will not check the variant.
  */
  argv[1] = "--sqlite";
  argv[2] = "select value from settings where key = 'sulist' limit 1";

  char sulist_enabled[32];
  if (!exec_command(sulist_enabled, sizeof(sulist_enabled), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    state->state = Abnormal;

    return;
  }

  is_using_sulist = strcmp(sulist_enabled, "value=1") == 0;

  if (atoi(magisk_version) >= MIN_MAGISK_VERSION) state->state = Supported;
  else state->state = TooOld;
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

bool magisk_uid_should_umount(const char *const process) {
  /* INFO: PROCESS_NAME_MAX_LEN already has a +1 for NULL */
  char sqlite_cmd[51 + PROCESS_NAME_MAX_LEN];
  if (is_using_sulist)
    snprintf(sqlite_cmd, sizeof(sqlite_cmd), "SELECT 1 FROM sulist WHERE process=\"%s\" LIMIT 1", process);
  else /* INFO: Find if process string starts with any data in "process" column */
    snprintf(sqlite_cmd, sizeof(sqlite_cmd), "SELECT 1 FROM denylist WHERE \"%s\" LIKE process || '%%' LIMIT 1", process);

  char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char result[sizeof("1=1")];
  if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s\n", strerror(errno));
    errno = 0;

    return false;
  }

  return is_using_sulist ? result[0] == '\0' : result[0] != '\0';
}

bool magisk_uid_is_manager(uid_t uid) {
  char *const argv[] = { "magisk", "--sqlite", "select value from strings where key=\"requester\" limit 1", NULL };

  char output[128];
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
