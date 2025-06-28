#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sys/ptrace.h>
#include <sys/auxv.h>
#include <elf.h>
#include <link.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <signal.h>

#include <unistd.h>

#include "utils.h"

bool inject_on_main(int pid, const char *lib_path) {
  LOGI("injecting %s to zygote %d", lib_path, pid);

  /*
    parsing KernelArgumentBlock

    https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/private/KernelArgumentBlock.h;l=30;drc=6d1ee77ee32220e4202c3066f7e1f69572967ad8
  */

  struct user_regs_struct regs = { 0 };

  char pid_maps[PATH_MAX];
  snprintf(pid_maps, sizeof(pid_maps), "/proc/%d/maps", pid);

  struct maps *map = parse_maps(pid_maps);
  if (map == NULL) {
    LOGE("failed to parse remote maps");

    return false;
  }

  if (!get_regs(pid, &regs)) return false;

  uintptr_t arg = (uintptr_t)regs.REG_SP;

  char addr_mem_region[1024];
  get_addr_mem_region(map, arg, addr_mem_region, sizeof(addr_mem_region));

  LOGV("kernel argument %" PRIxPTR " %s", arg, addr_mem_region);

  int argc;
  char **argv = (char **)((uintptr_t *)arg + 1);
  LOGV("argv %p", (void *)argv);

  read_proc(pid, arg, &argc, sizeof(argc));
  LOGV("argc %d", argc);

  char **envp = argv + argc + 1;
  LOGV("envp %p", (void *)envp);

  char **p = envp;
  while (1) {
    uintptr_t *buf;
    read_proc(pid, (uintptr_t)p, &buf, sizeof(buf));

    if (buf == NULL) break;

    /* TODO: Why ++p? */
    p++;
  }

  /* TODO: Why ++p? */
  p++;

  ElfW(auxv_t) *auxv = (ElfW(auxv_t) *)p;

  get_addr_mem_region(map, (uintptr_t)auxv, addr_mem_region, sizeof(addr_mem_region));
  LOGV("auxv %p %s", auxv, addr_mem_region);

  ElfW(auxv_t) *v = auxv;
  uintptr_t entry_addr = 0;
  uintptr_t addr_of_entry_addr = 0;

  while (1) {
    ElfW(auxv_t) buf;

    read_proc(pid, (uintptr_t)v, &buf, sizeof(buf));

    if (buf.a_type == AT_ENTRY) {
      entry_addr = (uintptr_t)buf.a_un.a_val;
      addr_of_entry_addr = (uintptr_t)v + offsetof(ElfW(auxv_t), a_un);

      get_addr_mem_region(map, entry_addr, addr_mem_region, sizeof(addr_mem_region));
      LOGV("entry address %" PRIxPTR " %s (entry=%" PRIxPTR ", entry_addr=%" PRIxPTR ")", entry_addr,
            addr_mem_region, (uintptr_t)v, addr_of_entry_addr);

      break;
    }

    if (buf.a_type == AT_NULL) break;

    v++;
  }

  if (entry_addr == 0) {
    LOGE("failed to get entry");

    return false;
  }

  /*
    Replace the program entry with an invalid address
    For arm32 compatibility, we set the last bit to the same as the entry address
  */

  /* INFO: (-0x0F & ~1) is a value below zero, while the one after "|"
            is an unsigned (must be 0 or greater) value, so we must
            cast the second value to signed long (intptr_t) to avoid
            undefined behavior.
  */
  uintptr_t break_addr = (uintptr_t)((intptr_t)(-0x0F & ~1) | (intptr_t)((uintptr_t)entry_addr & 1));
  if (!write_proc(pid, (uintptr_t)addr_of_entry_addr, &break_addr, sizeof(break_addr))) return false;

  ptrace(PTRACE_CONT, pid, 0, 0);

  int status;
  wait_for_trace(pid, &status, __WALL);
  if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSEGV) {
    if (!get_regs(pid, &regs)) return false;

    if (((int)regs.REG_IP & ~1) != ((int)break_addr & ~1)) {
      LOGE("stopped at unknown addr %p", (void *) regs.REG_IP);

      return false;
    }

    /* The linker has been initialized now, we can do dlopen */
    LOGD("stopped at entry");

    /* restore entry address */
    if (!write_proc(pid, (uintptr_t) addr_of_entry_addr, &entry_addr, sizeof(entry_addr))) return false;

    /* backup registers */
    struct user_regs_struct backup;
    memcpy(&backup, &regs, sizeof(regs));

    free_maps(map);

    map = parse_maps(pid_maps);
    if (!map) {
      LOGE("failed to parse remote maps");

      return false;
    }

    struct maps *local_map = parse_maps("/proc/self/maps");
    if (!local_map) {
      LOGE("failed to parse local maps");

      return false;
    }

    void *libc_return_addr = find_module_return_addr(map, "libc.so");
    LOGD("libc return addr %p", libc_return_addr);

    const char *libdl_path = NULL;
    const char *libc_path = NULL;
    for (size_t i = 0; i < local_map->size; i++) {
      if (local_map->maps[i].path == NULL) continue;

      const char *filename = position_after(local_map->maps[i].path, '/');

      if (strcmp(filename, "libdl.so") == 0) {
        libdl_path = local_map->maps[i].path;

        /* INFO: If we had found libc.so too, no need to continue searching */
        if (libc_path) break;

        continue;
      }

      if (strcmp(filename, "libc.so") == 0) {
        libc_path = local_map->maps[i].path;

        /* INFO: If we had found libdl.so too, no need to continue searching */
        if (libdl_path) break;

        continue;
      }
    }

    /* call dlopen */
    void *dlopen_addr = NULL;
    if (!libdl_path || (dlopen_addr = find_func_addr(local_map, map, libdl_path, "dlopen")) == NULL) {
      /* INFO: Android 7.1 and below doesn't have libdl.so loaded in Zygote */
      LOGW("Failed to find dlopen from libdl.so, will load from linker");

      #ifdef __LP64__
        dlopen_addr = find_func_addr(local_map, map, "/system/bin/linker64", "__dl_dlopen");
      #else
        dlopen_addr = find_func_addr(local_map, map, "/system/bin/linker", "__dl_dlopen");
      #endif
      if (dlopen_addr == NULL) {
        PLOGE("Find __dl_dlopen");

        free_maps(local_map);
        free_maps(map);

        return false;
      }
    }

    long *args = (long *)malloc(3 * sizeof(long));
    if (args == NULL) {
      LOGE("malloc args");

      return false;
    }

    uintptr_t str = push_string(pid, &regs, lib_path);

    args[0] = (long) str;
    args[1] = (long) RTLD_NOW;

    uintptr_t remote_handle = remote_call(pid, &regs, (uintptr_t)dlopen_addr, (uintptr_t)libc_return_addr, args, 2);
    LOGD("remote handle %p", (void *)remote_handle);
    if (remote_handle == 0) {
      LOGE("handle is null");

      /* call dlerror */
      void *dlerror_addr = NULL;
      if (!libdl_path || (dlerror_addr = find_func_addr(local_map, map, libdl_path, "dlerror")) == NULL) {
        /* INFO: Android 7.1 and below doesn't have libdl.so loaded in Zygote */
        LOGW("Failed to find dlerror from libdl.so, will load from linker");

        #ifdef __LP64__
          dlerror_addr = find_func_addr(local_map, map, "/system/bin/linker64", "__dl_dlerror");
        #else
          dlerror_addr = find_func_addr(local_map, map, "/system/bin/linker", "__dl_dlerror");
        #endif
        if (dlerror_addr == NULL) {
          LOGE("Find __dl_dlerror");

          free(args);
          free_maps(local_map);
          free_maps(map);

          return false;
        }
      }

      uintptr_t dlerror_str_addr = remote_call(pid, &regs, (uintptr_t)dlerror_addr, (uintptr_t)libc_return_addr, args, 0);
      LOGD("dlerror str %p", (void *)dlerror_str_addr);
      if (dlerror_str_addr == 0) {
        LOGE("dlerror str is null");

        free(args);
        free_maps(local_map);
        free_maps(map);

        return false;
      }

      void *strlen_addr = find_func_addr(local_map, map, libc_path, "strlen");
      if (strlen_addr == NULL) {
        LOGE("find strlen");

        free(args);
        free_maps(local_map);
        free_maps(map);

        return false;
      }

      args[0] = (long) dlerror_str_addr;

      uintptr_t dlerror_len = remote_call(pid, &regs, (uintptr_t)strlen_addr, (uintptr_t)libc_return_addr, args, 1);
      if (dlerror_len <= 0) {
        LOGE("dlerror len <= 0");

        free(args);
        free_maps(local_map);
        free_maps(map);

        return false;
      }

      char *err = (char *)malloc((dlerror_len + 1) * sizeof(char));
      if (err == NULL) {
        LOGE("malloc err");

        free(args);
        free_maps(local_map);
        free_maps(map);

        return false;
      }

      read_proc(pid, dlerror_str_addr, err, dlerror_len + 1);

      LOGE("dlerror info %s", err);

      free(err);
      free(args);

      free_maps(local_map);
      free_maps(map);

      return false;
    }

    /* call dlsym(handle, "entry") */
    void *dlsym_addr = NULL;
    if (!libdl_path || (dlsym_addr = find_func_addr(local_map, map, libdl_path, "dlsym")) == NULL) {
      /* INFO: Android 7.1 and below doesn't have libdl.so loaded in Zygote */
      LOGW("Failed to find dlsym from libdl.so, will load from linker");

      #ifdef __LP64__
        dlsym_addr = find_func_addr(local_map, map, "/system/bin/linker64", "__dl_dlsym");
      #else
        dlsym_addr = find_func_addr(local_map, map, "/system/bin/linker", "__dl_dlsym");
      #endif
      if (dlsym_addr == NULL) {
        LOGE("find __dl_dlsym");

        free(args);
        free_maps(local_map);
        free_maps(map);

        return false;
      }
    }

    free_maps(local_map);

    str = push_string(pid, &regs, "entry");
    args[0] = remote_handle;
    args[1] = (long) str;

    uintptr_t injector_entry = remote_call(pid, &regs, (uintptr_t)dlsym_addr, (uintptr_t)libc_return_addr, args, 2);
    LOGD("injector entry %p", (void *)injector_entry);
    if (injector_entry == 0) {
      LOGE("injector entry is null");

      return false;
    }

    /* record the address range of libzygisk.so */
    map = parse_maps(pid_maps);

    void *start_addr = NULL;
    size_t block_size = 0;

    for (size_t i = 0; i < map->size; i++) {
      if (!strstr(map->maps[i].path, "libzygisk.so")) continue;

      if (start_addr == NULL) start_addr = (void *)map->maps[i].start;

      size_t size = map->maps[i].end - map->maps[i].start;
      block_size += size;

      LOGD("found block %s: [%p-%p] with size %zu", map->maps[i].path, (void *)map->maps[i].start,
            (void *)map->maps[i].end, size);
    }

    free_maps(map);

    /* call injector entry(start_addr, block_size, path) */
    args[0] = (uintptr_t)start_addr;
    args[1] = block_size;
    str = push_string(pid, &regs, rezygiskd_get_path());
    args[2] = (uintptr_t)str;

    remote_call(pid, &regs, injector_entry, (uintptr_t)libc_return_addr, args, 3);

    free(args);

    /* reset pc to entry */
    backup.REG_IP = (long) entry_addr;
    LOGD("invoke entry");

    /* restore registers */
    if (!set_regs(pid, &backup)) return false;

    return true;
  } else {
    char status_str[64];
    parse_status(status, status_str, sizeof(status_str));

    LOGE("stopped by other reason: %s", status_str);
  }

  return false;
}

