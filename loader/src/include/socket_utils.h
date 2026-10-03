#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <sys/types.h>

/*
  INFO: Abstract namespace: no path, so the socket stays out of /proc/net/unix,
        which any app can read. The name is the basename of the path passed in,
        so both ends still agree without a new shared constant.
*/
static inline socklen_t abstract_addr(struct sockaddr_un *addr, const char *path) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;

  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;

  size_t len = strlen(name);
  if (len > sizeof(addr->sun_path) - 2) len = sizeof(addr->sun_path) - 2;
  memcpy(addr->sun_path + 1, name, len);

  return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len);
}

ssize_t write_loop(int fd, const void *buf, size_t count);

ssize_t read_loop_offset(int fd, void *buf, size_t len, off_t offset);

ssize_t read_loop(int fd, void *buf, size_t len);

ssize_t write_fd(int fd, int sendfd);

int read_fd(int fd);

ssize_t write_string(int fd, const char *str);

char *read_string(int fd);

#define write_func_def(type)              \
  ssize_t write_## type(int fd, type val)

#define read_func_def(type)               \
  ssize_t read_## type(int fd, type *val)

write_func_def(uint8_t);
read_func_def(uint8_t);

write_func_def(uint32_t);
read_func_def(uint32_t);

write_func_def(size_t);
read_func_def(size_t);

#endif /* SOCKET_UTILS_H */
