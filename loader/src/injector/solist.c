/* INFO: This file is written in C99 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "elf_util.h"
#include "logging.h"

#include "solist.h"

#ifdef __LP64__
  size_t solist_size_offset = 0x18;
  size_t solist_next_offset = 0x30;
  size_t solist_realpath_offset = 0x1a8;
#else
  size_t solist_size_offset = 0x90;
  size_t solist_next_offset = 0xa4;
  size_t solist_realpath_offset = 0x174;
#endif

static const char *(*get_realpath_sym)(SoInfo *) = NULL;
static void (*soinfo_free)(SoInfo *) = NULL;

static inline SoInfo *get_next(SoInfo *self) {
  return *(SoInfo **)((uintptr_t)self + solist_next_offset);
}

static inline const char *get_path(SoInfo *self) {
  if (get_realpath_sym)
    return (*get_realpath_sym)(self);

  return ((const char *)((uintptr_t)self + solist_realpath_offset));
}

static inline void set_size(SoInfo *self, size_t size) {
  *(size_t *) ((uintptr_t)self + solist_size_offset) = size;
}

static inline size_t get_size(SoInfo *self) {
  return *(size_t *) ((uintptr_t)self + solist_size_offset);
}

struct pdg ppdg;

static bool pdg_setup(ElfImg *img) {
  ppdg.ctor = (void *(*)())getSymbAddress(img,  "__dl__ZN18ProtectedDataGuardC2Ev");
  ppdg.dtor = (void *(*)())getSymbAddress(img, "__dl__ZN18ProtectedDataGuardD2Ev");

  return ppdg.ctor != NULL && ppdg.dtor != NULL;
}

static void pdg_protect() {
  if (ppdg.ctor != NULL)
    (*(ppdg.ctor))();
}

static void pdg_unprotect() {
  if (ppdg.dtor != NULL)
    (*(ppdg.dtor))();
}

static SoInfo *solist = NULL;
static SoInfo *somain = NULL;
static SoInfo **sonext = NULL;

static uint64_t *g_module_load_counter = NULL;
static uint64_t *g_module_unload_counter = NULL;

static bool solist_init() {
  ElfImg *linker = ElfImg_create("/linker");
  if (linker == NULL) {
    LOGE("Failed to load linker");

    return false;
  }

  ppdg = (struct pdg) {
    .ctor = NULL,
    .dtor = NULL
  };
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
  solist = (SoInfo *)getSymbValueByPrefix(linker, "__dl__ZL6solist");
  if (solist == NULL) {
    LOGE("Failed to find solist __dl__ZL6solist*");

    ElfImg_destroy(linker);

    return false;
  }

  somain = (SoInfo *)getSymbValueByPrefix(linker, "__dl__ZL6somain");
  LOGI("%p is somain", (void *)somain);
  if (somain == NULL) {
    LOGE("Failed to find somain __dl__ZL6somain*");

    ElfImg_destroy(linker);

    return false;
  }

  sonext = (SoInfo **)getSymbAddressByPrefix(linker, "__dl__ZL6sonext");
  if (sonext == NULL) {
    LOGE("Failed to find sonext __dl__ZL6sonext*");

    ElfImg_destroy(linker);

    return false;
  }

  SoInfo *vdso = (SoInfo *)getSymbValueByPrefix(linker, "__dl__ZL4vdso");
  if (vdso == NULL) {
    LOGE("Failed to find vsdo __dl__ZL4vdso*");

    ElfImg_destroy(linker);

    return false;
  }

  get_realpath_sym = (const char *(*)(SoInfo *))getSymbAddress(linker, "__dl__ZNK6soinfo12get_realpathEv");
  if (get_realpath_sym == NULL) {
    LOGE("Failed to find get_realpath __dl__ZNK6soinfo12get_realpathEv");

    ElfImg_destroy(linker);

    return false;
  }

  soinfo_free = (void (*)(SoInfo *))getSymbAddressByPrefix(linker, "__dl__ZL11soinfo_freeP6soinfo");
  if (soinfo_free == NULL) {
    LOGE("Failed to find soinfo_free __dl__ZL11soinfo_freeP6soinfo*");

    ElfImg_destroy(linker);

    return false;
  }

  g_module_load_counter = (uint64_t *)getSymbAddress(linker, "__dl__ZL21g_module_load_counter");
  if (g_module_load_counter != NULL) LOGD("found symbol g_module_load_counter");

  g_module_unload_counter = (uint64_t *)getSymbAddress(linker, "__dl__ZL23g_module_unload_counter");
  if (g_module_unload_counter != NULL) LOGD("found symbol g_module_unload_counter");

  for (size_t i = 0; i < 1024 / sizeof(void *); i++) {
    uintptr_t possible_field = (uintptr_t)solist + i * sizeof(void *);
    size_t possible_size_of_somain = *(size_t *)((uintptr_t)somain + i * sizeof(void *));

    if (possible_size_of_somain < 0x100000 && possible_size_of_somain > 0x100) {
      solist_size_offset = i * sizeof(void *);

      LOGD("solist_size_offset is %zu * %zu = %p", i, sizeof(void *), (void *)solist_size_offset);
    }

    if (*(void **)possible_field == somain || (vdso != NULL && *(void **)possible_field == vdso)) {
      solist_next_offset = i * sizeof(void *);
      LOGD("solist_next_offset is %zu * %zu = %p", i, sizeof(void *), (void *)solist_next_offset);

      break;
    }
  }

  ElfImg_destroy(linker);

  return true;
}

bool solist_drop_so_path(const char *target_path) {
  if (solist == NULL && !solist_init()) {
    LOGE("Failed to initialize solist");

    return false;
  }

  for (SoInfo *iter = solist; iter; iter = get_next(iter)) {
    if (get_path(iter) && strstr(get_path(iter), target_path)) {
      pdg_protect();

      LOGV("dropping solist record loaded at %s with size %zu", get_path(iter), get_size(iter));
      if (get_size(iter) > 0) {
        set_size(iter, 0);
        soinfo_free(iter);

        pdg_unprotect();

        return true;
      }

      pdg_unprotect();
    }
  }

  return false;
}

void solist_reset_counters(size_t load, size_t unload) {
  if (solist == NULL && !solist_init()) {
    LOGE("Failed to initialize solist");

    return;
  }

  if (g_module_load_counter == NULL || g_module_unload_counter == NULL) {
    LOGD("g_module counters not defined, skip reseting them");

    return;
  }

  uint64_t loaded_modules = *g_module_load_counter;
  uint64_t unloaded_modules = *g_module_unload_counter;

  if (loaded_modules >= load) {
    *g_module_load_counter = loaded_modules - load;

    LOGD("reset g_module_load_counter to %zu", (size_t) *g_module_load_counter);
  }

  if (unloaded_modules >= unload) {
    *g_module_unload_counter = unloaded_modules - unload;

    LOGD("reset g_module_unload_counter to %zu", (size_t) *g_module_unload_counter);
  }
}
