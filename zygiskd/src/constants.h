#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <android/log.h>

#define bool _Bool
#define true 1
#define false 0

#if DEBUG == false
  #define MAX_LOG_LEVEL ANDROID_LOG_VERBOSE
#else
  #define MAX_LOG_LEVEL ANDROID_LOG_INFO
#endif

#if (defined(__LP64__) || defined(_LP64))
  #define lp_select(a, b) b
#else
  #define lp_select(a, b) a
#endif

#define ZYGOTE_INJECTED lp_select(5, 4)
#define DAEMON_SET_INFO lp_select(7, 6)
#define DAEMON_SET_ERROR_INFO lp_select(9, 8)
#define SYSTEM_SERVER_STARTED 10

enum DaemonSocketAction {
  PingHeartbeat          = 0,
  RequestLogcatFd        = 1,
  GetProcessFlags        = 2,
  GetInfo                = 3,
  ReadModules            = 4,
  RequestCompanionSocket = 5,
  GetModuleDir           = 6,
  ZygoteRestart          = 7,
  SystemServerStarted    = 8,
  GetCleanNamespace      = 9
};

enum ProcessFlags: uint32_t {
  PROCESS_GRANTED_ROOT = (1u << 0),
  PROCESS_ON_DENYLIST = (1u << 1),
  PROCESS_IS_MANAGER = (1u << 28),
  PROCESS_ROOT_IS_APATCH = (1u << 27),
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
  Rooted,
  Module
};

#endif /* CONSTANTS_H */
