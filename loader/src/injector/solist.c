#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>

#include <linux/limits.h>

#include "elf_util.h"
#include "logging.h"

#include "solist.h"

bool heuristics_done = false;

/* INFO: This is useless. Heuristics should work fine and replace it, but it should
           be the same. In case of the unlikely event of heuristics failing, this might
           make it not be completely broken.

         It is generated from Android 15 linker source, in a self contained standalone
           code to ensure it is correct, considering alignment and padding.
*/
#ifdef __LP64__
  size_t solist_size_offset = 0x18;
#else
  size_t solist_size_offset = 0xc;
#endif

static SoInfo *(*find_containing_library)(const void *) = NULL;

/* INFO: Protected Data Guard related code */
struct pdg ppdg = { 0 };

static bool pdg_setup(ElfImg *img) {
  /* INFO: Systems with both utilize *2, and the ones that lack *2, use *1. */
  ppdg.ctor = (void *(*)())getSymbAddress(img,  "__dl__ZN18ProtectedDataGuardC2Ev");
  if (ppdg.ctor == NULL) ppdg.ctor = (void *(*)())getSymbAddress(img, "__dl__ZN18ProtectedDataGuardC1Ev");

  ppdg.dtor = (void *(*)())getSymbAddress(img, "__dl__ZN18ProtectedDataGuardD2Ev");
  if (ppdg.dtor == NULL) ppdg.dtor = (void *(*)())getSymbAddress(img, "__dl__ZN18ProtectedDataGuardD1Ev");

  return ppdg.ctor != NULL && ppdg.dtor != NULL;
}

/* INFO: Allow data to be written to the areas. */
static void pdg_unprotect() {
  (*ppdg.ctor)();
}

/* INFO: Block write and only allow read access to the areas. */
static void pdg_protect() {
  (*ppdg.dtor)();
}

static size_t *g_module_load_counter = NULL;
static size_t *g_module_unload_counter = NULL;

static bool solist_init() {
  #ifdef __LP64__
    ElfImg *linker = ElfImg_create("/system/bin/linker64", NULL);
  #else
    ElfImg *linker = ElfImg_create("/system/bin/linker", NULL);
  #endif
  if (linker == NULL) {
    LOGE("Failed to load linker");

    return false;
  }

  if (!pdg_setup(linker)) {
    LOGE("Failed to setup pdg");

    ElfImg_destroy(linker);

    return false;
  }

  /* INFO: Since Android 15, the symbol names for the linker have a suffix,
              this makes it impossible to hardcode the symbol names. To allow
              this to work on all versions, we need to iterate over the loaded
              symbols and find the correct ones.

      See #63 for more information.
  */
  SoInfo *somain = getSymbValueByPrefix(linker, "__dl__ZL6somain");
  if (somain == NULL) {
    LOGE("Failed to find somain __dl__ZL6somain*");

    ElfImg_destroy(linker);

    return false;
  }

  LOGD("%p is somain", (void *)somain);

  find_containing_library = (SoInfo *(*)(const void *))getSymbAddress(linker, "__dl__Z23find_containing_libraryPKv");
  if (find_containing_library == NULL) {
    LOGE("Failed to find find_containing_library __dl__Z23find_containing_libraryPKv");

    goto solist_init_error;
  }

  LOGD("%p is find_containing_library", (void *)find_containing_library);

  g_module_load_counter = (size_t *)getSymbAddress(linker, "__dl__ZL21g_module_load_counter");
  if (g_module_load_counter != NULL) LOGD("found symbol g_module_load_counter");

  g_module_unload_counter = (size_t *)getSymbAddress(linker, "__dl__ZL23g_module_unload_counter");
  if (g_module_unload_counter != NULL) LOGD("found symbol g_module_unload_counter");

  ElfImg_destroy(linker);

  for (size_t i = 0; i < 1024 / sizeof(void *); i++) {
    size_t possible_size_of_somain = *(size_t *)((uintptr_t)somain + i * sizeof(void *));

    if (possible_size_of_somain < 0x100000 && possible_size_of_somain > 0x100) {
      solist_size_offset = i * sizeof(void *);

      LOGD("solist_size_offset is %zu * %zu = %p", i, sizeof(void *), (void *)solist_size_offset);

      break;
    }
  }

  return true;

  solist_init_error:
    ElfImg_destroy(linker);

  heuristics_done = true;

  return false;
}

/* INFO: find_containing_library returns the SoInfo for the library that contains
           that memory inside its limits, hence why named "lib_memory" in ReZygisk. */
bool solist_drop_so_path(void *lib_memory) {
  if (!heuristics_done && !solist_init()) {
    LOGE("Failed to initialize solist");

    return false;
  }

  SoInfo *found = (*find_containing_library)(lib_memory);
  if (found == NULL) {
    LOGD("Could not find containing library for %p", lib_memory);

    return false;
  }

  /* INFO: This area is guarded. Must unprotect first. */
  pdg_unprotect();
  /* INFO: Bypass linker munmap by setting size to 0 */
  *(size_t *) ((uintptr_t)found + solist_size_offset) = 0;
  pdg_protect();

  LOGD("Set libzygisk.so [%p] size to 0", (void *)found);

  /* INFO: We know, as libzygisk.so, our limits. Because of that, we will
             ignore the numerous limitations that not handling other structures
             that are finalized with dlclose lead up to, so that the code can
             be simpler. */
  if (dlclose(found) == -1) {
    LOGE("Failed to dlclose libzygisk.so: %s", dlerror());

    return false;
  }

  LOGD("Successfully hidden soinfo traces for libzygisk.so");

  return true;
}

void solist_reset_counters(size_t libs_loaded) {
  if (!heuristics_done && !solist_init()) {
    LOGE("Failed to initialize solist");

    return;
  }

  if (g_module_load_counter == NULL || g_module_unload_counter == NULL) {
    LOGD("g_module counters not defined, skip reseting them");

    return;
  }

  size_t loaded_modules = *g_module_load_counter;
  size_t unloaded_modules = *g_module_unload_counter;

  if ((int)loaded_modules - (int)libs_loaded >= 0) {
    *g_module_load_counter -= libs_loaded;

    LOGD("reset g_module_load_counter to %zu", *g_module_load_counter);
  }

  if ((int)unloaded_modules - (int)libs_loaded >= 0) {
    *g_module_unload_counter -= libs_loaded;

    LOGD("reset g_module_unload_counter to %zu", *g_module_unload_counter);
  }
}
