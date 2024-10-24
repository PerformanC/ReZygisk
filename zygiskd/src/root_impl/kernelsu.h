#ifndef KERNELSU_H
#define KERNELSU_H

#include "../constants.h"

void ksu_get_existence(struct root_impl_state *state);

bool ksu_uid_granted_root(uid_t uid);

bool ksu_uid_should_umount(uid_t uid);

bool ksu_uid_is_manager(uid_t uid);

#endif
