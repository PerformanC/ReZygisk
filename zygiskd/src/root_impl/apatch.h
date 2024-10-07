#ifndef APATCH_H
#define APATCH_H

#include "../constants.h"

enum RootImplState apatch_get_existence(void);

bool apatch_uid_granted_root(uid_t uid);

bool apatch_uid_should_umount(uid_t uid);

bool apatch_uid_is_manager(uid_t uid);

#endif
