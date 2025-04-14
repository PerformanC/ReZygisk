#ifndef SOLIST_H
#define SOLIST_H

/* INFO: Temporary */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct SoInfo SoInfo;

struct SoInfo {
  char data[0];
};

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
          memory of the list since we will close there, not loading a library creating
          this gap. However, the previously loaded library would remain in the solist,
          requiring ReZygisk to clean those up.

        To do that, we use 2 functions: soinfo_free, and set_size, which will
          zero the region size, and then remove all traces of that library (libzygisk.so)
          which was previously loaded.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker.cpp#1712
*/
bool solist_drop_so_path(const char *target_path);

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
void solist_reset_counters(size_t load, size_t unload);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SOLIST_H */
