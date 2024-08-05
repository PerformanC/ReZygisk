#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include <unistd.h>
#include <linux/limits.h>
#include <sched.h>
#include <pthread.h>

#include <android/log.h>

#include "utils.h"

void switch_mount_namespace(pid_t pid) {
  char current_path[PATH_MAX];
  if (getcwd(current_path, PATH_MAX) == NULL) {
    /* TODO: Improve error messages */
    LOGE("getcwd: %s\n", strerror(errno));

    return;
  }

  /* INFO: We will NEVER achieve PATH_MAX value, but this is for ensurance. */
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/%d/ns/mnt", pid);

  FILE *mnt_ns = fopen(path, "r");
  if (mnt_ns == NULL) {
    /* TODO: Improve error messages */
    LOGE("fopen: %s\n", strerror(errno));

    return;
  }

  if (setns(fileno(mnt_ns), 0) == -1) {
    /* TODO: Improve error messages */
    LOGE("setns: %s\n", strerror(errno));

    return;
  }

  fclose(mnt_ns);

  if (chdir(current_path) == -1) {
    /* TODO: Improve error messages */
    LOGE("chdir: %s\n", strerror(errno));

    return;
  }
}

int __system_property_get(const char *, char *);

void get_property(const char *name, char *output) {
  __system_property_get(name, output);
}

void set_socket_create_context(const char *context) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/thread-self/attr/sockcreate");

  FILE *sockcreate = fopen(path, "w");
  if (sockcreate == NULL) {
    LOGE("fopen: %s\n", strerror(errno));

    return;
  }

  if (fwrite(context, 1, strlen(context), sockcreate) != strlen(context)) {
    LOGE("fwrite: %s\n", strerror(errno));

    return;
  }

  fclose(sockcreate);
}

static void get_current_attr(char *output) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/self/attr/current");

  FILE *current = fopen(path, "r");
  if (current == NULL) {
    LOGE("fopen: %s\n", strerror(errno));

    return;
  }

  if (fgets(output, PATH_MAX, current) == NULL) {
    LOGE("fgets: %s\n", strerror(errno));

    return;
  }

  fclose(current);
}

void unix_datagram_sendto(const char *path, void *buf, size_t len) {
  char current_attr[PATH_MAX];
  get_current_attr(current_attr);

  set_socket_create_context(current_attr);

  struct sockaddr_un addr;
  addr.sun_family = AF_UNIX;

  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  int socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (socket_fd == -1) {
    LOGE("socket: %s\n", strerror(errno));

    return;
  }

  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    LOGE("connect: %s\n", strerror(errno));

    return;
  }

  if (sendto(socket_fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    LOGE("sendto: %s\n", strerror(errno));

    return;
  }

  set_socket_create_context("u:r:zygote:s0");

  close(socket_fd);
}

int chcon(const char *path, const char *context) {
  char command[PATH_MAX];
  snprintf(command, PATH_MAX, "chcon %s %s", context, path);

  return system(command);
}

int unix_listener_from_path(char *path) {
  if (remove(path) == -1 && errno != ENOENT) {
    LOGE("remove: %s\n", strerror(errno));

    return -1;
  }

  int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    LOGE("socket: %s\n", strerror(errno));

    return -1;
  }

  struct sockaddr_un addr;

  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
    LOGE("bind: %s\n", strerror(errno));

    return -1;
  }

  if (listen(socket_fd, 2) == -1) {
    LOGE("listen: %s\n", strerror(errno));

    return -1;
  }

  if (chcon(path, "u:object_r:magisk_file:s0") == -1) {
    LOGE("chcon: %s\n", strerror(errno));

    return -1;
  }

  return socket_fd;
}

ssize_t send_fd(int sockfd, int fd) {
  char control_buf[CMSG_SPACE(sizeof(int))];
  memset(control_buf, 0, sizeof(control_buf));

  int cnt = 1;
  struct iovec iov = {
    .iov_base = &cnt,
    .iov_len  = sizeof(cnt)
  };

  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control_buf,
    .msg_controllen = sizeof(control_buf)
  };

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));

  memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

  ssize_t sent_bytes = sendmsg(sockfd, &msg, 0);
  if (sent_bytes == -1) {
    LOGE("Failed to send fd: %s\n", strerror(errno));

    return -1;
  }

  return sent_bytes;
}

ssize_t recv_fd(int sockfd, int *fd) {
  char control_buf[CMSG_SPACE(sizeof(int))];
  memset(control_buf, 0, sizeof(control_buf));

  int cnt = 1;
  struct iovec iov = {
    .iov_base = &cnt,
    .iov_len  = sizeof(cnt)
  };

  struct msghdr msg = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = control_buf,
    .msg_controllen = sizeof(control_buf)
  };

  ssize_t received_bytes = recvmsg(sockfd, &msg, 0);
  if (received_bytes == -1) {
    LOGE("Failed to read fd: %s\n", strerror(errno));

    return -1;
  }

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  memcpy(fd, CMSG_DATA(cmsg), sizeof(int));

  return received_bytes;
}

ssize_t write_string(int fd, const char *str) {
  size_t len = strlen(str);

  ssize_t written_bytes = write(fd, &len, sizeof(len));
  if (written_bytes != sizeof(len)) {
    LOGE("Failed to write string length: %s\n", strerror(errno));

    return -1;
  }

  written_bytes = write(fd, str, len);
  if ((size_t)written_bytes != len) {
    LOGE("Failed to write string: Not all bytes were written.\n");

    return -1;
  }

  return written_bytes;
}

ssize_t read_string(int fd, char *str, size_t len) {
  size_t str_len_buf[1];

  ssize_t read_bytes = read(fd, &str_len_buf, sizeof(str_len_buf));
  if (read_bytes != (ssize_t)sizeof(str_len_buf)) {
    LOGE("Failed to read string length: %s\n", strerror(errno));

    return -1;
  }
  
  size_t str_len = str_len_buf[0];

  if (str_len > len) {
    LOGE("Failed to read string: Buffer is too small (%zu > %zu).\n", str_len, len);

    return -1;
  }

  read_bytes = read(fd, str, str_len);
  if (read_bytes != (ssize_t)str_len) {
    LOGE("Failed to read string: Not all bytes were read (%zd != %zu).\n", read_bytes, str_len);

    return -1;
  }

  return read_bytes;
}
