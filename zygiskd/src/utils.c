#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/sysmacros.h>
#include <sys/mount.h>

#include <unistd.h>
#include <linux/limits.h>
#include <sched.h>
#include <pthread.h>

#include <android/log.h>

#include "utils.h"
#include "root_impl/common.h"
#include "root_impl/magisk.h"

int clean_namespace_fd = 0;
int rooted_namespace_fd = 0;
int module_namespace_fd = 0;

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

struct mountinfo {
  unsigned int id;
  unsigned int parent;
  dev_t device;
  const char *root;
  const char *target;
  const char *vfs_option;
  struct {
      unsigned int shared;
      unsigned int master;
      unsigned int propagate_from;
  } optional;
  const char *type;
  const char *source;
  const char *fs_option;
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
    free((void *)mounts->mounts[i].root);
    free((void *)mounts->mounts[i].target);
    free((void *)mounts->mounts[i].vfs_option);
    free((void *)mounts->mounts[i].type);
    free((void *)mounts->mounts[i].source);
    free((void *)mounts->mounts[i].fs_option);
  }

  free((void *)mounts->mounts);
}

bool parse_mountinfo(const char *restrict pid, struct mountinfos *restrict mounts) {
  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/%s/mountinfo", pid);

  FILE *mountinfo = fopen(path, "r");
  if (mountinfo == NULL) {
    LOGE("fopen: %s\n", strerror(errno));

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
            "%u "           // (1) id
            "%u "           // (2) parent
            "%u:%u "        // (3) maj:min
            "%n%*s%n "      // (4) mountroot
            "%n%*s%n "      // (5) target
            "%n%*s%n"       // (6) vfs options (fs-independent)
            "%n%*[^-]%n - " // (7) optional fields
            "%n%*s%n "      // (8) FS type
            "%n%*s%n "      // (9) source
            "%n%*s%n",      // (10) fs options (fs specific)
            &id, &parent, &maj, &min, &root_start, &root_end, &target_start,
            &target_end, &vfs_option_start, &vfs_option_end,
            &optional_start, &optional_end, &type_start, &type_end,
            &source_start, &source_end, &fs_option_start, &fs_option_end);

    mounts->mounts = (struct mountinfo *)realloc(mounts->mounts, (i + 1) * sizeof(struct mountinfo));
    if (!mounts->mounts) {
      LOGE("Failed to allocate memory for mounts->mounts");

      fclose(mountinfo);
      free_mounts(mounts);

      return false;
    }

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
    mounts->mounts[i].target = strndup(line + target_start, (size_t)(target_end - target_start));
    mounts->mounts[i].vfs_option = strndup(line + vfs_option_start, (size_t)(vfs_option_end - vfs_option_start));
    mounts->mounts[i].optional.shared = shared;
    mounts->mounts[i].optional.master = master;
    mounts->mounts[i].optional.propagate_from = propagate_from;
    mounts->mounts[i].type = strndup(line + type_start, (size_t)(type_end - type_start));
    mounts->mounts[i].source = strndup(line + source_start, (size_t)(source_end - source_start));
    mounts->mounts[i].fs_option = strndup(line + fs_option_start, (size_t)(fs_option_end - fs_option_start));

    i++;
  }

  fclose(mountinfo);

  mounts->length = i;

  return true;
}

enum mns_umount_state {
  Complete,
  NotComplete,
  Error
};

enum mns_umount_state unmount_root(bool modules_only, struct root_impl impl) {
  /* INFO: We are already in the target pid mount namespace, so actually,
             when we use self here, we meant its pid.
  */
  struct mountinfos mounts;
  if (!parse_mountinfo("self", &mounts)) {
    LOGE("Failed to parse mountinfo\n");

    return Error;
  }

  /* INFO: Implementations like Magisk Kitsune will mount MagiskSU when boot is completed,
             so if we cache the clean mount done before the boot is completed, it will get
             it mounted later and hence it will leak mounts. To avoid that we will detect
             if implementation is Kitsune, and if so, see if /system/bin... is mounted,
             if not, it won't cache this namespace. */
  bool magiskSU_umounted = false;

