#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdbool.h>
#include <stdint.h>

#define PROCESS_NAME_MAX_LEN 256 + 1

/* INFO: Persistent user settings. Unlike /data/adb/rezygisk, it is not wiped on boot. */
#define NEXTZYGISK_DATA_DIR "/data/adb/nextzygisk"
/* INFO: When present, NextZygisk does not unmount root for apps on the denylist. */
#define UMOUNT_DISABLED_FILE NEXTZYGISK_DATA_DIR "/umount_disabled"

#define ZYGOTE_INJECTED LP_SELECT(5, 4)
#define DAEMON_SET_INFO LP_SELECT(7, 6)
#define DAEMON_SET_ERROR_INFO LP_SELECT(9, 8)

enum DaemonSocketAction {
  ZygoteInjected         = 0,
  GetProcessFlags        = 1,
  GetInfo                = 2,
  ReadModules            = 3,
  RequestCompanionSocket = 4,
  GetModuleDir           = 5,
  ZygoteRestart          = 6,
  UpdateMountNamespace   = 7,
  RemoveModule           = 8
};

enum ProcessFlags: uint32_t {
  PROCESS_GRANTED_ROOT = (1u << 0),
  PROCESS_ON_DENYLIST = (1u << 1),
  PROCESS_IS_MANAGER = (1u << 27),
  PROCESS_ROOT_IS_APATCH = (1u << 28),
  PROCESS_ROOT_IS_KSU = (1u << 29),
  PROCESS_ROOT_IS_MAGISK = (1u << 30),
  PROCESS_IS_FIRST_STARTED = (1u << 31)
};

enum RootImplState {
  Supported,
  TooOld,
  Inexistent,
  Abnormal
};

enum MountNamespaceState {
  Clean,
  Mounted
};

#endif /* CONSTANTS_H */
