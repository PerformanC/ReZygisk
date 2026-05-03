#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/xattr.h>

#include <linux/limits.h>
#include <sched.h>
#include <unistd.h>

#include "root_impl/common.h"
#include "root_impl/kernelsu.h"
#include "root_impl/magisk.h"

#include "utils.h"

bool switch_mount_namespace(pid_t pid) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/ns/mnt", pid);

  int nsfd = open(path, O_RDONLY | O_CLOEXEC);
  if (nsfd == -1) {
    LOGE("Failed to open nsfd: %s", strerror(errno));

    return false;
  }

  if (setns(nsfd, CLONE_NEWNS) == -1) {
    LOGE("Failed to setns: %s", strerror(errno));

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
  FILE *sockcreate = fopen("/proc/thread-self/attr/sockcreate", "w");
  if (sockcreate == NULL) {
    LOGE("Failed to open sockcreate with %d: %s. Retrying with tid.", errno, strerror(errno));

    goto fail;
  }

  if (fwrite(context, 1, strlen(context), sockcreate) != strlen(context)) {
    LOGE("Failed to write to sockcreate with %d: %s. Retrying with tid.", errno, strerror(errno));

    fclose(sockcreate);

    goto fail;
  }

  fclose(sockcreate);

  return;

  fail:
    ;
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/proc/self/task/%d/attr/sockcreate", gettid());

    sockcreate = fopen(path, "w");
    if (sockcreate == NULL) {
      LOGE("Failed to open tid sockcreate with %d: %s", errno, strerror(errno));

      return;
    }

    if (fwrite(context, 1, strlen(context), sockcreate) != strlen(context)) {
      LOGE("Failed to write to tid sockcreate with %d: %s", errno, strerror(errno));

      return;
    }

    fclose(sockcreate);
}

static bool get_current_attr(char *restrict output, size_t size) {
  FILE *current = fopen("/proc/self/attr/current", "r");
  if (current == NULL) {
    LOGE("fopen: %s", strerror(errno));

    return false;
  }

  size_t ret = fread(output, 1, size, current);
  if (ferror(current)) {
    LOGE("fread: %s", strerror(errno));

    fclose(current);

    return false;
  }

  output[ret] = '\0';

  fclose(current);

  return true;
}

void unix_datagram_sendto(const char *restrict path, const void *restrict buf, size_t len) {
  char current_attr[PATH_MAX];
  if (!get_current_attr(current_attr, sizeof(current_attr))) {
    LOGE("Failed to get current attribute");

    return;
  }

  set_socket_create_context(current_attr);

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX
  };
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  int socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (socket_fd == -1) {
    LOGE("socket: %s", strerror(errno));

    set_socket_create_context("u:r:zygote:s0");

    return;
  }

  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    LOGE("connect: %s", strerror(errno));

    close(socket_fd);

    set_socket_create_context("u:r:zygote:s0");

    return;
  }

  if (sendto(socket_fd, buf, len, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    LOGE("sendto: %s", strerror(errno));

    close(socket_fd);

    set_socket_create_context("u:r:zygote:s0");

    return;
  }

  set_socket_create_context("u:r:zygote:s0");

  close(socket_fd);
}

int chcon(const char *restrict path, const char *context) {
  return lsetxattr(path, "security.selinux", context, strlen(context) + 1, 0);
}

int unix_listener_from_path(const char *restrict path) {
  if (remove(path) == -1 && errno != ENOENT) {
    LOGE("remove: %s", strerror(errno));

    return -1;
  }

  int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    LOGE("socket: %s", strerror(errno));

    return -1;
  }

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX
  };
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
    LOGE("bind: %s", strerror(errno));

    close(socket_fd);

    return -1;
  }

  if (listen(socket_fd, 2) == -1) {
    LOGE("listen: %s", strerror(errno));

    close(socket_fd);

    return -1;
  }

  if (chcon(path, "u:object_r:zygisk_file:s0") == -1)
    LOGW("chcon (non-fatal): %s", strerror(errno));

  return socket_fd;
}

