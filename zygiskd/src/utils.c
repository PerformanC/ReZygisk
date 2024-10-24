#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <errno.h>

#include <unistd.h>
#include <linux/limits.h>
#include <sched.h>
#include <pthread.h>

#include <android/log.h>

#include "utils.h"
#include "root_impl/common.h"

bool switch_mount_namespace(pid_t pid) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/ns/mnt", pid);

  int nsfd = open(path, O_RDONLY | O_CLOEXEC);
  if (nsfd == -1) {
    LOGE("Failed to open nsfd: %s\n", strerror(errno));

    return false;
  }

  if (setns(nsfd, CLONE_NEWNS) == -1) {
    LOGE("Failed to setns: %s\n", strerror(errno));

    close(nsfd);

    return false;
  }

  close(nsfd);

  return true;
}

int __system_property_get(const char *, char *);

void get_property(const char *restrict name, char *restrict output) {
  __system_property_get(name, output);
}

void set_socket_create_context(const char *restrict context) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/thread-self/attr/sockcreate");

  FILE *sockcreate = fopen(path, "w");
  if (sockcreate == NULL) {
    LOGE("Failed to open /proc/thread-self/attr/sockcreate: %s Now trying to via gettid().\n", strerror(errno));

    goto fail;
  }

  if (fwrite(context, 1, strlen(context), sockcreate) != strlen(context)) {
    LOGE("Failed to write to /proc/thread-self/attr/sockcreate: %s Now trying to via gettid().\n", strerror(errno));

    fclose(sockcreate);

    goto fail;
  }

  fclose(sockcreate);

  return;

  fail:
    snprintf(path, PATH_MAX, "/proc/self/task/%d/attr/sockcreate", gettid());

    sockcreate = fopen(path, "w");
    if (sockcreate == NULL) {
      LOGE("Failed to open %s: %s\n", path, strerror(errno));

      return;
    }

    if (fwrite(context, 1, strlen(context), sockcreate) != strlen(context)) {
      LOGE("Failed to write to %s: %s\n", path, strerror(errno));

      return;
    }

    fclose(sockcreate);
}

static void get_current_attr(char *restrict output, size_t size) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/self/attr/current");

  FILE *current = fopen(path, "r");
  if (current == NULL) {
    LOGE("fopen: %s\n", strerror(errno));

    return;
  }

  if (fread(output, 1, size, current) == 0) {
    LOGE("fread: %s\n", strerror(errno));

    return;
  }

  fclose(current);
}

void unix_datagram_sendto(const char *restrict path, void *restrict buf, size_t len) {
  char current_attr[PATH_MAX];
  get_current_attr(current_attr, sizeof(current_attr));

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

int chcon(const char *restrict path, const char *context) {
  char command[PATH_MAX];
  snprintf(command, PATH_MAX, "chcon %s %s", context, path);

  return system(command);
}

int unix_listener_from_path(char *restrict path) {
  if (remove(path) == -1 && errno != ENOENT) {
    LOGE("remove: %s\n", strerror(errno));

    return -1;
  }

  int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    LOGE("socket: %s\n", strerror(errno));

    return -1;
  }

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX
  };
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

int read_fd(int fd) {
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

  ssize_t ret = recvmsg(fd, &msg, 0);
  if (ret == -1) {
    LOGE("recvmsg: %s\n", strerror(errno));

    return -1;
  }

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg == NULL) {
    LOGE("CMSG_FIRSTHDR: %s\n", strerror(errno));

    return -1;
  }

  int sendfd;
  memcpy(&sendfd, CMSG_DATA(cmsg), sizeof(int));

  return sendfd;
}

#define write_func(type)                    \
  ssize_t write_## type(int fd, type val) { \
    return write(fd, &val, sizeof(type));   \
  }

#define read_func(type)                     \
  ssize_t read_## type(int fd, type *val) { \
    return read(fd, val, sizeof(type));     \
  }

write_func(int)
read_func(int)

