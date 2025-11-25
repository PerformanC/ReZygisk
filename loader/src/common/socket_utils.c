#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>

#include <unistd.h>

#include "logging.h"

#include "socket_utils.h"

ssize_t write_loop(int fd, const void *buf, size_t count) {
  ssize_t written = 0;
  while (written < (ssize_t)count) {
    ssize_t ret = write(fd, (const char *)buf + written, count - written);
    if (ret == -1) {
      if (errno == EINTR || errno == EAGAIN) continue;

      PLOGE("write");
    }

    if (ret == 0) {
      LOGE("write: 0 bytes written");

      return -1;
    }

    written += ret;
  }

  return written;
}

ssize_t read_loop(int fd, void *buf, size_t count) {
  ssize_t read_bytes = 0;
  while (read_bytes < (ssize_t)count) {
    ssize_t ret = read(fd, buf + read_bytes, count - read_bytes);
    if (ret == -1) {
      if (errno == EINTR || errno == EAGAIN) continue;

      PLOGE("read");
    }

    if (ret == 0) {
      LOGE("read: 0 bytes read");

      return -1;
    }

    read_bytes += ret;
  }

  return read_bytes;
}

/* TODO: Standardize how to log errors */
int read_fd(int fd) {
  char cmsgbuf[CMSG_SPACE(sizeof(int))];

  int cnt = 1;
  struct iovec iov = {
    .iov_base = &cnt,
    .iov_len = sizeof(cnt)
  };

  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = cmsgbuf,
    .msg_controllen = sizeof(cmsgbuf)
  };

  ssize_t ret = TEMP_FAILURE_RETRY(recvmsg(fd, &msg, MSG_WAITALL));
  if (ret == -1) {
    PLOGE("recvmsg");

    return -1;
  }

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg == NULL) {
    PLOGE("CMSG_FIRSTHDR");

    return -1;
  }

  int sendfd;
  memcpy(&sendfd, CMSG_DATA(cmsg), sizeof(int));

  return sendfd;
}

ssize_t write_string(int fd, const char *str) {
  size_t str_len = strlen(str);
  ssize_t write_bytes = write_loop(fd, &str_len, sizeof(size_t));
  if (write_bytes != (ssize_t)sizeof(size_t)) {
    LOGE("Failed to write string length: Not all bytes were written (%zd != %zu).\n", write_bytes, sizeof(size_t));

    return -1;
  }

  write_bytes = write_loop(fd, str, str_len);
  if (write_bytes != (ssize_t)str_len) {
    LOGE("Failed to write string: Promised bytes doesn't exist (%zd != %zu).\n", write_bytes, str_len);

    return -1;
  }

  return write_bytes;
}

char *read_string(int fd) {
  size_t str_len = 0;
  ssize_t read_bytes = read_loop(fd, &str_len, sizeof(size_t));
  if (read_bytes != (ssize_t)sizeof(size_t)) {
    LOGE("Failed to read string length: Not all bytes were read (%zd != %zu).\n", read_bytes, sizeof(size_t));

    return NULL;
  }

  char *buf = malloc(str_len + 1);
  if (buf == NULL) {
    PLOGE("allocate memory for string");

    return NULL;
  }

  read_bytes = read_loop(fd, buf, str_len);
  if (read_bytes != (ssize_t)str_len) {
    LOGE("Failed to read string: Promised bytes doesn't exist (%zd != %zu).\n", read_bytes, str_len);

    free(buf);

    return NULL;
  }

  if (str_len > 0) buf[str_len] = '\0';

  return buf;
}

#define write_func(type)                       \
  ssize_t write_## type(int fd, type val) {    \
    return write_loop(fd, &val, sizeof(type)); \
  }

#define read_func(type)                      \
  ssize_t read_## type(int fd, type *val) {  \
    return read_loop(fd, val, sizeof(type)); \
  }

write_func(uint8_t)
read_func(uint8_t)

write_func(uint32_t)
read_func(uint32_t)

write_func(size_t)
read_func(size_t)