ssize_t write_fd(int fd, int sendfd) {
  char cmsgbuf[CMSG_SPACE(sizeof(int))];
  char buf[1] = { 0 };

  struct iovec iov = {
    .iov_base = buf,
    .iov_len = sizeof(buf)
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
    LOGE("sendmsg: %s", strerror(errno));

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
    LOGE("recvmsg: %s", strerror(errno));

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
    LOGE("Failed to write string length: Not all bytes were written (%zd != %zu).", written_bytes, sizeof(size_t));

    return -1;
  }

  written_bytes = write(fd, str, str_len);
  if ((size_t)written_bytes != str_len) {
    LOGE("Failed to write string: Not all bytes were written.");

    return -1;
  }

  return written_bytes;
}

ssize_t read_string(int fd, char *restrict buf, size_t buf_size) {
  size_t str_len = 0;
  ssize_t read_bytes = read(fd, &str_len, sizeof(size_t));
  if (read_bytes != (ssize_t)sizeof(size_t)) {
    LOGE("Failed to read string length: Not all bytes were read (%zd != %zu).", read_bytes, sizeof(size_t));

    return -1;
  }

  if (str_len > buf_size - 1) {
    LOGE("Failed to read string: Buffer is too small (%zu > %zu - 1).", str_len, buf_size);

    return -1;
  }

  read_bytes = read(fd, buf, str_len);
  if (read_bytes != (ssize_t)str_len) {
    LOGE("Failed to read string: Promised bytes doesn't exist (%zd != %zu).", read_bytes, str_len);

    return -1;
  }

  if (str_len > 0) buf[str_len] = '\0';

  return read_bytes;
}

/* INFO: Cannot use restrict here as execv does not have restrict */
bool exec_command(char *restrict buf, size_t len, const char *restrict file, const char *const argv[]) {
  int link[2];
  pid_t pid;

  if (pipe(link) == -1) {
    LOGE("pipe: %s", strerror(errno));

    return false;
  }

  if ((pid = fork()) == -1) {
    LOGE("fork: %s", strerror(errno));

    close(link[0]);
    close(link[1]);

    return false;
  }

  if (pid == 0) {
    dup2(link[1], STDOUT_FILENO);
    close(link[0]);
    close(link[1]);

    /* NOTE: Sonarlint complains about a const qualifier drop here (c:S859),
               but this cast is deliberate and unavoidable.
    */
    execv(file, (char *const *)argv);

    LOGE("execv failed: %s", strerror(errno));
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
  struct pollfd pfd = {
    .fd = fd,
    .events = POLLIN,
    .revents = 0
  };

  int timeout = block ? -1 : 0;
  if (poll(&pfd, 1, timeout) == -1) {
    LOGE("poll: %s", strerror(errno));

    return false;
  }

  return pfd.revents & ~POLLIN ? false : true;
}

/* INFO: Cannot use restrict here as execv does not have restrict */
int non_blocking_execv(const char *restrict file, char *const argv[]) {
  int link[2];
  pid_t pid;

  if (pipe(link) == -1) {
    LOGE("pipe: %s", strerror(errno));

    return -1;
  }

  if ((pid = fork()) == -1) {
    LOGE("fork: %s", strerror(errno));

    return -1;
  }

  if (pid == 0) {
    dup2(link[1], STDOUT_FILENO);
    close(link[0]);
    close(link[1]);

    execv(file, argv);

    _exit(1);
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
      if (impl.variant == KOfficial) strcpy(output, "KernelSU");
      else strcpy(output, "KernelSU Next");

      break;
    }
    case APatch: {
      strcpy(output, "APatch");

      break;
    }
    case Magisk: {
      if (impl.variant == MOfficial) strcpy(output, "Magisk Official");
      else strcpy(output, "Magisk Kitsune");

      break;
    }
  }
}

struct mountinfo {
  unsigned int id;
  unsigned int parent;
  dev_t device;
  char *root;
  char *target;
  char *vfs_option;
  struct {
      unsigned int shared;
      unsigned int master;
      unsigned int propagate_from;
  } optional;
  char *type;
  char *source;
  char *fs_option;
};

struct mountinfos {
  struct mountinfo *mounts;
  size_t length;
};

