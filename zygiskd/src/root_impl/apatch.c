#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "../constants.h"
#include "../utils.h"
#include "common.h"

#include "apatch.h"

void apatch_get_existence(struct root_impl_state *state) {
  struct stat s;
  if (stat("/data/adb/apd", &s) != 0) {
    if (errno != ENOENT) {
      LOGE("Failed to stat APatch apd binary: %s\n", strerror(errno));
    }
    errno = 0;

    state->state = Inexistent;

    return;
  }

  char *PATH = getenv("PATH");
  if (PATH == NULL) {
    LOGE("Failed to get PATH environment variable: %s\n", strerror(errno));
    errno = 0;

    state->state = Inexistent;

    return;
  }

  if (strstr(PATH, "/data/adb/ap/bin") == NULL) {
    LOGE("APatch's APD binary is not in PATH\n");

    state->state = Inexistent;

    return;
  }

  char apatch_version[32];
  char *const argv[] = { "apd", "-V", NULL };

  if (!exec_command(apatch_version, sizeof(apatch_version), "/data/adb/apd", argv)) {
    LOGE("Failed to execute apd binary: %s\n", strerror(errno));
    errno = 0;

    state->state = Inexistent;

    return;
  }

  int version = atoi(apatch_version + strlen("apd "));

  if (version == 0) state->state = Abnormal;
  else if (version >= MIN_APATCH_VERSION && version <= 999999) state->state = Supported;
  else if (version >= 1 && version <= MIN_APATCH_VERSION - 1) state->state = TooOld;
  else state->state = Abnormal;
}

struct package_config {
  uid_t uid;
  bool root_granted;
  bool umount_needed;
};

struct packages_config {
  struct package_config *configs;
  size_t size;
};

/* WARNING: Dynamic memory based */
bool _apatch_get_package_config(struct packages_config *restrict config) {
  config->configs = NULL;
  config->size = 0;

  FILE *fp = fopen("/data/adb/ap/package_config", "r");
  if (fp == NULL) {
    LOGE("Failed to open APatch's package_config: %s\n", strerror(errno));

    return false;
  }

  char line[1048];
  /* INFO: Skip the CSV header */
  if (fgets(line, sizeof(line), fp) == NULL) {
    LOGE("Failed to read APatch's package_config header: %s\n", strerror(errno));

    fclose(fp);

    return false;
  }

  while (fgets(line, sizeof(line), fp) != NULL) { 
    config->configs = realloc(config->configs, (config->size + 1) * sizeof(struct package_config));
    if (config->configs == NULL) {
      LOGE("Failed to realloc APatch config struct: %s\n", strerror(errno));

      fclose(fp);

      return false;
    }

    strtok(line, ",");

    char *exclude_str = strtok(NULL, ",");
    if (exclude_str == NULL) continue;

    char *allow_str = strtok(NULL, ",");
    if (allow_str == NULL) continue;

    char *uid_str = strtok(NULL, ",");
    if (uid_str == NULL) continue;

    config->configs[config->size].uid = atoi(uid_str);
    config->configs[config->size].root_granted = strcmp(allow_str, "1") == 0;
    config->configs[config->size].umount_needed = strcmp(exclude_str, "1") == 0;

    config->size++;
  }

  fclose(fp);

  return true;
}

void _apatch_free_package_config(struct packages_config *restrict config) {
  free(config->configs);
}

bool apatch_uid_granted_root(uid_t uid) {
  struct packages_config config;
  if (!_apatch_get_package_config(&config)) {
    _apatch_free_package_config(&config);

    return false;
  }

  for (size_t i = 0; i < config.size; i++) {
    if (config.configs[i].uid == uid) {
      _apatch_free_package_config(&config);

      return config.configs[i].root_granted;
    }
  }

  return false;
}

bool apatch_uid_should_umount(uid_t uid) {
  struct packages_config config;
  if (!_apatch_get_package_config(&config)) {
    _apatch_free_package_config(&config);

    return false;
  }

  for (size_t i = 0; i < config.size; i++) {
    if (config.configs[i].uid == uid) {
      _apatch_free_package_config(&config);

      return config.configs[i].umount_needed;
    }
  }

  _apatch_free_package_config(&config);

  return false;
}

bool apatch_uid_is_manager(uid_t uid) {
  struct stat s;
  if (stat("/data/user_de/0/me.bmax.apatch", &s) == -1) {
    if (errno != ENOENT) {
      LOGE("Failed to stat APatch manager data directory: %s\n", strerror(errno));
    }
    errno = 0;

    return false;
  }

  return s.st_uid == uid;
}
