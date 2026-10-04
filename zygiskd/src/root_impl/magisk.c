#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include "magisk.h"

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

    strcpy(path_to_magisk, magisk_files[i]);

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

/* INFO: Every app launch asks for its process flags. Answering them used to spawn
           `magisk --sqlite` up to three times per launch. Instead, the relevant
           tables are loaded once and kept in memory until magisk.db changes. */
#define MAGISK_DB "/data/adb/magisk.db"
#define MAGISK_DB_WAL MAGISK_DB "-wal"
#define MAGISK_DB_JOURNAL MAGISK_DB "-journal"
#define MAGISK_SQLITE_MAX_OUTPUT (4 * 1024 * 1024)

static struct {
  bool valid;
  struct file_stamp db;
  struct file_stamp wal;
  struct file_stamp journal;
  char manager_pkg[NAME_MAX + 1];
  uid_t *root_uids;
  size_t root_uids_len;
  /* INFO: denylist "process" column values, matched with SQLite LIKE semantics */
  char **denylist;
  size_t denylist_len;
} magisk_cache = { 0 };

static void magisk_cache_free(void) {
  free(magisk_cache.root_uids);

  for (size_t i = 0; i < magisk_cache.denylist_len; i++) {
    free(magisk_cache.denylist[i]);
  }
  free(magisk_cache.denylist);

  memset(&magisk_cache, 0, sizeof(magisk_cache));
}

static char *magisk_sqlite(const char *query) {
  const char *const argv[] = { "magisk", "--sqlite", query, NULL };

  return exec_command_output((const char *)path_to_magisk, argv, MAGISK_SQLITE_MAX_OUTPUT);
}

/* INFO: Calls cb with the value of each "<column>=<value>" output line. */
static bool magisk_for_each_value(char *output, const char *column, bool (*cb)(const char *value)) {
  size_t column_len = strlen(column);

  char *save_ptr = NULL;
  for (char *line = strtok_r(output, "\n", &save_ptr); line; line = strtok_r(NULL, "\n", &save_ptr)) {
    if (strncmp(line, column, column_len) != 0 || line[column_len] != '=') continue;

    if (!cb(line + column_len + 1)) return false;
  }

  return true;
}

static bool magisk_cache_add_root_uid(const char *value) {
  uid_t *tmp = realloc(magisk_cache.root_uids, (magisk_cache.root_uids_len + 1) * sizeof(uid_t));
  if (!tmp) return false;

  magisk_cache.root_uids = tmp;
  magisk_cache.root_uids[magisk_cache.root_uids_len++] = (uid_t)strtoul(value, NULL, 10);

  return true;
}

static bool magisk_cache_add_denylist(const char *value) {
  char **tmp = realloc(magisk_cache.denylist, (magisk_cache.denylist_len + 1) * sizeof(char *));
  if (!tmp) return false;

  magisk_cache.denylist = tmp;

  char *entry = strdup(value);
  if (!entry) return false;

  magisk_cache.denylist[magisk_cache.denylist_len++] = entry;

  return true;
}

/* INFO: Returns true when the cache can be used. On any failure the callers fall
           back to querying magisk directly, exactly like before. */
static bool magisk_cache_refresh(void) {
  struct file_stamp db, wal, journal;
  file_stamp_get(MAGISK_DB, &db);
  file_stamp_get(MAGISK_DB_WAL, &wal);
  file_stamp_get(MAGISK_DB_JOURNAL, &journal);

  if (!db.exists) {
    if (magisk_cache.valid) magisk_cache_free();

    return false;
  }

  if (magisk_cache.valid && file_stamp_equal(&db, &magisk_cache.db) &&
      file_stamp_equal(&wal, &magisk_cache.wal) && file_stamp_equal(&journal, &magisk_cache.journal))
    return true;

  magisk_cache_free();

  /* INFO: The stamps are taken BEFORE reading, so a write racing with us only
             causes one extra reload on the next call, never stale data. */
  char *output = magisk_sqlite("select value from strings where key=\"requester\" limit 1");
  if (!output) return false;

  snprintf(magisk_cache.manager_pkg, sizeof(magisk_cache.manager_pkg), "%s", "com.topjohnwu.magisk");
  if (strncmp(output, "value=", strlen("value=")) == 0) {
    char *end = strchr(output, '\n');
    if (end) *end = '\0';

    snprintf(magisk_cache.manager_pkg, sizeof(magisk_cache.manager_pkg), "%s", output + strlen("value="));
  }
  free(output);

  output = magisk_sqlite("select uid from policies where policy=2");
  if (!output) goto fail;

  bool ok = magisk_for_each_value(output, "uid", magisk_cache_add_root_uid);
  free(output);
  if (!ok) goto fail;

  output = magisk_sqlite("select process from denylist");
  if (!output) goto fail;

  ok = magisk_for_each_value(output, "process", magisk_cache_add_denylist);
  free(output);
  if (!ok) goto fail;

  magisk_cache.db = db;
  magisk_cache.wal = wal;
  magisk_cache.journal = journal;
  magisk_cache.valid = true;

  LOGI("Magisk cache loaded: %zu root uids, %zu denylist entries", magisk_cache.root_uids_len, magisk_cache.denylist_len);

  return true;

  fail:
    LOGE("Failed to load Magisk database cache, falling back to direct queries");

    magisk_cache_free();

    return false;
}

