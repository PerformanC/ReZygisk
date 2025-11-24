#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdbool.h>

#include <android/log.h>

#if (defined(__LP64__) || defined(_LP64))
  #define lp_select(a, b) b
#else
  #define lp_select(a, b) a
#endif

#define PROCESS_NAME_MAX_LEN 256 + 1

#define ZYGOTE_INJECTED lp_select(5, 4)
#define DAEMON_SET_INFO lp_select(7, 6)
#define DAEMON_SET_ERROR_INFO lp_select(9, 8)
#define SYSTEM_SERVER_STARTED 10

enum DaemonSocketAction {
  PingHeartbeat          = 0,
  GetProcessFlags        = 1,
  GetInfo                = 2,
  ReadModules            = 3,
  RequestCompanionSocket = 4,
  GetModuleDir           = 5,
  ZygoteRestart          = 6,
  SystemServerStarted    = 7,
  UpdateMountNamespace   = 8
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
