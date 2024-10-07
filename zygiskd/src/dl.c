#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#include <android/log.h>

#include "companion.h"
#include "dl.h"
#include "utils.h"

#define ANDROID_NAMESPACE_TYPE_SHARED 0x2
#define ANDROID_DLEXT_USE_NAMESPACE 0x200

typedef struct AndroidNamespace {
  unsigned char _unused[0];
} AndroidNamespace;

typedef struct AndroidDlextinfo {
  uint64_t flags;
  void *reserved_addr;
  size_t reserved_size;
  int relro_fd;
  int library_fd;
  off64_t library_fd_offset;
  AndroidNamespace *library_namespace;
} AndroidDlextinfo;

extern void *android_dlopen_ext(const char *filename, int flags, const AndroidDlextinfo *extinfo);

typedef AndroidNamespace *(*AndroidCreateNamespaceFn)(
  const char *name,
  const char *ld_library_path,
  const char *default_library_path,
  uint64_t type,
  const char *permitted_when_isolated_path,
  AndroidNamespace *parent,
  const void *caller_addr
);

void *android_dlopen(char *path, int flags) {
  char *dir = dirname(path);
  struct AndroidDlextinfo info = {
    .flags = 0,
    .reserved_addr = NULL,
    .reserved_size = 0,
    .relro_fd = 0,
    .library_fd = 0,
    .library_fd_offset = 0,
    .library_namespace = NULL,
  };

  void *handle = dlsym(RTLD_DEFAULT, "__loader_android_create_namespace");
  AndroidCreateNamespaceFn android_create_namespace_fn = (AndroidCreateNamespaceFn)handle;

  AndroidNamespace *ns = android_create_namespace_fn(
    path,
    dir,
    NULL,
    ANDROID_NAMESPACE_TYPE_SHARED,
    NULL,
    NULL,
    (const void *)&android_dlopen
  );

  if (ns != NULL) {
    info.flags = ANDROID_DLEXT_USE_NAMESPACE;
    info.library_namespace = ns;

    LOGI("Open %s with namespace %p\n", path, (void *)ns);
  } else {
    LOGI("Cannot create namespace for %s\n", path);
  }

  void *result = android_dlopen_ext(path, flags, &info);
  if (result == NULL) {
    LOGE("Failed to dlopen %s: %s\n", path, dlerror());
  }

  return result;
}
