#ifndef SOLIST_H
#define SOLIST_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdbool.h>
#include <sys/types.h>

typedef void SoInfo;

#define FuncType(name) void (*name)

struct pdg {
  void *(*ctor)();
  void *(*dtor)();
};

/* 
  INFO: When dlopen'ing a library, the system will save information of the
          opened library so a structure called soinfo, which contains another
          called solist, a list with the information of opened objects.

        Due to special handling in ptracer, however, it won't heave gaps in the
          memory of the list since we will remove the info immediatly after loading
          libzygisk.so, so that it doesn't create gaps between current module info
          and the next (soinfo).

        To do that, we use 2 functions: set_size and dlclose, which will first zero
          zero the size that the linker believes the shared library is, and then dlclose.
          Because the size is 0, it won't munmap the library, allowing us to keep loaded while
          having all other traces removed.

        For the case of modules, which are arbitrary, we won't call dlclose, as it could break
          the module. Instead of using dlclose, we separately call soinfo_free, which will free
          the soinfo structure. That will allow to keep the data initialized by constructors
          mmaped, hence properly dropping most traces without breaking the module.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker.cpp#1712
*/
bool solist_drop_so_path(void *lib_memory);

/*
  INFO: When dlopen'ing a library, the system will increment 1 to a global
          counter that tracks the amount of libraries ever loaded in that process,
          the same happening in dlclose.

        This cannot directly be used to detect if ReZygisk is present, however, with
          enough data about specific environments, this can be used to detect if any
          other library (be it malicious or not) was loaded. To avoid future detections,
          we patch that value to the original value.

        To do that, we retrieve the address of both "g_module_load_counter" and "g_module
          _unload_counter" variables and force set them to the original value, based on
          the modules dlopen'ed.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker.cpp#1874
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker.cpp#1944
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker.cpp#3413
*/
void solist_reset_counters(size_t libs_loaded);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SOLIST_H */