char *strndup(const char *restrict str, size_t length) {
  char *restrict copy = malloc(length + 1);
  if (copy == NULL) return NULL;

  memcpy(copy, str, length);
  copy[length] = '\0';

  return copy;
}

void free_mounts(struct mountinfos *restrict mounts) {
  for (size_t i = 0; i < mounts->length; i++) {
    free(mounts->mounts[i].root);
    free(mounts->mounts[i].target);
    free(mounts->mounts[i].vfs_option);
    free(mounts->mounts[i].type);
    free(mounts->mounts[i].source);
    free(mounts->mounts[i].fs_option);
  }

  free(mounts->mounts);
}

bool parse_mountinfo(const char *restrict pid, struct mountinfos *restrict mounts) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/%s/mountinfo", pid);

  FILE *mountinfo = fopen(path, "r");
  if (mountinfo == NULL) {
    LOGE("fopen: %s", strerror(errno));

    return false;
  }

  char line[PATH_MAX];
  size_t i = 0;

  mounts->mounts = NULL;
  mounts->length = 0;

  while (fgets(line, sizeof(line), mountinfo) != NULL) {
    int root_start = 0, root_end = 0;
    int target_start = 0, target_end = 0;
    int vfs_option_start = 0, vfs_option_end = 0;
    int type_start = 0, type_end = 0;
    int source_start = 0, source_end = 0;
    int fs_option_start = 0, fs_option_end = 0;
    int optional_start = 0, optional_end = 0;
    unsigned int id, parent, maj, min;
    sscanf(line,
      "%u "           /* INFO: id */
      "%u "           /* INFO: parent id */
      "%u:%u "        /* INFO: maj:min */
      "%n%*s%n "      /* INFO: mountroot */
      "%n%*s%n "      /* INFO:target */
      "%n%*s%n"       /* INFO: vfs options (fs-independent) */
      "%n%*[^-]%n - " /* INFO: optional fields */
      "%n%*s%n "      /* INFO: FS type */
      "%n%*s%n "      /* INFO: source */
      "%n%*s%n",      /* INFO: fs options (fs specific) */
      &id, &parent, &maj, &min, &root_start, &root_end, &target_start,
      &target_end, &vfs_option_start, &vfs_option_end,
      &optional_start, &optional_end, &type_start, &type_end,
      &source_start, &source_end, &fs_option_start, &fs_option_end);

    struct mountinfo *tmp_mounts = (struct mountinfo *)realloc(mounts->mounts, (i + 1) * sizeof(struct mountinfo));
    if (!tmp_mounts) {
      LOGE("Failed to allocate memory for mounts->mounts");

      goto cleanup_mount_allocs;
    }
    mounts->mounts = tmp_mounts;

    unsigned int shared = 0;
    unsigned int master = 0;
    unsigned int propagate_from = 0;
    if (strstr(line + optional_start, "shared:")) {
      shared = (unsigned int)atoi(strstr(line + optional_start, "shared:") + 7);
    }

    if (strstr(line + optional_start, "master:")) {
      master = (unsigned int)atoi(strstr(line + optional_start, "master:") + 7);
    }

    if (strstr(line + optional_start, "propagate_from:")) {
      propagate_from = (unsigned int)atoi(strstr(line + optional_start, "propagate_from:") + 15);
    }

    mounts->mounts[i].id = id;
    mounts->mounts[i].parent = parent;
    mounts->mounts[i].device = (dev_t)(makedev(maj, min));
    mounts->mounts[i].root = strndup(line + root_start, (size_t)(root_end - root_start));
    if (mounts->mounts[i].root == NULL) {
      LOGE("Failed to allocate memory for root");

      goto cleanup_mount_allocs;
    }
    mounts->mounts[i].target = strndup(line + target_start, (size_t)(target_end - target_start));
    if (mounts->mounts[i].target == NULL) {
      LOGE("Failed to allocate memory for target");

      goto cleanup_root;
    }
    mounts->mounts[i].vfs_option = strndup(line + vfs_option_start, (size_t)(vfs_option_end - vfs_option_start));
    if (mounts->mounts[i].vfs_option == NULL) {
      LOGE("Failed to allocate memory for vfs_option");

      goto cleanup_target;
    }
    mounts->mounts[i].optional.shared = shared;
    mounts->mounts[i].optional.master = master;
    mounts->mounts[i].optional.propagate_from = propagate_from;
    mounts->mounts[i].type = strndup(line + type_start, (size_t)(type_end - type_start));
    if (mounts->mounts[i].type == NULL) {
      LOGE("Failed to allocate memory for type");

      goto cleanup_vfs_option;
    }
    mounts->mounts[i].source = strndup(line + source_start, (size_t)(source_end - source_start));
    if (mounts->mounts[i].source == NULL) {
      LOGE("Failed to allocate memory for source");

      goto cleanup_type;
    }
    mounts->mounts[i].fs_option = strndup(line + fs_option_start, (size_t)(fs_option_end - fs_option_start));
    if (mounts->mounts[i].fs_option == NULL) {
      LOGE("Failed to allocate memory for fs_option");

      goto cleanup_source;
    }

    i++;

    continue;

    cleanup_source:
      free(mounts->mounts[i].source);
    cleanup_type:
      free(mounts->mounts[i].type);
    cleanup_vfs_option:
      free(mounts->mounts[i].vfs_option);
    cleanup_target:
      free(mounts->mounts[i].target);
    cleanup_root:
      free(mounts->mounts[i].root);
    cleanup_mount_allocs:
      fclose(mountinfo);
      free_mounts(mounts);

      return false;
  }

  fclose(mountinfo);

  mounts->length = i;

  return true;
}

