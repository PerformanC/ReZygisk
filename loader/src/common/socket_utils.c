#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/socket.h>

#include "logging.h"

#include "socket_utils.h"

ssize_t write_loop(int fd, const void *buf, size_t count) {
  ssize_t written = 0;
  while (written < (ssize_t)count) {
    ssize_t ret = write(fd, (const char *)buf + written, count - written);
    if (ret == -1) {
      if (errno == EAGAIN) {
        LOGW("Got EAGAIN while writing to fd %d, retrying...\n", fd);

        /* INFO: Sleep for 1ms*/
        usleep(1000);

        continue;
      }

      if (errno == EINTR) continue;

      PLOGE("write");

      return -1;
    }

    if (ret == 0) {
      LOGE("write: 0 bytes written");

      return -1;
    }

    written += ret;
  }

  return written;
}

ssize_t read_loop_offset(int fd, void *buf, size_t count, off_t off) {
  if (off < 0) {
    LOGE("read_loop_offset: negative offset: %lld", (long long)off);

    return -1;
  }

  ssize_t read_bytes = 0;
  while (read_bytes < (ssize_t)count) {
    size_t remaining = count - (size_t)read_bytes;
    char *dst = (char *)buf + read_bytes;
    ssize_t ret = pread(fd, dst, remaining, off + read_bytes);
    if (ret == -1) {
      if (errno == EAGAIN) {
        LOGW("Got EAGAIN while writing to fd %d, retrying...\n", fd);

        /* INFO: Sleep for 1ms*/
        usleep(1000);

        continue;
      }

      if (errno == EINTR) continue;

      PLOGE("read");

      return -1;
    }

    if (ret == 0) {
      LOGE("read: 0 bytes read");

      return -1;
    }

    read_bytes += ret;
  }

  return read_bytes;
}

ssize_t read_loop(int fd, void *buf, size_t count) {
  ssize_t read_bytes = 0;
  while (read_bytes < (ssize_t)count) {
    size_t remaining = count - (size_t)read_bytes;
    char *dst = (char *)buf + read_bytes;
    ssize_t ret = read(fd, dst, remaining);
    if (ret == -1) {
      if (errno == EINTR || errno == EAGAIN) continue;

      PLOGE("read");

      return -1;
    }

    if (ret == 0) {
      LOGE("read: 0 bytes read");

      return -1;
    }

    read_bytes += ret;
  }

  return read_bytes;
}

ssize_t write_fd(int fd, int sendfd) {
  char cmsgbuf[CMSG_SPACE(sizeof(int))];
  char buf[1] = { 0 };

  struct iovec iov = {
    .iov_base = buf,
    .iov_len = 1
  };

  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = cmsgbuf,
    .msg_controllen = sizeof(cmsgbuf)
  };

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;

  memcpy(CMSG_DATA(cmsg), &sendfd, sizeof(int));

  ssize_t ret = sendmsg(fd, &msg, 0);
  if (ret == -1) {
    LOGE("sendmsg: %s\n", strerror(errno));

    return -1;
  }

  return ret;
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

  struct cmsghdr *cmsg;
  int sendfd = -1;

  for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len < CMSG_LEN(sizeof(int))) continue;

    memcpy(&sendfd, CMSG_DATA(cmsg), sizeof(int));

    break;
  }

  if (sendfd == -1) {
    LOGE("Failed to receive fd: No valid fd found in ancillary data.");

    return -1;
  }

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

  buf[str_len] = '\0';

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
