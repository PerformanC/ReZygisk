#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <android/log.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>
#include <linux/userfaultfd.h>
#include <linux/memfd.h>
#include <linux/oom.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <pwd.h>
#include <stdbool.h>
#include <dirent.h>
#include <linux/limits.h>

#include "root_impl/common.h"
#include "constants.h"
#include "companion.h"
#include "zygiskd.h"

#include "utils.h"

#define LOG_TAG "ReZygisk"

int __android_log_print(int prio, const char *tag, const char *fmt, ...);

// Signal handler for graceful shutdown
static volatile sig_atomic_t running = 1;

static void signal_handler(int sig) {
  LOGI("Received signal %d, shutting down gracefully\n", sig);
  running = 0;
}

// Setup signal handlers for proper cleanup on termination
static void setup_signal_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  
  // Handle termination signals for graceful shutdown
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  
  // Ignore SIGPIPE to prevent crashes when writing to closed sockets
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);
}

// Set process priority to reduce battery usage
static void optimize_process_priority(void) {
  // Set CPU scheduling policy to reduce battery usage
  if (!set_low_priority()) {
    LOGE("Failed to set low priority, continuing anyway\n");
  }
  
  // Set I/O priority to lowest
  #ifdef __NR_ioprio_set
  syscall(__NR_ioprio_set, 1, 0, 7 | (3 << 13)); // Class: IDLE, priority: 7 (lowest)
  #endif
  
  // Set memory usage limits
  struct rlimit rlim;
  rlim.rlim_cur = 100 * 1024 * 1024; // 100MB soft limit
  rlim.rlim_max = 150 * 1024 * 1024; // 150MB hard limit
  if (setrlimit(RLIMIT_AS, &rlim) == -1) {
    LOGE("Failed to set memory limits: %s\n", strerror(errno));
  }
}

int main(int argc, char *argv[]) {
  #ifdef __LP64__
    LOGI("Welcome to ReZygisk %s Zygiskd64!\n", ZKSU_VERSION);
  #else
    LOGI("Welcome to ReZygisk %s Zygiskd32!\n", ZKSU_VERSION);
  #endif

  // Setup signal handlers for graceful shutdown
  setup_signal_handlers();
  
  // Optimize process priority to reduce battery usage
  optimize_process_priority();

  if (argc > 1) {
    if (strcmp(argv[1], "companion") == 0) {
      if (argc < 3) {
        LOGI("Usage: zygiskd companion <fd>\n");
        return 1;
      }

      int fd = atoi(argv[2]);
      if (fd < 0) {
        LOGE("Invalid file descriptor: %d\n", fd);
        return 1;
      }
      
      // Set companion process to low priority
      set_low_priority();
      
      companion_entry(fd);
      return 0;
    }

    else if (strcmp(argv[1], "version") == 0) {
      LOGI("ReZygisk Daemon %s\n", ZKSU_VERSION);
      return 0;
    }

    else if (strcmp(argv[1], "root") == 0) {
      root_impls_setup();

      struct root_impl impl;
      get_impl(&impl);

      char impl_name[LONGEST_ROOT_IMPL_NAME];
      stringify_root_impl_name(impl, impl_name);

      LOGI("Root implementation: %s\n", impl_name);
      return 0;
    }

    else {
      LOGI("Usage: zygiskd [companion|version|root]\n");
      return 0;
    }
  }

  // Create a directory for temporary files if it doesn't exist
  mkdir("/data/adb/rezygisk", 0755);
  
  if (switch_mount_namespace((pid_t)1) == false) {
    LOGE("Failed to switch mount namespace\n");
    return 1;
  }
  
  root_impls_setup();
  
  // Periodically check memory usage
  pthread_t memory_monitor_thread;
  pthread_create(&memory_monitor_thread, NULL, (void *(*)(void *))check_memory_usage, NULL);
  pthread_detach(memory_monitor_thread);
  
  // Start the zygiskd daemon
  zygiskd_start(argv);

  return 0;
}