bool umount_root(struct root_impl impl) {
  /* INFO: We are already in the target pid mount namespace, so actually,
             when we use self here, we meant its pid.
  */
  struct mountinfos mounts;
  if (!parse_mountinfo("self", &mounts)) {
    LOGE("Failed to parse mountinfo");

    return false;
  }

  const char *source_name = "magisk";
  if (impl.impl == KernelSU) source_name = "KSU";
  else if (impl.impl == APatch) source_name = "APatch";

  LOGI("[%s] Unmounting root", source_name);

  char **targets_to_unmount = NULL;
  size_t num_targets = 0;

  for (size_t i = 0; i < mounts.length; i++) {
    struct mountinfo mount = mounts.mounts[i];

    bool should_unmount = false;
    if (strcmp(mount.source, source_name) == 0 || (impl.impl == Magisk && strcmp(mount.source, "worker") == 0)) should_unmount = true;
    if (strncmp(mount.target, "/data/adb/modules", strlen("/data/adb/modules")) == 0) should_unmount = true;
    if (strncmp(mount.root, "/adb/modules/", strlen("/adb/modules/")) == 0) should_unmount = true;

    if (!should_unmount) continue;

    char **tmp_targets = realloc(targets_to_unmount, (num_targets + 1) * sizeof(char*));
    if (tmp_targets == NULL) {
      LOGE("[%s] Failed to allocate memory for targets_to_unmount", source_name);

      free(targets_to_unmount);

      free_mounts(&mounts);

      return false;
    }
    targets_to_unmount = tmp_targets;

    num_targets++;

    targets_to_unmount[num_targets - 1] = mount.target;
  }

  for (size_t i = num_targets; i > 0; i--) {
    const char *target = targets_to_unmount[i - 1];
    if (umount2(target, MNT_DETACH) == -1) {
      LOGE("[%s] Failed to unmount %s: %s", source_name, target, strerror(errno));

      continue;
    }

    LOGI("[%s] Unmounted %s", source_name, target);
  }

  free(targets_to_unmount);

  free_mounts(&mounts);

  return true;
}

