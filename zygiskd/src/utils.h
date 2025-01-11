#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>

#include "constants.h"
#include "root_impl/common.h"

#define CONCAT_(x,y) x##y
#define CONCAT(x,y) CONCAT_(x,y)

#define LOG_TAG lp_select("zygiskd32", "zygiskd64")

#define LOGI(...)                                              \
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
  printf(__VA_ARGS__);

#define LOGW(...)                                                \
  __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__);   \
  printf(__VA_ARGS__);

#define LOGE(...)                                                \
  __android_log_print(ANDROID_LOG_ERROR , LOG_TAG, __VA_ARGS__); \
  printf(__VA_ARGS__);

#define ASSURE_SIZE_WRITE(area_name, subarea_name, sent_size, expected_size)                                     \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    return;                                                                                                      \
  }

#define ASSURE_SIZE_READ(area_name, subarea_name, sent_size, expected_size)                                      \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    return;                                                                                                      \
  }

#define ASSURE_SIZE_WRITE_BREAK(area_name, subarea_name, sent_size, expected_size)                               \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    break;                                                                                                       \
  }

#define ASSURE_SIZE_READ_BREAK(area_name, subarea_name, sent_size, expected_size)                                \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    break;                                                                                                       \
  }

#define ASSURE_SIZE_WRITE_WR(area_name, subarea_name, sent_size, expected_size)                                  \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to sent " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    return -1;                                                                                                   \
  }

#define ASSURE_SIZE_READ_WR(area_name, subarea_name, sent_size, expected_size)                                   \
  if (sent_size != (ssize_t)(expected_size)) {                                                                   \
    LOGE("Failed to read " subarea_name " in " area_name ": Expected %zu, got %zd\n", expected_size, sent_size); \
                                                                                                                 \
    return -1;                                                                                                   \
  }

#define write_func_def(type)               \
  ssize_t write_## type(int fd, type val)

#define read_func_def(type)               \
  ssize_t read_## type(int fd, type *val)

bool switch_mount_namespace(pid_t pid);

void get_property(const char *name, char *restrict output);

void set_socket_create_context(const char *restrict context);

void unix_datagram_sendto(const char *restrict path, void *restrict buf, size_t len);

int chcon(const char *path, const char *restrict context);

int unix_listener_from_path(char *path);

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

bool exec_command(char *restrict buf, size_t len, const char *restrict file, char *const argv[]);

bool check_unix_socket(int fd, bool block);

int non_blocking_execv(const char *restrict file, char *const argv[]);

void stringify_root_impl_name(struct root_impl impl, char *restrict output);

int save_mns_fd(int pid, enum MountNamespaceState mns_state, struct root_impl impl);

#endif /* UTILS_H */
