#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "../utils.h"
#include "kernelsu.h"
#include "apatch.h"
#include "magisk.h"

#include "common.h"

static struct root_impl impl;

void root_impls_setup(void) {
  struct root_impl_state state_ksu;
  ksu_get_existence(&state_ksu);

  struct root_impl_state state_apatch;
  apatch_get_existence(&state_apatch);

  struct root_impl_state state_magisk;
  magisk_get_existence(&state_magisk);

  /* INFO: Check if it's only one supported, if not, it's multile and that's bad.
            Remember that true here is equal to the integer 1. */
  if ((state_ksu.state == Supported ? 1 : 0) + (state_apatch.state == Supported ? 1 : 0) + (state_magisk.state == Supported ? 1 : 0) >= 2) {
    impl.impl = Multiple;
  } else if (state_ksu.state == Supported) {
    impl.impl = KernelSU;
  } else if (state_apatch.state == Supported) {
    impl.impl = APatch;
  } else if (state_magisk.state == Supported) {
    impl.impl = Magisk;
    impl.variant = state_magisk.variant;
  } else {
    impl.impl = None;
  }

  switch (impl.impl) {
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
      if (state_magisk.variant == 0) {
        LOGI("Magisk Official root implementation found.\n");
      } else {
        LOGI("Magisk Kitsune root implementation found.\n");
      }

      break;
    }
  }
}

void get_impl(struct root_impl *uimpl) {
  uimpl->impl = impl.impl;
  uimpl->variant = impl.variant;
}

bool uid_granted_root(uid_t uid) {
  switch (impl.impl) {
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
  switch (impl.impl) {
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
  switch (impl.impl) {
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
