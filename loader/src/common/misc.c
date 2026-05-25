#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>

#include <unistd.h>

#include "logging.h"
#include "socket_utils.h"

#include "misc.h"

int parse_int(const char *str) {
  int val = 0;

  char *c = (char *)str;
  while (*c) {
    if (*c > '9' || *c < '0')
      return -1;

    val = val * 10 + *c - '0';
    c++;
  }

  return val;
}

struct kernel_version parse_kversion() {
  struct utsname uts;
  if (uname(&uts) == -1) {
    PLOGE("uname");

    return (struct kernel_version) { 0 };
  }

  struct kernel_version version;
  if (sscanf(uts.release, "%hhu.%u.%u", &version.major, &version.minor, &version.patch) != 3) {
    LOGE("Failed to parse kernel version");

    return (struct kernel_version) { 0 };
  }

  return version;
}

/* INFO: Opening /proc/.../maps leads to its access time being updated. This
           function bypasses this by reading the maps from a forked process,
           which is the same memory topology anyway. See more information in
           parse_maps().
*/
struct maps_info *parse_maps_safe(const char *pid) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0) {
    LOGE("Failed to create socket pair");

    return NULL;
  }

  int ppid = clone(NULL, NULL, SIGCHLD, NULL);
  if (ppid == -1) {
    LOGE("Failed to clone process");

    close(sockets[0]);
    close(sockets[1]);

    return NULL;
  }

  if (ppid == 0) {
    close(sockets[0]);

    char path[64];
    snprintf(path, sizeof(path), "/proc/%s/maps", pid);

    int maps_file = open(path, O_RDONLY | O_CLOEXEC);
    if (maps_file < 0) {
      LOGE("Failed to open %s", path);

      uint8_t can_kill_myself = 0;
      if (TEMP_FAILURE_RETRY(write(sockets[1], &can_kill_myself, sizeof(can_kill_myself))) < 0) {
        LOGE("Failed to write to socket");
      }

      goto scan_children_fail;
    }

    if (write_fd(sockets[1], maps_file) < 0) {
      LOGE("Failed to write file descriptor to socket");

      goto post_open_scan_children_fail;
    }

    /* INFO: Wait for the parent process to finish reading */
    uint8_t can_kill_myself = 1;
    if (TEMP_FAILURE_RETRY(read(sockets[1], &can_kill_myself, sizeof(can_kill_myself))) < 0) {
      LOGE("Failed to read from socket");

      goto post_open_scan_children_fail;
    }

    close(maps_file);
    close(sockets[1]);

    _exit(EXIT_SUCCESS);

    post_open_scan_children_fail:
      close(maps_file);
    scan_children_fail:
      close(sockets[1]);

      _exit(EXIT_FAILURE);
  }

  close(sockets[1]);

  int fd = read_fd(sockets[0]);
  if (fd < 0) {
    LOGE("Failed to read file descriptor from socket");

    close(sockets[0]);

    return NULL;
  }

  FILE *fp = fdopen(fd, "r");
  if (!fp) {
    LOGE("Failed to open file descriptor as FILE");

    close(fd);
    close(sockets[0]);

    return NULL;
  }

  struct maps_info *info_array = calloc(1, sizeof(struct maps_info));
  if (!info_array) {
    PLOGE("allocate memory");

    close(fd);
    close(sockets[0]);

    return NULL;
  }

  size_t infos_capacity = 2;
  info_array->maps = malloc(infos_capacity * sizeof(struct map_entry));
  if (!info_array->maps) {
    PLOGE("allocate memory for maps");

    free(info_array);

    close(fd);
    close(sockets[0]);

    return NULL;
  }
  info_array->length = 0;

  char line[1024];
  while (fgets(line, sizeof(line), fp) != NULL) {
    line[strlen(line) - 1] = '\0';

    uintptr_t start, end, offset;
    unsigned int dev_major, dev_minor;
    ino_t inode;
    char perms[5] = { 0 };
    int path_off;

    if (sscanf(line, "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %x:%x %lu %n",
               &start, &end, perms, &offset, &dev_major, &dev_minor, &inode, &path_off) != 7) {
      continue;
    }

    uint8_t perms_bit = 0;
    if (perms[0] == 'r') perms_bit |= PROT_READ;
    if (perms[1] == 'w') perms_bit |= PROT_WRITE;
    if (perms[2] == 'x') perms_bit |= PROT_EXEC;

    while (isspace((unsigned char)line[path_off]))
      path_off++;

    char *path_str = strdup(line + path_off);
    if (!path_str) {
      PLOGE("allocate memory for map path");

      goto cleanup_maps;
    }

    if (info_array->length >= infos_capacity) {
      infos_capacity *= 2;
      struct map_entry *tmp_maps = realloc(info_array->maps, infos_capacity * sizeof(struct map_entry));
      if (!tmp_maps) {
        PLOGE("reallocate memory for maps");

        goto cleanup_maps_and_path;
      }
      info_array->maps = tmp_maps;
    }

    struct map_entry new_map = {
      .start = start,
      .end = end,
      .perms = perms_bit,
      .is_private = (perms[3] == 'p'),
      .offset = offset,
      .dev = makedev(dev_major, dev_minor),
      .inode = inode,
      .path = path_str
    };

    info_array->maps[info_array->length++] = new_map;

    continue;

    cleanup_maps_and_path:
      free(path_str);
    cleanup_maps:
      for (size_t i = 0; i < info_array->length; i++) {
        free(info_array->maps[i].path);
      }
      free(info_array->maps);
      free(info_array);

      fclose(fp);
      close(sockets[0]);

      waitpid(ppid, NULL, 0);

      return NULL;
  }

  fclose(fp);

  /* INFO: Notify the children process that we are done */
  uint8_t can_kill_itself = 1;
  if (TEMP_FAILURE_RETRY(write(sockets[0], &can_kill_itself, sizeof(can_kill_itself))) < 0) {
    LOGE("Failed to write to socket");

    goto cleanup_maps;
  }

  close(sockets[0]);

  /* INFO: Resize to the actual size */
  struct map_entry *tmp_maps = realloc(info_array->maps, info_array->length * sizeof(struct map_entry));
  if (!tmp_maps)
    PLOGE("reallocate memory for maps");

  if (tmp_maps) info_array->maps = tmp_maps;
  /* INFO: This waitpid ensures that we only resume code execution once the child dies,
            or the child process will become zombie as shown in /proc/<child_pid>/status */
  waitpid(ppid, NULL, 0);

  return info_array;
}

