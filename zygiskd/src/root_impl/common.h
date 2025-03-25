#ifndef __ROOT_IMPL_COMMON_H__
#define __ROOT_IMPL_COMMON_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

// Forward declarations for root implementation functions
void kernelsu_setup(void);
void apatch_setup(void);
void magisk_setup(void);

// Root implementation types
enum RootImpl {
  None = 0,
  Multiple = 1,
  KernelSU = 2,
  APatch = 3,
  Magisk = 4,
};

// Maximum length of root implementation name
#define LONGEST_ROOT_IMPL_NAME 32

// Root implementation structure
struct root_impl {
  enum RootImpl impl;
  int variant;
};

// Root implementation state structure
struct root_impl_state {
  uint8_t variant;
  int state;
  // Add any other fields needed for state tracking
};

// Setup root implementations
void root_impls_setup(void);

// Get current root implementation
void get_impl(struct root_impl *impl);

// Reset the cached implementation (useful for testing)
void reset_impl_cache(void);

// Check if a UID has been granted root access
bool uid_granted_root(uid_t uid);

// Check if a UID should have mounts unmounted
bool uid_should_umount(uid_t uid);

// Check if a UID is a manager app
bool uid_is_manager(uid_t uid);

// KernelSU specific functions
bool ksu_uid_granted_root(uid_t uid);
bool ksu_uid_should_umount(uid_t uid);
bool ksu_uid_is_manager(uid_t uid);

// APatch specific functions
bool apatch_uid_granted_root(uid_t uid);
bool apatch_uid_should_umount(uid_t uid);
bool apatch_uid_is_manager(uid_t uid);

// Magisk specific functions
bool magisk_uid_granted_root(uid_t uid);
bool magisk_uid_should_umount(uid_t uid);
bool magisk_uid_is_manager(uid_t uid);

#endif /* __ROOT_IMPL_COMMON_H__ */
