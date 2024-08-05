#include <stdlib.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <errno.h>

#include "../constants.h"
#include "../utils.h"

#include "kernelsu.h"

#define KERNEL_SU_OPTION 0xdeadbeef

#define CMD_GET_VERSION 2
#define CMD_UID_GRANTED_ROOT 12
#define CMD_UID_SHOULD_UMOUNT 13

enum RootImplState ksu_get_kernel_su(void) {
  int version = 0;
  prctl(KERNEL_SU_OPTION, CMD_GET_VERSION, &version, 0, 0);

  errno = 0;

  if (version == 0) return Abnormal;

  if (version >= MIN_KSU_VERSION && version <= MAX_KSU_VERSION) return Supported;
  
  if (version >= 1 && version <= MIN_KSU_VERSION - 1) return TooOld;

  return Abnormal;
}

bool ksu_uid_granted_root(uid_t uid) {
  uint32_t result = 0;
  bool granted = false;
  prctl(KERNEL_SU_OPTION, CMD_UID_GRANTED_ROOT, uid, &granted, &result);

  LOGI("ksu_uid_granted_root: %d", granted);

  if (result != KERNEL_SU_OPTION) return false;

  return granted;
}

bool ksu_uid_should_umount(uid_t uid) {
  uint32_t result = 0;
  bool umount = false;
  prctl(KERNEL_SU_OPTION, CMD_UID_SHOULD_UMOUNT, uid, &umount, &result);

  LOGI("ksu_uid_should_umount: %d", umount);

  if (result != KERNEL_SU_OPTION) return false;

  return umount;
}

bool ksu_uid_is_manager(uid_t uid) {

  struct stat s;
  if (stat("/data/user_de/0/me.weishu.kernelsu", &s) == 0) {
    LOGI("ksu_uid_is_manager: %d", uid);

    return s.st_uid == uid;
  }

  LOGI("ksu_uid_is_manager: false");

  return false;
}