/* INFO: Accessing /proc/.../maps will update its access time. This is detectable
           by using stat() to check when the application takes control of the
           execution of the process. However, if we do this before the fork(),
           it will update the access time of the maps file of the parent process,
           not child, making it undetectable.
*/
struct maps_info *parse_maps(const char *pid) {
  /* INFO: The character limit for a 32-bit PID is 10 */
  char path[(sizeof("/proc//maps") - 1) + 10 + 1];
  snprintf(path, sizeof(path), "/proc/%s/maps", pid);

  FILE *fp = fopen(path, "r");
  if (!fp) {
    PLOGE("Failed to open %s", path);

    return NULL;
  }

  struct maps_info *info_array = calloc(1, sizeof(struct maps_info));
  if (!info_array) {
    PLOGE("allocate memory");

    fclose(fp);

    return NULL;
  }

  size_t infos_capacity = 2;
  info_array->maps = malloc(infos_capacity * sizeof(struct map_entry));
  if (!info_array->maps) {
    PLOGE("allocate memory for maps");

    free(info_array);
    fclose(fp);

    return NULL;
  }
  info_array->length = 0;

  char line[1024];
  while (fgets(line, sizeof(line), fp) != NULL) {
    line[strlen(line) - 1] = '\0';

    uintptr_t start, end, offset;
    unsigned int dev_major, dev_minor;
    ino_t inode;
    char perms[5] = { 0 };
    int path_off;

    if (sscanf(line, "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %x:%x %lu %n",
               &start, &end, perms, &offset, &dev_major, &dev_minor, &inode, &path_off) != 7) {
      continue;
    }

    uint8_t perms_bit = 0;
    if (perms[0] == 'r') perms_bit |= PROT_READ;
    if (perms[1] == 'w') perms_bit |= PROT_WRITE;
    if (perms[2] == 'x') perms_bit |= PROT_EXEC;

    while (isspace((unsigned char)line[path_off]))
      path_off++;

    char *path_str = strdup(line + path_off);
    if (!path_str) {
      PLOGE("allocate memory for map path");

      goto cleanup_maps;
    }

    if (info_array->length >= infos_capacity) {
      infos_capacity *= 2;
      struct map_entry *tmp_maps = realloc(info_array->maps, infos_capacity * sizeof(struct map_entry));
      if (!tmp_maps) {
        PLOGE("reallocate memory for maps");

        goto cleanup_maps_and_path;
      }
      info_array->maps = tmp_maps;
    }

    struct map_entry new_map = {
      .start = start,
      .end = end,
      .perms = perms_bit,
      .is_private = (perms[3] == 'p'),
      .offset = offset,
      .dev = makedev(dev_major, dev_minor),
      .inode = inode,
      .path = path_str
    };

    info_array->maps[info_array->length++] = new_map;

    continue;

    cleanup_maps_and_path:
      free(path_str);
    cleanup_maps:
      for (size_t i = 0; i < info_array->length; i++) {
        free(info_array->maps[i].path);
      }
      free(info_array->maps);
      free(info_array);

      fclose(fp);

      return NULL;
  }

  fclose(fp);

  /* INFO: Resize to the actual size */
  struct map_entry *tmp_maps = realloc(info_array->maps, info_array->length * sizeof(struct map_entry));
  if (!tmp_maps)
    PLOGE("reallocate memory for maps");

  if (tmp_maps) info_array->maps = tmp_maps;

  return info_array;
}

void free_maps(struct maps_info *maps) {
  for (size_t i = 0; i < maps->length; i++) {
    free(maps->maps[i].path);
  }

  free(maps->maps);
  free(maps);
}
