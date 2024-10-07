#ifndef COMMON_H
#define COMMON_H

#include "../constants.h"

enum RootImpl {
  None,
  Multiple,
  KernelSU,
  APatch,
  Magisk
};

void root_impls_setup(void);

enum RootImpl get_impl(void);

bool uid_granted_root(uid_t uid);

bool uid_should_umount(uid_t uid);

bool uid_is_manager(uid_t uid);

#endif /* COMMON_H */
