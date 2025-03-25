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
#include <sys/system_properties.h>

#include "../utils.h"
#include "common.h"

// Undefine LOG_TAG before redefining it
#undef LOG_TAG
#define LOG_TAG "ReZygisk-RootImpl"

// Mutex to protect concurrent access to root implementation
static pthread_mutex_t root_impl_mutex = PTHREAD_MUTEX_INITIALIZER;

// Cache the root implementation to avoid repeated checks
static struct root_impl cached_impl = { .impl = None };
static int impl_initialized = 0;

// Function to safely check if a file exists with proper error handling
static bool file_exists(const char *path) {
  if (!path) return false;
  
  struct stat st;
  if (stat(path, &st) == 0) {
    return true;
  }
  
  // Only log if it's not a simple "file not found" error
  if (errno != ENOENT) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Failed to stat %s: %s\n", path, strerror(errno));
  }
  
  return false;
}

// Optimized implementation detection with caching
void get_impl(struct root_impl *impl) {
  if (!impl) return;
  
  // Use cached implementation if available
  pthread_mutex_lock(&root_impl_mutex);
  if (impl_initialized) {
    *impl = cached_impl;
    pthread_mutex_unlock(&root_impl_mutex);
    return;
  }
  
  // Initialize to None by default
  impl->impl = None;
  impl->variant = 0;
  
  bool has_ksu = file_exists("/sys/module/kernelsu");
  bool has_apatch = file_exists("/data/adb/ap");
  bool has_magisk = file_exists("/data/adb/magisk");
  
  int count = 0;
  
  if (has_ksu) {
    impl->impl = KernelSU;
    count++;
  }
  
  if (has_apatch) {
    impl->impl = APatch;
    count++;
  }
  
  if (has_magisk) {
    impl->impl = Magisk;
    count++;
    
    // Check for Magisk variant (not critical, so don't error if it fails)
    char prop[PROP_VALUE_MAX];
    __system_property_get("ro.magisk.version", prop);
    
    if (strstr(prop, "kitsune") != NULL) {
      impl->variant = 1; // Kitsune variant
    } else {
      impl->variant = 0; // Official variant
    }
  }
  
  if (count > 1) {
    impl->impl = Multiple;
  }
  
  // Cache the result
  cached_impl = *impl;
  impl_initialized = 1;
  pthread_mutex_unlock(&root_impl_mutex);
}

// Reset the cached implementation (useful for testing or after system changes)
void reset_impl_cache(void) {
  pthread_mutex_lock(&root_impl_mutex);
  impl_initialized = 0;
  pthread_mutex_unlock(&root_impl_mutex);
}

// Optimized setup function with better resource management
void root_impls_setup(void) {
  struct root_impl impl;
  get_impl(&impl);
  
  // Log the detected implementation
  char impl_name[LONGEST_ROOT_IMPL_NAME];
  stringify_root_impl_name(impl, impl_name);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Detected root implementation: %s\n", impl_name);
  
  // Setup the appropriate implementation
  switch (impl.impl) {
    case KernelSU:
      kernelsu_setup();
      break;
    case APatch:
      apatch_setup();
      break;
    case Magisk:
      magisk_setup();
      break;
    case Multiple:
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Multiple root implementations detected, using priority order\n");
      // Try each implementation in order of preference
      if (file_exists("/sys/module/kernelsu")) {
        kernelsu_setup();
      } else if (file_exists("/data/adb/ap")) {
        apatch_setup();
      } else if (file_exists("/data/adb/magisk")) {
        magisk_setup();
      }
      break;
    case None:
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "No root implementation detected\n");
      break;
  }
}

bool uid_granted_root(uid_t uid) {
  struct root_impl impl;
  get_impl(&impl);
  
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
  struct root_impl impl;
  get_impl(&impl);
  
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
  struct root_impl impl;
  get_impl(&impl);
  
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
