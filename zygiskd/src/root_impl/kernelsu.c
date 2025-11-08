#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <sys/syscall.h>

#include "../constants.h"
#include "../utils.h"
#include "common.h"

#include "kernelsu.h"

/* INFO: It would be presumed it is a unsigned int,
           so we need to cast it to signed int to
           avoid any potential UB.
*/
#define KSU_INSTALL_MAGIC1 (int)0xDEADBEEF
#define KSU_INSTALL_MAGIC2 (int)0xCAFEBABE

#define CMD_GET_VERSION 2
#define CMD_UID_GRANTED_ROOT 12
#define CMD_UID_SHOULD_UMOUNT 13
#define CMD_GET_MANAGER_UID 16
#define CMD_HOOK_MODE 0xC0DEAD1A

struct ksu_uid_granted_root_cmd {
  uint32_t uid;
  uint8_t granted;
};

struct ksu_uid_should_umount_cmd {
  uint32_t uid;
  uint8_t should_umount;
};

struct ksu_get_manager_uid_cmd {
  uint32_t uid;
};

#define KSU_IOCTL_UID_GRANTED_ROOT _IOC(_IOC_READ|_IOC_WRITE, 'K', 8, 0)
#define KSU_IOCTL_UID_SHOULD_UMOUNT _IOC(_IOC_READ|_IOC_WRITE, 'K', 9, 0)
#define KSU_IOCTL_GET_MANAGER_UID _IOC(_IOC_READ, 'K', 10, 0)

static enum kernelsu_variants variant = KOfficial;

static int ksu_fd = -1;

static bool supports_manager_uid_retrieval = false;
static bool ksu_uses_new_ksuctl = false;

void ksu_get_existence(struct root_impl_state *state) {
  syscall(SYS_reboot, KSU_INSTALL_MAGIC1, KSU_INSTALL_MAGIC2, 0, (void *)&ksu_fd);
  if (ksu_fd == -1) {
    /* INFO: Perhaps it uses the old ksuctl interface */
    int reply_ok = 0;

    int version = 0;
    prctl((signed int)KSU_INSTALL_MAGIC1, CMD_GET_VERSION, &version, 0, &reply_ok);

    if (version == 0) state->state = Abnormal;
    else if (version >= MIN_KSU_VERSION) {
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
      prctl((signed int)KSU_INSTALL_MAGIC1, CMD_HOOK_MODE, mode, NULL, &reply_ok);

      if (mode[0] != '\0') state->variant = KNext;
      else state->variant = KOfficial;

      variant = state->variant;

      /* INFO: CMD_GET_MANAGER_UID is a KernelSU Next feature, however we won't 
                limit to KernelSU Next only in case other forks wish to implement
                it. */
      prctl((signed int)KSU_INSTALL_MAGIC1, CMD_GET_MANAGER_UID, NULL, NULL, &reply_ok);

      if (reply_ok == KSU_INSTALL_MAGIC1) {
        LOGI("KernelSU implementation supports CMD_GET_MANAGER_UID.\n");

        supports_manager_uid_retrieval = true;
      }
    }
    else if (version >= 1 && version <= MIN_KSU_VERSION - 1) state->state = TooOld;
    else state->state = Abnormal;

    return;
  }

  ksu_uses_new_ksuctl = true;

  state->state = Supported;
}

bool ksu_uid_granted_root(uid_t uid) {
  if (!ksu_uses_new_ksuctl) {
    bool granted = false;
    uint32_t result = 0;
    prctl(KSU_INSTALL_MAGIC1, CMD_UID_GRANTED_ROOT, uid, &granted, &result);

    if ((int)result != KSU_INSTALL_MAGIC1) return false;

    return granted;
  }

  struct ksu_uid_granted_root_cmd cmd = { 
    .uid = uid,
    .granted = 0
  };

  if (ioctl(ksu_fd, KSU_IOCTL_UID_GRANTED_ROOT, &cmd) == -1) {
    LOGE("Failed to ioctl KSU_IOCTL_UID_GRANTED_ROOT: %s\n", strerror(errno));

    return false;
  }

  return cmd.granted;
}

bool ksu_uid_should_umount(uid_t uid) {
  if (!ksu_uses_new_ksuctl) {
    bool should_umount = false;
    uint32_t result = 0;
    prctl(KSU_INSTALL_MAGIC1, CMD_UID_SHOULD_UMOUNT, uid, &should_umount, &result);

    if ((int)result != KSU_INSTALL_MAGIC1) return false;

    return should_umount;
  }

  struct ksu_uid_should_umount_cmd cmd = {
    .uid = uid,
    .should_umount = 0
  };

  if (ioctl(ksu_fd, KSU_IOCTL_UID_SHOULD_UMOUNT, &cmd) == -1) {
    LOGE("Failed to ioctl KSU_IOCTL_UID_SHOULD_UMOUNT: %s\n", strerror(errno));

    return false;
  }

  return cmd.should_umount;
}

bool ksu_uid_is_manager(uid_t uid) {
  /* INFO: If the manager UID is set, we can use it to check if the UID
             is the manager UID, which is more reliable than checking
             the KSU manager data directory, as spoofed builds of
             KernelSU Next have different package names.
  */
  if (!ksu_uses_new_ksuctl) {
    if (supports_manager_uid_retrieval) {
      int reply_ok = 0;

      uid_t manager_uid = 0;
      prctl(KSU_INSTALL_MAGIC1, CMD_GET_MANAGER_UID, &manager_uid, NULL, &reply_ok);

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

  /* INFO: If it uses ioctl, it already has support to get managet UID operation */
  struct ksu_get_manager_uid_cmd cmd;
  if (ioctl(ksu_fd, KSU_IOCTL_GET_MANAGER_UID, &cmd) == -1) {
    LOGE("Failed to ioctl KSU_IOCTL_GET_MANAGER_UID: %s\n", strerror(errno));

    return false;
  }

  return uid == cmd.uid;
}

void ksu_cleanup(void) {
  if (ksu_fd != -1) {
    close(ksu_fd);
    ksu_fd = -1;
  }
}
