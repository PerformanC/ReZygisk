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

  const char *PATH = getenv("PATH");
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
  char *process;
  uid_t uid;
  bool root_granted;
  bool umount_needed;
};

struct packages_config {
  struct package_config *configs;
  size_t size;
};

void _apatch_free_package_config(struct packages_config *restrict config) {
  for (size_t i = 0; i < config->size; i++) {
    free(config->configs[i].process);
  }

  free(config->configs);
}

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
    struct package_config *tmp_configs = realloc(config->configs, (config->size + 1) * sizeof(struct package_config));
    if (tmp_configs == NULL) {
      LOGE("Failed to realloc APatch config struct: %s\n", strerror(errno));

      _apatch_free_package_config(config);
      fclose(fp);

      return false;
    }
    config->configs = tmp_configs;

    char *save_ptr = NULL;
    const char *process_str = strtok_r(line, ",", &save_ptr);
    if (process_str == NULL) continue;

    const char *exclude_str = strtok_r(NULL, ",", &save_ptr);
    if (exclude_str == NULL) continue;

    const char *allow_str = strtok_r(NULL, ",", &save_ptr);
    if (allow_str == NULL) continue;

    const char *uid_str = strtok_r(NULL, ",", &save_ptr);
    if (uid_str == NULL) continue;

    config->configs[config->size].process = strdup(process_str);
    if (config->configs[config->size].process == NULL) {
      LOGE("Failed to strdup for the process `%s`: %s\n", process_str, strerror(errno));

      _apatch_free_package_config(config);
      fclose(fp);

      return false;
    }
    config->configs[config->size].uid = (uid_t)atoi(uid_str);
    config->configs[config->size].root_granted = strcmp(allow_str, "1") == 0;
    config->configs[config->size].umount_needed = strcmp(exclude_str, "1") == 0;

    config->size++;
  }

  fclose(fp);

  return true;
}

bool apatch_uid_granted_root(uid_t uid) {
  struct packages_config config;
  if (!_apatch_get_package_config(&config)) return false;

  for (size_t i = 0; i < config.size; i++) {
    if (config.configs[i].uid != uid) continue;

    /* INFO: This allow us to copy the information to avoid use-after-free */
    bool root_granted = config.configs[i].root_granted;

    _apatch_free_package_config(&config);

    return root_granted;
  }

  _apatch_free_package_config(&config);

  return false;
}

bool apatch_uid_should_umount(uid_t uid, const char *const process) {
  struct packages_config config;
  if (!_apatch_get_package_config(&config)) return false;

  for (size_t i = 0; i < config.size; i++) {
    if (config.configs[i].uid != uid) continue;

    /* INFO: This allow us to copy the information to avoid use-after-free */
    bool umount_needed = config.configs[i].umount_needed;

    _apatch_free_package_config(&config);

    return umount_needed;
  }

  /* INFO: Isolated services have different UIDs than the main app, and
             while libzygisk.so has code to send the UID of the app related
             to the isolated service, we add this so that in case it fails,
             this should avoid it pass through as Mounted.
  */
  if (IS_ISOLATED_SERVICE(uid)) {
    size_t targeted_process_length = strlen(process);

    for (size_t i = 0; i < config.size; i++) {
      size_t config_process_length = strlen(config.configs[i].process);
      size_t smallest_process_length = targeted_process_length < config_process_length ? targeted_process_length : config_process_length;

      if (strncmp(config.configs[i].process, process, smallest_process_length) != 0) continue;

      /* INFO: This allow us to copy the information to avoid use-after-free */
      bool umount_needed = config.configs[i].umount_needed;

      _apatch_free_package_config(&config);

      return umount_needed;
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