#define STOPPED_WITH(sig, event) (WIFSTOPPED(status) && WSTOPSIG(status) == (sig) && (status >> 16) == (event))
#define WAIT_OR_DIE wait_for_trace(pid, &status, __WALL);
#define CONT_OR_DIE                           \
  if (ptrace(PTRACE_CONT, pid, 0, 0) == -1) { \
    PLOGE("cont");                            \
                                              \
    return false;                             \
  }

bool trace_zygote(int pid) {
  LOGI("start tracing %d (tracer %d)", pid, getpid());

  int status;

  if (ptrace(PTRACE_SEIZE, pid, 0, PTRACE_O_EXITKILL | PTRACE_O_TRACESECCOMP) == -1) {
    PLOGE("seize");

    return false;
  }

  WAIT_OR_DIE

  if (STOPPED_WITH(SIGSTOP, PTRACE_EVENT_STOP)) {
    char lib_path[PATH_MAX];
    snprintf(lib_path, sizeof(lib_path), "%s/lib" LP_SELECT("", "64") "/libzygisk.so", rezygiskd_get_path());

    if (!inject_on_main(pid, lib_path)) {
      LOGE("failed to inject");

      return false;
    }

    LOGD("inject done, continue process");
    if (kill(pid, SIGCONT)) {
      PLOGE("kill");

      return false;
    }

    CONT_OR_DIE
    WAIT_OR_DIE

    if (STOPPED_WITH(SIGTRAP, PTRACE_EVENT_STOP)) {
      CONT_OR_DIE
      WAIT_OR_DIE

      if (STOPPED_WITH(SIGCONT, 0)) {
        LOGD("received SIGCONT");

        /* INFO: Due to kernel bugs, fixed in 5.16+, ptrace_message (msg of
             PTRACE_GETEVENTMSG) may not represent the current state of
             the process. Because we set some options, which alters the
             ptrace_message, we need to call PTRACE_SYSCALL to reset the
             ptrace_message to 0, the default/normal state.
        */
        ptrace(PTRACE_SYSCALL, pid, 0, 0);

        WAIT_OR_DIE

        ptrace(PTRACE_DETACH, pid, 0, SIGCONT);
      }
    } else {
      char status_str[64];
      parse_status(status, status_str, sizeof(status_str));

      LOGE("unknown state %s, not SIGTRAP + EVENT_STOP", status_str);

      ptrace(PTRACE_DETACH, pid, 0, 0);

      return false;
    }
  } else {
    char status_str[64];
    parse_status(status, status_str, sizeof(status_str));

    LOGE("unknown state %s, not SIGSTOP + EVENT_STOP", status_str);

    ptrace(PTRACE_DETACH, pid, 0, 0);

    return false;
  }

  return true;
}
