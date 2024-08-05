#include <stdlib.h>

#include <sys/types.h>
#include <libgen.h>
#include <dlfcn.h>

#define ANDROID_NAMESPACE_TYPE_SHARED 0x2
#define ANDROID_DLEXT_USE_NAMESPACE 0x200

struct AndroidNamespace {
  u_int8_t _unused[0];
};

struct AndroidDlextinfo {
  u_int64_t flags;
  void *reserved_addr;
  size_t reserved_size;
  int relro_fd;
  int library_fd;
  off64_t library_fd_offset;
  struct AndroidNamespace *library_namespace;
};

void *android_dlopen_ext(const char *filename, int flags, const struct AndroidDlextinfo *extinfo);

void *android_dlopen(char *path, u_int32_t flags) {
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

  void *android_create_namespace_fn = dlsym(RTLD_DEFAULT, "__loader_android_create_namespace");

  if (android_create_namespace_fn != NULL) {
    void *ns = ((void *(*)(const char *, const char *, const char *, u_int32_t, void *, void *, void *))android_create_namespace_fn)(
      path,
      dir,
      NULL,
      ANDROID_NAMESPACE_TYPE_SHARED,
      NULL,
      NULL,
      (void *)&android_dlopen
    );

    if (ns != NULL) {
      info.flags = ANDROID_DLEXT_USE_NAMESPACE;
      info.library_namespace = ns;
    }
  }

  return android_dlopen_ext(path, flags, &info);
}