int save_mns_fd(int pid, enum MountNamespaceState mns_state, struct root_impl impl) {
  static int clean_namespace_fd = -1;
  static int mounted_namespace_fd = -1;

  if (mns_state == Clean && clean_namespace_fd != -1) return clean_namespace_fd;
  if (mns_state == Mounted && mounted_namespace_fd != -1) return mounted_namespace_fd;

  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
    LOGE("socketpair: %s", strerror(errno));

    return -1;
  }

  int socket_parent = sockets[0];
  int socket_child = sockets[1];

  pid_t fork_pid = fork();
  if (fork_pid < 0) {
    LOGE("fork: %s", strerror(errno));

    close(socket_parent);
    close(socket_child);

    return -1;
  }

  if (fork_pid == 0) {
    close(socket_parent);

    if (switch_mount_namespace(pid) == false) {
      LOGE("Failed to switch mount namespace");

      if (write_uint8_t(socket_child, 0) == -1)
        LOGE("Failed to write to socket_child: %s", strerror(errno));

      goto finalize_mns_fork;
    }

    if (mns_state == Clean) {
      unshare(CLONE_NEWNS);

      if (!umount_root(impl)) {
        LOGE("Failed to umount root");

        if (write_uint8_t(socket_child, 0) == -1)
          LOGE("Failed to write to socket_child: %s", strerror(errno));

        goto finalize_mns_fork;
      }
    }

    if (write_uint8_t(socket_child, 1) == -1) {
      LOGE("Failed to write to socket_child: %s", strerror(errno));

      close(socket_child);

      _exit(1);
    }

    uint8_t has_opened = 0;
    if (read_uint8_t(socket_child, &has_opened) == -1)
      LOGE("Failed to read from socket_child: %s", strerror(errno));

    finalize_mns_fork:
      close(socket_child);

      _exit(0);
  }

  close(socket_child);

  uint8_t has_succeeded = 0;
  if (read_uint8_t(socket_parent, &has_succeeded) == -1) {
    LOGE("Failed to read from socket_parent: %s", strerror(errno));

    close(socket_parent);

    return -1;
  }

  if (!has_succeeded) {
    LOGE("Failed to umount root");

    close(socket_parent);

    return -1;
  }

  char ns_path[PATH_MAX];
  snprintf(ns_path, PATH_MAX, "/proc/%d/ns/mnt", fork_pid);

  int ns_fd = open(ns_path, O_RDONLY);
  if (ns_fd == -1) {
    LOGE("open: %s", strerror(errno));

    close(socket_parent);

    return -1;
  }

  uint8_t opened_signal = 1;
  if (write_uint8_t(socket_parent, opened_signal) == -1) {
    LOGE("Failed to write to socket_parent: %s", strerror(errno));

    close(ns_fd);
    close(socket_parent);

    return -1;
  }

  if (close(socket_parent) == -1) {
    LOGE("Failed to close socket_parent: %s", strerror(errno));

    close(ns_fd);

    return -1;
  }

  if (waitpid(fork_pid, NULL, 0) == -1) {
    LOGE("waitpid: %s", strerror(errno));

    return -1;
  }

  if (impl.impl == Magisk && impl.variant == MKitsune && mns_state == Clean) {
    LOGI("[Magisk] Magisk Kitsune detected, will skip cache first.");

    /* INFO: MagiskSU of Kitsune has a special behavior: It is only mounted
               once system boots, because of that, we can only cache once
               that happens, or else it will clean the mounts, then later
               get MagiskSU mounted, resulting in a mount leak.

       SOURCES:
        - https://github.com/1q23lyc45/KitsuneMagisk/blob/8562a0b2ad142d21566c1ea41690ad64108ca14c/native/src/core/bootstages.cpp#L359
    */
    char boot_completed[2];
    get_property("sys.boot_completed", boot_completed);

    if (boot_completed[0] == '1') {
      LOGI("[Magisk] Appropriate mns found, caching clean namespace fd.");

      clean_namespace_fd = ns_fd;
    }

    /* BUG: For the case where it hasn't booted yet, we will need to
              keep creating mns that will be left behind, the issue is:
              they are not close'd. This is a problem, because we will
              have a leak of fds, although only from the period of booting.

            When trying to close the ns_fd from the libzygisk.so, fdsan
              will complain as it is owned by RandomAccessFile, and if
              we close from ReZygiskd, system will refuse to boot for
              some reason, even if we wait for setns in libzygisk.so,
              and that issue is related to setns, as it only happens
              with it.
    */

    return ns_fd;
  }

  if (mns_state == Clean) clean_namespace_fd = ns_fd;
  else if (mns_state == Mounted) mounted_namespace_fd = ns_fd;

  return ns_fd;
}
