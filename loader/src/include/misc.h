#ifndef MISC_H
#define MISC_H

#include <stdbool.h>
#include <inttypes.h>

#ifdef __LP64__
  #define LP_SELECT(lp32, lp64) lp64
#else
  #define LP_SELECT(lp32, lp64) lp32
#endif

#define IS_ISOLATED_SERVICE(uid)      \
  ((uid) >= 90000 && (uid) < 1000000)

struct kernel_version {
  uint8_t major;
  unsigned int minor;
  unsigned int patch;
};

struct map_entry {
  uintptr_t start;
  uintptr_t end;
  int perms;
  bool is_private;
  uintptr_t offset;
  dev_t dev;
  ino_t inode;
  char *path;
};

struct maps_info {
  struct map_entry *maps;
  size_t length;
};

int parse_int(const char *str);

struct kernel_version parse_kversion();

struct maps_info *parse_maps_safe(const char *pid);

struct maps_info *parse_maps(const char *pid);

void free_maps(struct maps_info *maps);

#endif /* MISC_H */