#include <stdio.h>

#include <sys/types.h>

#include "../utils.h"
#include "kernelsu.h"
#include "apatch.h"
#include "magisk.h"

#include "common.h"

static enum RootImpl ROOT_IMPL = None;

void root_impls_setup(void) {
  if (ksu_get_existence() == Supported) ROOT_IMPL = KernelSU;
  else if (apatch_get_existence() == Supported) ROOT_IMPL = APatch;
  else if (magisk_get_existence() == Supported) ROOT_IMPL = Magisk;
  else ROOT_IMPL = None;

  switch (ROOT_IMPL) {
    case None: {
      LOGI("No root implementation found.\n");

      break;
    }
    case Multiple: {
      LOGI("Multiple root implementations found.\n");

      break;
    }
    case KernelSU: {
      LOGI("KernelSU root implementation found.\n");

      break;
    }
    case APatch: {
      LOGI("APatch root implementation found.\n");

      break;
    }
    case Magisk: {
      LOGI("Magisk root implementation found.\n");

      break;
    }
  }
}

enum RootImpl get_impl(void) {
  return ROOT_IMPL;
}

bool uid_granted_root(uid_t uid) {
  switch (get_impl()) {
    case KernelSU: {
      return ksu_uid_granted_root(uid);
    }
    case APatch: {
      return apatch_uid_granted_root(uid);
    }
    case Magisk: {
      return magisk_uid_granted_root(uid);
    }
    default: {
      return false;
    }
  }
}

bool uid_should_umount(uid_t uid) {
  switch (get_impl()) {
    case KernelSU: {
      return ksu_uid_should_umount(uid);
    }
    case APatch: {
      return apatch_uid_should_umount(uid);
    }
    case Magisk: {
      return magisk_uid_should_umount(uid);
    }
    default: {
      return false;
    }
  }
}

bool uid_is_manager(uid_t uid) {
  switch (get_impl()) {
    case KernelSU: {
      return ksu_uid_is_manager(uid);
    }
    case APatch: {
      return apatch_uid_is_manager(uid);
    }
    case Magisk: {
      return magisk_uid_is_manager(uid);
    }
    default: {
      return false;
    }
  }
}
