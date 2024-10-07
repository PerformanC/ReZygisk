#ifndef MAGISK_H
#define MAGISK_H

#include "../constants.h"

enum magisk_variants {
  Official,
  Kitsune
};

enum RootImplState magisk_get_existence(void);

bool magisk_uid_granted_root(uid_t uid);

bool magisk_uid_should_umount(uid_t uid);

bool magisk_uid_is_manager(uid_t uid);

#endif