write_func(size_t)
read_func(size_t)

write_func(uint32_t)
read_func(uint32_t)

write_func(uint8_t)
read_func(uint8_t)

ssize_t write_string(int fd, const char *restrict str) {
  size_t len[1];
  len[0] = strlen(str);

  ssize_t written_bytes = write(fd, &len, sizeof(size_t));
  if (written_bytes != sizeof(size_t)) {
    LOGE("Failed to write string length: Not all bytes were written (%zd != %zu).\n", written_bytes, sizeof(size_t));

    return -1;
  }

  written_bytes = write(fd, str, len[0]);
  if ((size_t)written_bytes != len[0]) {
    LOGE("Failed to write string: Not all bytes were written.\n");

    return -1;
  }

  return written_bytes;
}

ssize_t read_string(int fd, char *restrict str, size_t len) {
  size_t str_len_buf[1];

  ssize_t read_bytes = read(fd, &str_len_buf, sizeof(size_t));
  if (read_bytes != (ssize_t)sizeof(size_t)) {
    LOGE("Failed to read string length: Not all bytes were read (%zd != %zu).\n", read_bytes, sizeof(size_t));

    return -1;
  }
  
  size_t str_len = str_len_buf[0];

  if (str_len > len) {
    LOGE("Failed to read string: Buffer is too small (%zu > %zu).\n", str_len, len);

    return -1;
  }

  read_bytes = read(fd, str, str_len);
  if (read_bytes != (ssize_t)str_len) {
    LOGE("Failed to read string: Promised bytes doesn't exist (%zd != %zu).\n", read_bytes, str_len);

    return -1;
  }

  return read_bytes;
}

/* INFO: Cannot use restrict here as execv does not have restrict */
bool exec_command(char *restrict buf, size_t len, const char *restrict file, char *const argv[]) {
  int link[2];
  pid_t pid;

  if (pipe(link) == -1) {
    LOGE("pipe: %s\n", strerror(errno));

    return false;
  }

  if ((pid = fork()) == -1) {
    LOGE("fork: %s\n", strerror(errno));

    return false;
  }

  if (pid == 0) {
    dup2(link[1], STDOUT_FILENO);
    close(link[0]);
    close(link[1]);
    
    execv(file, argv);
  } else {
    close(link[1]);

    int nbytes = read(link[0], buf, len);
    buf[nbytes - 1] = '\0';

    wait(NULL);
  }

  return true;
}

bool check_unix_socket(int fd, bool block) {
  struct pollfd pfd = {
    .fd = fd,
    .events = POLLIN,
    .revents = 0
  };

  int timeout = block ? -1 : 0;
  poll(&pfd, 1, timeout);

  return pfd.revents & ~POLLIN ? false : true;
}

/* INFO: Cannot use restrict here as execv does not have restrict */
int non_blocking_execv(const char *restrict file, char *const argv[]) {
  int link[2];
  pid_t pid;

  if (pipe(link) == -1) {
    LOGE("pipe: %s\n", strerror(errno));

    return -1;
  }

  if ((pid = fork()) == -1) {
    LOGE("fork: %s\n", strerror(errno));

    return -1;
  }

  if (pid == 0) {
    dup2(link[1], STDOUT_FILENO);
    close(link[0]);
    close(link[1]);

    execv(file, argv);
  } else {
    close(link[1]);

    return link[0];
  }

  return -1;
}

void stringify_root_impl_name(struct root_impl impl, char *restrict output) {
  switch (impl.impl) {
    case None: {
      strcpy(output, "None");

      break;
    }
    case Multiple: {
      strcpy(output, "Multiple");

      break;
    }
    case KernelSU: {
      strcpy(output, "KernelSU");

      break;
    }
    case APatch: {
      strcpy(output, "APatch");

      break;
    }
    case Magisk: {
      if (impl.variant == 0) {
        strcpy(output, "Magisk Official");
      } else {
        strcpy(output, "Magisk Kitsune");
      }

      break;
    }
  }
}
