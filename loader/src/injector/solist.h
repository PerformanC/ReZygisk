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

struct soinfo_deconstructor {
  void (*fini_func)();
  size_t fini_array_size;
  void (**fini_array)();
};

struct soinfo_gap {
  void *start;
  size_t size;
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
bool solist_drop_so_path(void *lib_memory, bool unload);

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

/*
  INFO: Loaded libraries have several mappings to load the different parts of their ELF
          in the memory. Some libraries might include a padding (gap) in case it is extended.

        This function deals with both those jobs, and is purely here for organization, as it
          is simple.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker.cpp#333
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_phdr.cpp#584
*/
void solist_unload_lib(struct soinfo_gap *gap, void *base, size_t size);


/*
  INFO: Helper function to get the size of the mappings of a loaded library.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#171
*/
ssize_t solist_get_size(void *lib_memory);

/*
  INFO: Helper function to get the base of the loaded library, or, in other words
          the start of the first mapping of the loaded library.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#170
*/
void *solist_get_base(void *lib_memory);

/*
  INFO: Helper function to get the callback to the loaded library deconstructors (fini).

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#219
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#220
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#222
*/
struct soinfo_deconstructor solist_get_deconstructors(void *lib_memory);

/*
  INFO: Helper function to get the information of the padding/gap.

  SOURCES:
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#450
   - https://android.googlesource.com/platform/bionic/+/refs/heads/android15-release/linker/linker_soinfo.h#451
*/
struct soinfo_gap solist_get_gap_info(void *lib_memory);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SOLIST_H */