/* INFO: SQLite LIKE: ASCII case-insensitive, '%' matches any sequence, '_' any char. */
static bool sqlite_like(const char *pattern, const char *str) {
  const char *star_pattern = NULL;
  const char *star_str = NULL;

  while (*str) {
    if (*pattern == '%') {
      while (*pattern == '%') pattern++;
      if (*pattern == '\0') return true;

      star_pattern = pattern;
      star_str = str;

      continue;
    }

    if (*pattern != '\0' && (*pattern == '_' || tolower((unsigned char)*pattern) == tolower((unsigned char)*str))) {
      pattern++;
      str++;

      continue;
    }

    if (star_pattern) {
      pattern = star_pattern;
      str = ++star_str;

      continue;
    }

    return false;
  }

  while (*pattern == '%') pattern++;

  return *pattern == '\0';
}

/* INFO: Mirrors `"<process>" LIKE process || '%'`: the denylist entry is a prefix pattern. */
static bool denylist_entry_matches(const char *entry, const char *process) {
  size_t entry_len = strlen(entry);

  char pattern[PROCESS_NAME_MAX_LEN + 2];
  if (entry_len + 2 > sizeof(pattern)) return false;

  memcpy(pattern, entry, entry_len);
  pattern[entry_len] = '%';
  pattern[entry_len + 1] = '\0';

  return sqlite_like(pattern, process);
}

bool magisk_uid_granted_root(uid_t uid) {
  if (magisk_cache_refresh()) {
    for (size_t i = 0; i < magisk_cache.root_uids_len; i++) {
      if (magisk_cache.root_uids[i] == uid) return true;
    }

    return false;
  }

  char sqlite_cmd[256];
  snprintf(sqlite_cmd, sizeof(sqlite_cmd), "select 1 from policies where uid=%u and policy=2 limit 1", uid);

  const char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char result[32];
  if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s", strerror(errno));

    return false;
  }

  return result[0] != '\0';
}

bool magisk_uid_should_umount(const char *const process) {
  if (magisk_cache_refresh()) {
    for (size_t i = 0; i < magisk_cache.denylist_len; i++) {
      if (denylist_entry_matches(magisk_cache.denylist[i], process)) return true;
    }

    return false;
  }

  /* INFO: Escape double quotes, so a crafted process name can never break out of
             the SQL string literal. */
  char escaped[PROCESS_NAME_MAX_LEN * 2];
  size_t j = 0;
  for (size_t i = 0; process[i] != '\0' && j + 2 < sizeof(escaped); i++) {
    if (process[i] == '"') escaped[j++] = '"';
    escaped[j++] = process[i];
  }
  escaped[j] = '\0';

  char sqlite_cmd[64 + sizeof(escaped)];
  /* INFO: Find if process string starts with any data in "process" column */
  snprintf(sqlite_cmd, sizeof(sqlite_cmd), "SELECT 1 FROM denylist WHERE \"%s\" LIKE process || '%%' LIMIT 1", escaped);

  const char *const argv[] = { "magisk", "--sqlite", sqlite_cmd, NULL };

  char result[sizeof("1=1")];
  if (!exec_command(result, sizeof(result), (const char *)path_to_magisk, argv)) {
    LOGE("Failed to execute magisk binary: %s", strerror(errno));

    return false;
  }

  return result[0] != '\0';
}

bool magisk_uid_is_manager(uid_t uid) {
  char pkg[NAME_MAX + 1] = "com.topjohnwu.magisk";

  if (magisk_cache_refresh()) {
    snprintf(pkg, sizeof(pkg), "%s", magisk_cache.manager_pkg);
  } else {
    const char *const argv[] = { "magisk", "--sqlite", "select value from strings where key=\"requester\" limit 1", NULL };

    char output[128];
    if (!exec_command(output, sizeof(output), (const char *)path_to_magisk, argv)) {
      LOGE("Failed to execute magisk binary: %s", strerror(errno));

      return false;
    }

    if (output[0] != '\0')
      snprintf(pkg, sizeof(pkg), "%s", output + strlen("value="));
  }

  uid_t manager_uid = uid_from_pkg(pkg);
  if (!manager_uid) {
    LOGE("Failed to retrieve uid for %s", pkg);

    return false;
  }

  return manager_uid == APP_ID(uid);
}
