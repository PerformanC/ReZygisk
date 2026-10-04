#include "common.h"

#include <string.h>
#include <errno.h>

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "../utils.h"
#include "apatch.h"
#include "kernelsu.h"
#include "magisk.h"

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
    impl.variant = state_ksu.variant;
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
      LOGI("Magisk root implementation found.\n");

      break;
    }
  }
}

void get_impl(struct root_impl *uimpl) {
  *uimpl = impl;
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

bool uid_should_umount(uid_t uid, const char *const process) {
  switch (impl.impl) {
    case KernelSU: {
      return ksu_uid_should_umount(uid);
    }
    case APatch: {
      return apatch_uid_should_umount(uid, process);
    }
    case Magisk: {
      return magisk_uid_should_umount(process);
    }
    default: {
      return false;
    }
  }
}

/* INFO: The manager package is looked up on every app launch. Remember where it was
           found, so the next lookups cost a single stat() instead of a full scan.
           The stat also follows reinstalls (new UID) and uninstalls (rescan). */
static char cached_pkg[NAME_MAX + 1] = { 0 };
static char cached_pkg_path[PATH_MAX] = { 0 };

uid_t uid_from_pkg(const char *restrict pkg) {
  if (cached_pkg[0] != '\0' && strcmp(cached_pkg, pkg) == 0) {
    struct stat cached_st;
    if (stat(cached_pkg_path, &cached_st) == 0) return APP_ID(cached_st.st_uid);

    cached_pkg[0] = '\0';
  }

  DIR *dir = opendir("/data/user");
  if (!dir) {
    LOGE("Failed to opendir /data/user: %s", strerror(errno));

    /* INFO: In Android system, an app cannot have UID 0, hence it's safe to utilize it as a fail signal.

      SOURCES:
       - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-17.0.0_r1/services/core/java/com/android/server/pm/PackageManagerService.java#2100
    */
    return 0;
  }

  struct dirent *entry; struct stat st;
  bool found = false;
  while ((entry = readdir(dir)) != NULL) {
    /* INFO: Skip "." and ".." */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

    char stat_path[PATH_MAX];
    snprintf(stat_path, sizeof(stat_path), "/data/user_de/%s/%s", entry->d_name, pkg);

    if (stat(stat_path, &st) != 0) {
      if (errno != ENOENT) {
        LOGE("Failed to stat manager path %s with %s", stat_path, strerror(errno));
      }

      continue;
    }

    found = true;

    snprintf(cached_pkg, sizeof(cached_pkg), "%s", pkg);
    snprintf(cached_pkg_path, sizeof(cached_pkg_path), "%s", stat_path);

    break;
  }

  closedir(dir);

  /* INFO: Without a match `st` is uninitialized, never turn garbage into a manager UID. */
  return found ? APP_ID(st.st_uid) : 0;
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

void root_impl_cleanup(void) {
  if (impl.impl == KernelSU) ksu_cleanup();
}
