#ifndef MAGISK_H
#define MAGISK_H

#include "../constants.h"

enum magisk_variants {
  Official,
  Kitsune
};

void magisk_get_existence(struct root_impl_state *state);

bool magisk_uid_granted_root(uid_t uid);

bool magisk_uid_should_umount(uid_t uid);

bool magisk_uid_is_manager(uid_t uid);

#endif
