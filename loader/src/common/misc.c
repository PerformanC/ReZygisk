#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <ctype.h>

#include "logging.h"
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
        PLOGE("reallocate (extend: %zu -> %zu) memory for maps", infos_capacity / 2, infos_capacity);

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

  if (info_array->length == 0) {
    LOGE("Failed to find any maps in %s", pid);

    free(info_array);

    return NULL;
  }

  /* INFO: Resize to the actual size */
  struct map_entry *tmp_maps = realloc(info_array->maps, info_array->length * sizeof(struct map_entry));
  if (!tmp_maps)
    PLOGE("reallocate (reduce: %zu -> %zu) memory for maps", infos_capacity, info_array->length);

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