  switch (impl.impl) {
    case None: { break; }
    case Multiple: { break; }

    case KernelSU:
    case APatch: {
      char source_name[LONGEST_ROOT_IMPL_NAME];
      if (impl.impl == KernelSU) strcpy(source_name, "KSU");
      else strcpy(source_name, "APatch");

      for (size_t i = mounts.length - 1; i > 0; i--) {
        struct mountinfo mount = mounts.mounts[i];

        if (
          (
            modules_only && 
            (
              strncmp(mount.target, "/debug_ramdisk", strlen("/debug_ramdisk")) == 0
            )
          ) ||
          (
            strcmp(mount.source, source_name) == 0 ||
            strncmp(mount.root, "/adb/modules", strlen("/adb/modules")) == 0 || 
            strncmp(mount.target, "/data/adb/modules", strlen("/data/adb/modules")) == 0
          )
        ) {
          if (umount2(mount.target, MNT_DETACH) == -1) {
            LOGE("[Magisk] Failed to unmount %s: %s\n", mount.target, strerror(errno));

            continue;
          }

          LOGI("[%s] Unmounted %s (%s | %s)\n", source_name, mount.target, mount.root, mount.source);
        }
      }

      break;
    }
    case Magisk: {
      LOGI("[Magisk] Unmounting root %s modules\n", modules_only ? "only" : "with");

      for (size_t i = mounts.length - 1; i > 0; i--) {
        struct mountinfo mount = mounts.mounts[i];

        if (
          (
            modules_only && 
            (
              strcmp(mount.source, "magisk") == 0 ||
              strncmp(mount.target, "/debug_ramdisk", strlen("/debug_ramdisk")) == 0 ||
              strncmp(mount.target, "/system/bin", strlen("/system/bin")) == 0
            )
          ) ||
          (
            !modules_only && 
            (
              strcmp(mount.source, "magisk") == 0 ||
              strncmp(mount.target, "/debug_ramdisk", strlen("/debug_ramdisk")) == 0 ||
              strncmp(mount.target, "/data/adb/modules", strlen("/data/adb/modules")) == 0 ||
              strncmp(mount.root, "/adb/modules", strlen("/adb/modules")) == 0 ||
              strncmp(mount.target, "/system/bin", strlen("/system/bin")) == 0
            )
          )
        ) {
          if (impl.impl == Magisk && strncmp(mount.target, "/system/bin", strlen("/system/bin")) == 0)
            magiskSU_umounted = true;

          if (umount2(mount.target, MNT_DETACH) == -1) {
            LOGE("[Magisk] Failed to unmount %s: %s\n", mount.target, strerror(errno));

            continue;
          }

          LOGI("[Magisk] Unmounted %s\n", mount.target);
        } else {
          LOGI("[Magisk] Skipped unmounting %s (%s | %s)\n", mount.target, mount.root, mount.source);
        }
      }
    }
  }

  free_mounts(&mounts);

  return (impl.impl == Magisk && !magiskSU_umounted) ? NotComplete : Complete;
}

int save_mns_fd(int pid, enum MountNamespaceState mns_state, struct root_impl impl) {
  LOGI(" - Saving mount namespace fd for pid %d. State: %d\n", pid, mns_state);

  LOGI(" - Clean namespace fd: %d\n", clean_namespace_fd);
  LOGI(" - Rooted namespace fd: %d\n", rooted_namespace_fd);
  LOGI(" - Module namespace fd: %d\n", module_namespace_fd);

  if (mns_state == Clean && clean_namespace_fd != 0) return clean_namespace_fd;
  if (mns_state == Rooted && rooted_namespace_fd != 0) return rooted_namespace_fd;
  if (mns_state == Module && module_namespace_fd != 0) return module_namespace_fd;

  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
    LOGE("socketpair: %s\n", strerror(errno));

    return -1;
  }

  int reader = sockets[0];
  int writer = sockets[1];

  pid_t fork_pid = fork();
  if (fork_pid == 0) {
    switch_mount_namespace(pid);

    enum mns_umount_state umount_state = Complete;

    if (mns_state != Rooted) {
      unshare(CLONE_NEWNS);
      umount_state = unmount_root(mns_state == Module, impl);
      if (umount_state == Error) {
        write_uint8_t(writer, (uint8_t)umount_state);

        _exit(1);
      }
    }

    uint32_t mypid = 0;
    while (mypid != (uint32_t)getpid()) {
      write_uint8_t(writer, (uint8_t)umount_state);
      usleep(50);
      read_uint32_t(reader, &mypid);
    }

    _exit(0);
  } else if (fork_pid > 0) {
    enum mns_umount_state umount_state = (enum mns_umount_state)0;
    read_uint8_t(reader, (uint8_t *)&umount_state);

    if (umount_state == Error) {
      LOGE("Failed to unmount root\n");

      return -1;
    }

    char ns_path[PATH_MAX];
    snprintf(ns_path, PATH_MAX, "/proc/%d/ns/mnt", fork_pid);

    int ns_fd = open(ns_path, O_RDONLY);
    if (ns_fd == -1) {
      LOGE("open: %s\n", strerror(errno));

      return -1;
    }

    write_uint32_t(writer, (uint32_t)fork_pid);

    if (close(reader) == -1) {
      LOGE("Failed to close reader: %s\n", strerror(errno));

      return -1;
    }

    if (close(writer) == -1) {
      LOGE("Failed to close writer: %s\n", strerror(errno));

      return -1;
    }

    if (waitpid(fork_pid, NULL, 0) == -1) {
      LOGE("waitpid: %s\n", strerror(errno));

      return -1;
    }

    LOGI(" - Forked child exited\n");

    if (mns_state == Rooted) return (rooted_namespace_fd = ns_fd);
    else if (mns_state == Clean && umount_state == Complete) return (clean_namespace_fd = ns_fd);
    else if (mns_state == Module && umount_state == Complete) return (module_namespace_fd = ns_fd);
    else return ns_fd;
  } else {
    LOGE("fork: %s\n", strerror(errno));

    return -1;
  }

  return -1;
}
