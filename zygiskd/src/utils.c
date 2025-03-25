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

  ssize_t ret = recvmsg(fd, &msg, MSG_WAITALL);
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

write_func(size_t)
read_func(size_t)

write_func(uint32_t)
read_func(uint32_t)

write_func(uint8_t)
read_func(uint8_t)

ssize_t write_string(int fd, const char *restrict str) {
  size_t str_len = strlen(str);
  ssize_t written_bytes = write(fd, &str_len, sizeof(size_t));
  if (written_bytes != sizeof(size_t)) {
    LOGE("Failed to write string length: Not all bytes were written (%zd != %zu).\n", written_bytes, sizeof(size_t));

    return -1;
  }

  written_bytes = write(fd, str, str_len);
  if ((size_t)written_bytes != str_len) {
    LOGE("Failed to write string: Not all bytes were written.\n");

    return -1;
  }

  return written_bytes;
}

ssize_t read_string(int fd, char *restrict buf, size_t buf_size) {
  size_t str_len = 0;
  ssize_t read_bytes = read(fd, &str_len, sizeof(size_t));
  if (read_bytes != (ssize_t)sizeof(size_t)) {
    LOGE("Failed to read string length: Not all bytes were read (%zd != %zu).\n", read_bytes, sizeof(size_t));

    return -1;
  }
  
  if (str_len > buf_size - 1) {
    LOGE("Failed to read string: Buffer is too small (%zu > %zu - 1).\n", str_len, buf_size);

    return -1;
  }

  read_bytes = read(fd, buf, str_len);
  if (read_bytes != (ssize_t)str_len) {
    LOGE("Failed to read string: Promised bytes doesn't exist (%zd != %zu).\n", read_bytes, str_len);

    return -1;
  }

  if (str_len > 0) buf[str_len] = '\0';

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

    close(link[0]);
    close(link[1]);

    return false;
  }

  if (pid == 0) {
    dup2(link[1], STDOUT_FILENO);
    close(link[0]);
    close(link[1]);
    
    execv(file, argv);

    LOGE("execv failed: %s\n", strerror(errno));
    _exit(1);
  } else {
    close(link[1]);

    ssize_t nbytes = read(link[0], buf, len);
    if (nbytes > 0) buf[nbytes - 1] = '\0';
    /* INFO: If something went wrong, at least we must ensure it is NULL-terminated */
    else buf[0] = '\0';

    wait(NULL);

    close(link[0]);
  }

  return true;
}

bool check_unix_socket(int fd, bool block) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;

  // Use a shorter timeout for non-blocking checks to improve responsiveness
  int timeout = block ? 1000 : 0;
  
  int ret = poll(&pfd, 1, timeout);
  if (ret == -1) {
    if (errno == EINTR) return true; // Interrupted by signal, socket is still valid
    LOGE("poll: %s\n", strerror(errno));
    return false;
  }

  if (ret == 0) return true; // Timeout, socket is still valid
  
  // Check for socket errors
  if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
    return false;
  }
  
  return true;
}

// Improved implementation with better error handling and resource management
int non_blocking_execv(const char *restrict file, char *const argv[]) {
  pid_t pid = fork();
  if (pid == -1) {
    LOGE("fork: %s\n", strerror(errno));
    return -1;
  }

  if (pid > 0) {
    // Parent process
    return 0;
  }

  // Child process
  
  // Set process priority to reduce battery impact
  if (setpriority(PRIO_PROCESS, 0, 10) == -1) {
    LOGE("setpriority: %s\n", strerror(errno));
    // Continue anyway
  }
  
  // Close all unnecessary file descriptors
  int max_fd = sysconf(_SC_OPEN_MAX);
  for (int i = 3; i < max_fd; i++) {
    // Keep only the essential file descriptors
    close(i);
  }

  // Execute the command
  execv(file, argv);
  
  // If we get here, execv failed
  LOGE("execv: %s\n", strerror(errno));
  exit(1);
}

// Improved implementation with better memory management
void stringify_root_impl_name(struct root_impl impl, char *restrict output) {
  switch (impl.impl) {
    case None: {
      strncpy(output, "None", LONGEST_ROOT_IMPL_NAME - 1);
      output[LONGEST_ROOT_IMPL_NAME - 1] = '\0';
      break;
    }
    case Multiple: {
      strncpy(output, "Multiple", LONGEST_ROOT_IMPL_NAME - 1);
      output[LONGEST_ROOT_IMPL_NAME - 1] = '\0';
      break;
    }
    case KernelSU: {
      strncpy(output, "KernelSU", LONGEST_ROOT_IMPL_NAME - 1);
      output[LONGEST_ROOT_IMPL_NAME - 1] = '\0';
      break;
    }
    case APatch: {
      strncpy(output, "APatch", LONGEST_ROOT_IMPL_NAME - 1);
      output[LONGEST_ROOT_IMPL_NAME - 1] = '\0';
      break;
    }
    case Magisk: {
      strncpy(output, "Magisk", LONGEST_ROOT_IMPL_NAME - 1);
      output[LONGEST_ROOT_IMPL_NAME - 1] = '\0';
      break;
    }
  }
}

// Add a new utility function to safely close file descriptors
void safe_close(int *fd) {
  if (fd != NULL && *fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

// Add a new utility function to set process and thread priorities
bool set_low_priority(void) {
  // Set CPU scheduling policy to batch processing (lower priority)
  struct sched_param param;
  param.sched_priority = 0; // Lowest priority for SCHED_OTHER
  
  if (sched_setscheduler(0, SCHED_BATCH, &param) == -1) {
    LOGE("sched_setscheduler: %s\n", strerror(errno));
    // Try alternative method
    if (setpriority(PRIO_PROCESS, 0, 10) == -1) {
      LOGE("setpriority: %s\n", strerror(errno));
      return false;
    }
  }
  
  // Reduce I/O priority if possible
  #ifdef __NR_ioprio_set
  syscall(__NR_ioprio_set, 1, 0, 7 | (3 << 13)); // Class: IDLE, priority: 7 (lowest)
  #endif
  
  return true;
}

// Add a new utility function to check and limit memory usage
bool check_memory_usage(void) {
  FILE *meminfo = fopen("/proc/self/status", "r");
  if (meminfo == NULL) {
    LOGE("Failed to open memory info: %s\n", strerror(errno));
    return false;
  }
  
  char line[256];
  unsigned long vm_rss = 0;
  
  while (fgets(line, sizeof(line), meminfo) != NULL) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      // Extract the RSS value (in kB)
      char *p = line + 6;
      while (*p == ' ' || *p == '\t') p++;
      vm_rss = strtoul(p, NULL, 10);
      break;
    }
  }
  
  fclose(meminfo);
  
  // If memory usage is too high (> 50MB), log a warning
  if (vm_rss > 50000) {
    LOGE("High memory usage detected: %lu kB\n", vm_rss);
    return false;
  }
  
  return true;
}
