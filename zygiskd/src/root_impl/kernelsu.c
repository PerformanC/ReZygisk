#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <errno.h>

#include "../constants.h"
#include "../utils.h"
#include "common.h"

#include "kernelsu.h"

/* INFO: It would be presumed it is a unsigned int,
           so we need to cast it to signed int to
           avoid any potential UB.
*/
#define KERNEL_SU_OPTION (int)0xdeadbeef

#define CMD_GET_VERSION 2
#define CMD_UID_GRANTED_ROOT 12
#define CMD_UID_SHOULD_UMOUNT 13
#define CMD_GET_MANAGER_UID 16
#define CMD_HOOK_MODE 0xC0DEAD1A

static enum kernelsu_variants variant = KOfficial;

static bool supports_manager_uid_retrieval = false;

void ksu_get_existence(struct root_impl_state *state) {
  int version = 0;
  prctl((signed int)KERNEL_SU_OPTION, CMD_GET_VERSION, &version, 0, 0);

  if (version == 0) state->state = Abnormal;
  else if (version >= MIN_KSU_VERSION && version <= MAX_KSU_VERSION) {
    /* INFO: Some custom kernels for custom ROMs have pre-installed KernelSU.
            Some users don't want to use KernelSU, but, for example, Magisk.
            This if allows this to happen, as it checks if "ksud" exists,
            which in case it doesn't, it won't be considered as supported. */
    struct stat s;
    if (stat("/data/adb/ksud", &s) == -1) {
      if (errno != ENOENT) {
        LOGE("Failed to stat KSU daemon: %s\n", strerror(errno));
      }
      errno = 0;
      state->state = Abnormal;

      return;
    }

    state->state = Supported;

    char mode[16] = { 0 };
    prctl((signed int)KERNEL_SU_OPTION, CMD_HOOK_MODE, mode, 0, 0);

    if (mode[0] != '\0') state->variant = KNext;
    else state->variant = KOfficial;

    variant = state->variant;

    /* INFO: CMD_GET_MANAGER_UID is a KernelSU Next feature, however we won't 
               limit to KernelSU Next only in case other forks wish to implement
               it. */
    int reply_ok = 0;
    prctl((signed int)KERNEL_SU_OPTION, CMD_GET_MANAGER_UID, 0, 0, &reply_ok);

    if (reply_ok == KERNEL_SU_OPTION) {
      LOGI("KernelSU implementation supports CMD_GET_MANAGER_UID.\n");

      supports_manager_uid_retrieval = true;
    }
  }
  else if (version >= 1 && version <= MIN_KSU_VERSION - 1) state->state = TooOld;
  else state->state = Abnormal;
}

bool ksu_uid_granted_root(uid_t uid) {
  uint32_t result = 0;
  bool granted = false;
  prctl(KERNEL_SU_OPTION, CMD_UID_GRANTED_ROOT, uid, &granted, &result);

  if ((int)result != KERNEL_SU_OPTION) return false;

  return granted;
}

bool ksu_uid_should_umount(uid_t uid) {
  uint32_t result = 0;
  bool umount = false;
  prctl(KERNEL_SU_OPTION, CMD_UID_SHOULD_UMOUNT, uid, &umount, &result);

  if ((int)result != KERNEL_SU_OPTION) return false;

  return umount;
}

bool ksu_uid_is_manager(uid_t uid) {
  /* INFO: If the manager UID is set, we can use it to check if the UID
             is the manager UID, which is more reliable than checking
             the KSU manager data directory, as spoofed builds of
             KernelSU Next have different package names.
  */
  if (supports_manager_uid_retrieval) {
    uid_t manager_uid = 0;
    prctl(KERNEL_SU_OPTION, CMD_GET_MANAGER_UID, &manager_uid, NULL, NULL);

    return uid == manager_uid;
  }

  const char *manager_path = NULL;
  if (variant == KOfficial) manager_path = "/data/user_de/0/me.weishu.kernelsu";
  else if (variant == KNext) manager_path = "/data/user_de/0/com.rifsxd.ksunext";

  struct stat s;
  if (stat(manager_path, &s) == -1) {
    if (errno != ENOENT) {
      LOGE("Failed to stat KSU manager data directory: %s\n", strerror(errno));
    }
    errno = 0;

    return false;
  }

  return s.st_uid == uid;
}
