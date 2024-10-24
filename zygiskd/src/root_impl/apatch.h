#ifndef APATCH_H
#define APATCH_H

#include "../constants.h"

void apatch_get_existence(struct root_impl_state *state);

bool apatch_uid_granted_root(uid_t uid);

bool apatch_uid_should_umount(uid_t uid);

bool apatch_uid_is_manager(uid_t uid);

#endif
