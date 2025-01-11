//
// Original from https://github.com/LSPosed/NativeDetector/blob/master/app/src/main/jni/solist.cpp
//
#pragma once

#include <string>
#include "elf_util.h"
#include "logging.h"

namespace SoList  {
  class SoInfo {
    public:
      #ifdef __LP64__
        inline static size_t solist_size_offset = 0x18;
        inline static size_t solist_next_offset = 0x28;
        constexpr static size_t solist_realpath_offset = 0x1a8;
      #else
        inline static size_t solist_size_offset = 0x90;
        inline static size_t solist_next_offset = 0xa4;
        constexpr static size_t solist_realpath_offset = 0x174;
      #endif

      inline static const char *(*get_realpath_sym)(SoInfo *) = NULL;
      inline static const char *(*get_soname_sym)(SoInfo *) = NULL;
      inline static void (*soinfo_free)(SoInfo *) = NULL;

      inline SoInfo *get_next() {
        return *(SoInfo **) ((uintptr_t) this + solist_next_offset);
      }

      inline size_t get_size() {
        return *(size_t *) ((uintptr_t) this + solist_size_offset);
      }

      inline const char *get_path() {
        if (get_realpath_sym) return get_realpath_sym(this);

        return ((std::string *) ((uintptr_t) this + solist_realpath_offset))->c_str();
      }

      inline const char *get_name() {
        if (get_soname_sym) return get_soname_sym(this);

        return ((std::string *) ((uintptr_t) this + solist_realpath_offset - sizeof(void *)))->c_str();
      }

      void set_next(SoInfo *si) {
        *(SoInfo **) ((uintptr_t) this + solist_next_offset) = si;
      }

      void set_size(size_t size) {
        *(size_t *) ((uintptr_t) this + solist_size_offset) = size;
      }
  };

  class ProtectedDataGuard {
    public:
      ProtectedDataGuard() {
        if (ctor != nullptr)
          (this->*ctor)();
      }

      ~ProtectedDataGuard() {
        if (dtor != nullptr)
          (this->*dtor)();
      }

      static bool setup(const SandHook::ElfImg &linker) {
        ctor = MemFunc{.data = {.p = reinterpret_cast<void *>(linker.getSymbAddress(
                                    "__dl__ZN18ProtectedDataGuardC2Ev")), .adj = 0}}.f;
        dtor = MemFunc{.data = {.p = reinterpret_cast<void *>(linker.getSymbAddress(
                                    "__dl__ZN18ProtectedDataGuardD2Ev")), .adj = 0}}.f;
        return ctor != nullptr && dtor != nullptr;
      }

      ProtectedDataGuard(const ProtectedDataGuard &) = delete;

      void operator=(const ProtectedDataGuard &) = delete;

    private:
      using FuncType = void (ProtectedDataGuard::*)();

      inline static FuncType ctor = NULL;
      inline static FuncType dtor = NULL;

      union MemFunc {
        FuncType f;

        struct {
          void *p;
          std::ptrdiff_t adj;
        } data;
      };
  };


  static SoInfo *solist = NULL;
  static SoInfo *somain = NULL;
  static SoInfo **sonext = NULL;

  static uint64_t *g_module_load_counter = NULL;
  static uint64_t *g_module_unload_counter = NULL;

  static bool Initialize();

  template<typename T>
  inline T *getStaticPointer(const SandHook::ElfImg &linker, const char *name) {
    auto *addr = reinterpret_cast<T **>(linker.getSymbAddress(name));

    return addr == NULL ? NULL : *addr;
  }

  static bool DropSoPath(const char* target_path) {
    bool path_found = false;
    if (solist == NULL && !Initialize()) {
      LOGE("Failed to initialize solist");
      return path_found;
    }
    for (auto iter = solist; iter; iter = iter->get_next()) {
      if (iter->get_name() && iter->get_path() && strstr(iter->get_path(), target_path)) {
        SoList::ProtectedDataGuard guard;
        LOGV("dropping solist record for %s loaded at %s with size %zu", iter->get_name(), iter->get_path(), iter->get_size());
        if (iter->get_size() > 0) {
            iter->set_size(0);
            SoInfo::soinfo_free(iter);
            path_found = true;
        }
      }
    }
    return path_found;
  }

  static void ResetCounters(size_t load, size_t unload) {
    if (solist == NULL && !Initialize()) {
      LOGE("Failed to initialize solist");
      return;
    }
    if (g_module_load_counter == NULL || g_module_unload_counter == NULL) {
      LOGD("g_module counters not defined, skip reseting them");
      return;
    }
    auto loaded_modules = *g_module_load_counter;
    auto unloaded_modules = *g_module_unload_counter;
    if (loaded_modules >= load) {
      *g_module_load_counter = loaded_modules - load;
      LOGD("reset g_module_load_counter to %zu", (size_t) *g_module_load_counter);
    }
    if (unloaded_modules >= unload) {
      *g_module_unload_counter = unloaded_modules - unload;
      LOGD("reset g_module_unload_counter to %zu", (size_t) *g_module_unload_counter);
    }
  }

