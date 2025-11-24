#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <sys/types.h>

#include "../constants.h"

enum root_impls {
  None,
  Multiple,
  KernelSU,
  APatch,
  Magisk
};

struct root_impl_state {
  enum RootImplState state;
  uint8_t variant;
};

struct root_impl {
  enum root_impls impl;
  uint8_t variant;
};

#define LONGEST_ROOT_IMPL_NAME sizeof("Magisk Official")

void root_impls_setup(void);

void get_impl(struct root_impl *uimpl);

bool uid_granted_root(uid_t uid);

bool uid_should_umount(uid_t uid, const char *const process);

bool uid_is_manager(uid_t uid);

void root_impl_cleanup(void);

#endif /* COMMON_H */
