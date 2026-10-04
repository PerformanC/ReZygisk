#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>

#include <android/log.h>

#include "constants.h"
#include "root_impl/common.h"

#define CONCAT_(x,y) x##y
#define CONCAT(x,y) CONCAT_(x,y)

#ifdef __LP64__
  #define LP_SELECT(a, b) b
#else
  #define LP_SELECT(a, b) a
#endif

#ifndef LOG_TAG
  #define LOG_TAG "zygiskd" LP_SELECT("32", "64")
#endif

/* INFO: do/while so each macro is a single statement, safe in brace-less ifs. */
#define LOGI(...)                                                \
  do {                                                           \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
    printf(__VA_ARGS__);                                         \
  } while (0)

#define LOGW(...)                                                \
  do {                                                           \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
    printf(__VA_ARGS__);                                         \
  } while (0)

#define LOGE(...)                                                 \
  do {                                                            \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__); \
    printf(__VA_ARGS__);                                          \
  } while (0)

#define ASSURE_SIZE_WRITE(area_name, subarea_name, sent_size, expected_size, return_type)                        \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    return_type;                                                                                                 \
  }

#define ASSURE_SIZE_READ(area_name, subarea_name, sent_size, expected_size, return_type)                         \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    return_type;                                                                                                 \
  }

#define APP_ID(uid) uid % 100000

#define IS_ISOLATED_SERVICE(uid)      \
  ((APP_ID(uid)) >= 90000)

#define write_func_def(type)              \
  ssize_t write_## type(int fd, type val)

#define read_func_def(type)               \
  ssize_t read_## type(int fd, type *val)

bool switch_mount_namespace(pid_t pid);

void get_property(const char *name, char *restrict output);

void set_socket_create_context(const char *restrict context);

void unix_datagram_sendto(const char *restrict path, const void *restrict buf, size_t len);

int chcon(const char *path, const char *restrict context);

int unix_listener_from_path(const char *path);

ssize_t write_fd(int fd, int sendfd);
int read_fd(int fd);

write_func_def(size_t);
read_func_def(size_t);

write_func_def(uint32_t);
read_func_def(uint32_t);

write_func_def(uint8_t);
read_func_def(uint8_t);

ssize_t write_string(int fd, const char *restrict str);

ssize_t read_string(int fd, char *restrict buf, size_t buf_size);

bool exec_command(char *restrict buf, size_t len, const char *restrict file, const char *const argv[]);

/* WARNING: Dynamic memory based. Returns the whole stdout, NUL-terminated, or NULL on failure,
             non-zero exit or output larger than max_len. */
char *exec_command_output(const char *restrict file, const char *const argv[], size_t max_len);

/* INFO: Identifies a file version, used to invalidate caches when the file changes. */
struct file_stamp {
  bool exists;
  ino_t ino;
  off_t size;
  struct timespec mtime;
};

void file_stamp_get(const char *restrict path, struct file_stamp *restrict stamp);

bool file_stamp_equal(const struct file_stamp *a, const struct file_stamp *b);

bool check_unix_socket(int fd, bool block);

int non_blocking_execv(const char *restrict file, char *const argv[]);

void stringify_root_impl_name(struct root_impl impl, char *restrict output);

int save_mns_fd(int pid, enum MountNamespaceState mns_state, struct root_impl impl);

#endif /* UTILS_H */