  static bool Initialize() {
    SandHook::ElfImg linker("/linker");
    if (!ProtectedDataGuard::setup(linker)) return false;
    LOGD("found symbol ProtectedDataGuard");

    /* INFO: Since Android 15, the symbol names for the linker have a suffix,
                this makes it impossible to hardcode the symbol names. To allow
                this to work on all versions, we need to iterate over the loaded
                symbols and find the correct ones.

        See #63 for more information.
    */

    std::string_view solist_sym_name = linker.findSymbolNameByPrefix("__dl__ZL6solist");
    if (solist_sym_name.empty()) return false;
    LOGD("found symbol name %s", solist_sym_name.data());

    std::string_view soinfo_free_name = linker.findSymbolNameByPrefix("__dl__ZL11soinfo_freeP6soinfo");
    if (soinfo_free_name.empty()) return false;
    LOGD("found symbol name %s", soinfo_free_name.data());

    /* INFO: The size isn't a magic number, it's the size for the string: .llvm.7690929523238822858 */
    char llvm_sufix[25 + 1];

    if (solist_sym_name.length() != strlen("__dl__ZL6solist")) {
      strncpy(llvm_sufix, solist_sym_name.data() + strlen("__dl__ZL6solist"), sizeof(llvm_sufix));
    } else {
      llvm_sufix[0] = '\0';
    }

    solist = getStaticPointer<SoInfo>(linker, solist_sym_name.data());
    if (solist == NULL) return false;
    LOGD("found symbol solist");

    char somain_sym_name[sizeof("__dl__ZL6somain") + sizeof(llvm_sufix)];
    snprintf(somain_sym_name, sizeof(somain_sym_name), "__dl__ZL6somain%s", llvm_sufix);

    char sonext_sym_name[sizeof("__dl__ZL6sonext") + sizeof(llvm_sufix)];
    snprintf(sonext_sym_name, sizeof(somain_sym_name), "__dl__ZL6sonext%s", llvm_sufix);

    char vdso_sym_name[sizeof("__dl__ZL4vdso") + sizeof(llvm_sufix)];
    snprintf(vdso_sym_name, sizeof(vdso_sym_name), "__dl__ZL4vdso%s", llvm_sufix);

    somain = getStaticPointer<SoInfo>(linker, somain_sym_name);
    if (somain == NULL) return false;
    LOGD("found symbol somain");

    sonext = linker.getSymbAddress<SoInfo **>(sonext_sym_name);
    if (sonext == NULL) return false;
    LOGD("found symbol sonext");

    SoInfo *vdso = getStaticPointer<SoInfo>(linker, vdso_sym_name);
    if (vdso != NULL) LOGD("found symbol vdso");

    SoInfo::get_realpath_sym = reinterpret_cast<decltype(SoInfo::get_realpath_sym)>(linker.getSymbAddress("__dl__ZNK6soinfo12get_realpathEv"));
    if (SoInfo::get_realpath_sym == NULL) return false;
    LOGD("found symbol get_realpath_sym");

    SoInfo::get_soname_sym = reinterpret_cast<decltype(SoInfo::get_soname_sym)>(linker.getSymbAddress("__dl__ZNK6soinfo10get_sonameEv"));
    if (SoInfo::get_soname_sym == NULL) return false;
    LOGD("found symbol get_soname_sym");

    SoInfo::soinfo_free = reinterpret_cast<decltype(SoInfo::soinfo_free)>(linker.getSymbAddress(soinfo_free_name));
    if (SoInfo::soinfo_free == NULL) return false;
    LOGD("found symbol soinfo_free");

    g_module_load_counter = reinterpret_cast<decltype(g_module_load_counter)>(linker.getSymbAddress("__dl__ZL21g_module_load_counter"));
    if (g_module_load_counter != NULL) LOGD("found symbol g_module_load_counter");

    g_module_unload_counter = reinterpret_cast<decltype(g_module_unload_counter)>(linker.getSymbAddress("__dl__ZL23g_module_unload_counter"));
    if (g_module_unload_counter != NULL) LOGD("found symbol g_module_unload_counter");

    for (size_t i = 0; i < 1024 / sizeof(void *); i++) {
      auto possible_field = (uintptr_t) solist + i * sizeof(void *);
      auto possible_size_of_somain = *(size_t *)((uintptr_t) somain + i * sizeof(void *));
      if (possible_size_of_somain < 0x100000 && possible_size_of_somain > 0x100) {
        SoInfo::solist_size_offset = i * sizeof(void *);
        LOGD("solist_size_offset is %zu * %zu = %p", i, sizeof(void *), (void*) SoInfo::solist_size_offset);
      }
      if (*(void **)possible_field == somain || (vdso != NULL && *(void **)possible_field == vdso)) {
        SoInfo::solist_next_offset = i * sizeof(void *);
        LOGD("solist_next_offset is %zu * %zu = %p", i, sizeof(void *), (void*) SoInfo::solist_next_offset);
        break;
      }
    }

    return true;
  }
}
