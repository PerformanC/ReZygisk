#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include <link.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <elf.h>
#include <unistd.h>

#define LOG_TAG "zygisk-injector" LP_SELECT("32", "64")

#include "misc.h"
#include "utils.h"

#include "remote_csoloader.h"

/* TODO: I can't express how many detections this likely will have, but
           it is not of high priority. 32-bit apps are going to phase out
           eventually. However, we should investigate possible detections,
           especially when using mprotect to register memory pages. */
static bool inject_tango(int pid, const char *lib_path, uint32_t libc_init_target, uint32_t libc_init_got_slot) {
  struct user_regs_struct regs = { 0 };
  if (!get_regs(pid, &regs)) {
    PLOGE("Failed to get registers");

    return false;
  }

  struct user_regs_struct backup;
  memcpy(&backup, &regs, sizeof(regs));

  /* INFO: The character limit for a 32-bit integer is 10 */
  char pid_str[10 + 1];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);

  struct maps_info *remote_map = parse_maps(pid_str);
  if (!remote_map) {
    LOGE("Failed to parse remote maps for pid %d", pid);

    return false;
  }

  struct maps_info *local_map = parse_maps("self");
  if (!local_map) {
    LOGE("Failed to parse local maps");

    free_maps(remote_map);

    return false;
  }

  bool ok = false;
  bool need_restore = true;

  uintptr_t mapped_base = 0, mapped_entry = 0;
  size_t mapped_size = 0;
  if (!remote_csoloader_load_and_resolve_entry(pid, &regs, remote_map, local_map, lib_path, &mapped_base, &mapped_size, &mapped_entry)) {
    LOGE("Failed to load %s", lib_path);

    goto tango_done;
  }

  uint32_t lib_base = (uint32_t)mapped_base;
  uint32_t lib_size = (uint32_t)mapped_size;
  uint32_t lib_entry = (uint32_t)mapped_entry;

  LOGD("Mapped %s at 0x%x (size: 0x%x, entry=0x%x)", lib_path, lib_base, lib_size, lib_entry);

  if (!lib_entry) {
    LOGE("Failed to find 'entry' symbol in %s", lib_path);

    goto tango_done;
  }

  /* INFO: Tango translator intercepts both BKPT and UDF in userspace, before it
             can even raise SIGTRAP to the tracer. To bypass this, we use the
             kill(getpid(), SIGTRAP) trick which goes through the kernel's normal
             signal delivery path, allowing the tracer to catch it. */
  uint32_t code[] = {
    /* INFO: Call mprotect syscall to register the libzygisk.so memory pages */
    0x4807B5FF, /* PUSH {r0-r7,lr} ; LDR r0,[PC,#28] → data[0]    */
    0x22074907, /* LDR r1,[PC,#28] → data[1] ; MOVS r2,#7 (RWX)   */
    0xDF00277D, /* MOVS r7,#125 (__NR_mprotect) ; SVC #0           */
    /* INFO: Call the libzygisk.so entry function */
    0x49054804, /* LDR r0,[PC,#16] → data[0] ; LDR r1,[PC,#20] → data[1] */
    0x4B052201, /* MOVS r2,#1 (tango) ; LDR r3,[PC,#20] → data[2] */
    /* INFO: Call kill(getpid(), SIGTRAP) to trigger SIGTRAP */
    0x27144798, /* BLX r3 ; MOVS r7,#20 (__NR_getpid)              */
    0x2105DF00, /* SVC #0 (getpid) ; MOVS r1,#5 (SIGTRAP)          */
    0xDF002725, /* MOVS r7,#37 (__NR_kill) ; SVC #0                 */
    /* INFO: Data for the code above (lib_base, lib_size, lib_entry) */
    lib_base,
    lib_size,
    /* INFO: add | 1 would make it enter thumb mode */
    lib_entry,
    #if 0
      lib_entry | 1,
    #endif
    0,          /* INFO: Placeholder for the stub's later use */
  };

  uint32_t tramp = 0;
  for (size_t i = 0; i < remote_map->length && !tramp; i++) {
    const struct map_entry *map = &remote_map->maps[i];
    if (!map->path || !(map->perms & PROT_EXEC) || (uintptr_t)map->start >= 0x100000000ULL) continue;

    tramp = find_tramp_padding(pid, (uint32_t)(uintptr_t)map->start, (uint32_t)(uintptr_t)map->end, sizeof(code));
  }

  if (!tramp) {
    LOGE("Failed to find enough executable padding for trampoline (%zu bytes)", sizeof(code));

    goto tango_done;
  }

  for (size_t i = 0; i < sizeof(code) / sizeof(code[0]); i++) {
    if (ptrace_poke_u32(pid, (uintptr_t)(tramp + i * 4), code[i])) continue;

    LOGE("Failed to write trampoline word %zu", i);

    goto tango_done;
  }

  LOGD("GOT hook __libc_init in app_process32 (0x%x to trampoline 0x%x)", libc_init_target, tramp | 1);

  uint32_t tramp_thumb = tramp | 1;
  if (write_proc(pid, (uintptr_t)libc_init_got_slot, &tramp_thumb, 4) != 4 && !ptrace_poke_u32(pid, (uintptr_t)libc_init_got_slot, tramp_thumb)) {
    PLOGE("Patch GOT entry at 0x%x", libc_init_got_slot);

    goto tango_done;
  }

  if (!set_regs(pid, &backup)) {
    LOGE("Failed to restore regs before trampoline run");

    goto tango_done;
  }

  need_restore = false;

  if (ptrace(PTRACE_CONT, pid, 0, 0) == -1) {
    PLOGE("PTRACE_CONT for trampoline execution");

    goto tango_done;
  }

  /* INFO: Wait for it to get to __libc_init, which then goes to its GOT which
             then executes our trampoline. After executing our trampoline, it
             executes libzygisk.so entry, and after that, we raise a SIGTRAP
             event with kill(...), which here, we wait for it, to then do
             the cleanup. */
  while (1) {
    int status;
    wait_for_trace(pid, &status, __WALL);

    if (!WIFSTOPPED(status)) {
      LOGE("Process %d exited during trampoline (status 0x%x)", pid, status);

      goto tango_done;
    }

    int sig   = WSTOPSIG(status);
    int event = (status >> 16) & 0xFF;
    /* INFO: Catch the SIGTRAP generated by the kill() call */
    if (sig == SIGTRAP && event == 0)
      break;

    if (ptrace(PTRACE_CONT, pid, 0, event ? 0 : sig) == -1) {
      PLOGE("PTRACE_CONT while waiting trampoline SIGTRAP");

      goto tango_done;
    }
  }

  LOGD("Caught trampoline SIGTRAP");

  /* INFO: Clean the trampoline after catching the SIGTRAP to avoid detections */
  for (size_t i = 0; i < sizeof(code) / sizeof(code[0]); i++)
    ptrace_poke_u32(pid, (uintptr_t)(tramp + i * 4), 0);

  /* INFO: Also clean the GOT entry */
  if (!ptrace_poke_u32(pid, (uintptr_t)libc_init_got_slot, libc_init_target))
    LOGW("Failed to restore GOT at 0x%x", libc_init_got_slot);

  LOGD("Restored __libc_init GOT entry and zeroed trampoline");

  /* INFO: Tango translator maintains memory-side state that would go out of sync if we
             rewind execution via set_regs, so instead we write a tiny tail-call stub
             at the end of the trampoline's data that jumps forward into __libc_init. */
  ptrace_poke_u32(pid, (uintptr_t)(tramp + 32), 0x40FFE8BD /* POP.W {r0-r7,lr}   */);
  ptrace_poke_u32(pid, (uintptr_t)(tramp + 36), 0xC004F8DF /* LDR.W r12,[PC,#4]  */);
  ptrace_poke_u32(pid, (uintptr_t)(tramp + 40), 0x00004760 /* BX r12 ; (padding) */);
  ptrace_poke_u32(pid, (uintptr_t)(tramp + 44), libc_init_target);

  if (ptrace(PTRACE_SYSCALL, pid, 0, 0) == -1) {
    PLOGE("PTRACE_SYSCALL for tail-call stub");

    ok = true;

    goto tango_done;
  }

  int post_stub_status = 0;
  if (!wait_for_ptrace_syscall_stop(pid, &post_stub_status)) {
    LOGE("Process %d died waiting for post-stub syscall", pid);

    goto tango_done;
  }

  /* Zero the stub */
  for (size_t i = 0; i < 4; i++)
    ptrace_poke_u32(pid, (uintptr_t)(tramp + 32 + i * 4), 0);

  ok = true;

  tango_done:
    free_maps(remote_map);
    free_maps(local_map);

    if (need_restore) (void)set_regs(pid, &backup);

    return ok;
}

bool inject_on_main(int pid, const char *lib_path) {
  LOGI("injecting %s to zygote %d", lib_path, pid);

  /*
    parsing KernelArgumentBlock

    https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/private/KernelArgumentBlock.h;l=30;drc=6d1ee77ee32220e4202c3066f7e1f69572967ad8
  */

  struct user_regs_struct regs = { 0 };

  /* INFO: The character limit for a 32-bit integer is 10 */
  char pid_str[10 + 1];
  snprintf(pid_str, sizeof(pid_str), "%d", pid);

  struct maps_info *map = parse_maps(pid_str);
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

    p++;
  }

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

  /* INFO: (-0x0F & ~1) is a value below zero, while the one after "|"
            is an unsigned (must be 0 or greater) value, so we must
            cast the second value to signed long (intptr_t) to avoid
            undefined behavior.

           Replace the program entry with an invalid address. For arm32 compatibility,
            we set the last bit to the same as the entry address.
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

    /* INFO: The linker has been initialized now, we can do dlopen */
    LOGD("stopped at entry");

    /* INFO: Restore entry address */
    if (!write_proc(pid, (uintptr_t) addr_of_entry_addr, &entry_addr, sizeof(entry_addr))) return false;

    /* INFO: Backup registers */
    struct user_regs_struct backup;
    memcpy(&backup, &regs, sizeof(regs));

    free_maps(map);

    map = parse_maps(pid_str);
    if (!map) {
      LOGE("failed to parse remote maps");

      return false;
    }

    struct maps_info *local_map = parse_maps("self");
    if (!local_map) {
      LOGE("failed to parse local maps");

      return false;
    }

    void *libc_return_addr = find_module_return_addr(map, "libc.so");
    LOGD("libc return addr %p", libc_return_addr);

    uintptr_t remote_base = 0;
    size_t remote_size = 0;
    uintptr_t injector_entry = 0;

    if (!remote_csoloader_load_and_resolve_entry(pid, &regs, map, local_map, lib_path, &remote_base, &remote_size, &injector_entry)) {
      LOGE("remote CSOLoader mapping failed");

      free_maps(local_map);
      free_maps(map);

      return false;
    }

    free_maps(local_map);
    free_maps(map);

    long args[3];
    args[0] = (long)remote_base;
    args[1] = (long)remote_size;
    args[2] = 0; /* INFO: tango_flag */

    remote_call(pid, &regs, injector_entry, (uintptr_t)libc_return_addr, args, 3);

    /* INFO: remote_call uses a deliberate SIGSEGV on an invalid return address to regain control.
               If the call faults elsewhere (e.g., inside injector code), REG_IP won't match. */
    bool injector_ok = false;
    #if defined(__arm__)
      injector_ok = (((uintptr_t)regs.REG_IP & ~1u) == ((uintptr_t)libc_return_addr & ~1u));
    #else
      injector_ok = ((uintptr_t)regs.REG_IP == (uintptr_t)libc_return_addr);
    #endif
    if (!injector_ok) {
      char stopped_region[1024];
      struct maps_info *map_after = parse_maps(pid_str);
      if (map_after) {
        get_addr_mem_region(map_after, (uintptr_t)regs.REG_IP, stopped_region, sizeof(stopped_region));

        free_maps(map_after);
      } else {
        snprintf(stopped_region, sizeof(stopped_region), "<maps unavailable>");
      }

      LOGE("injector entry faulted at %p (%s)", (void *)regs.REG_IP, stopped_region);

      /* INFO: Restore registers before reporting failure. */
      backup.REG_IP = (long)entry_addr;

      (void)set_regs(pid, &backup);

      return false;
    }

    /* INFO: Reset pc to entry */
    backup.REG_IP = (long) entry_addr;
    LOGD("invoke entry");

    /* INFO: Restore registers */
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

bool trace_zygote(int pid, bool tango_flag) {
  LOGI("start tracing %d (tracer %d)", pid, getpid());

  int status;

  struct kernel_version version = parse_kversion();
  if (version.major > 3 || (version.major == 3 && version.minor >= 8)) {
    #ifdef __aarch64__
      if (tango_flag) {
        /* INFO: For tango injection, we need to seize with PTRACE_O_TRACESYSGOOD to
                   reliably catch the translator's entry point. */
        if (ptrace(PTRACE_SEIZE, pid, 0, PTRACE_O_EXITKILL | PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACESECCOMP) == -1) {
          PLOGE("seize for tango");

          return false;
        }
      } else {
        if (ptrace(PTRACE_SEIZE, pid, 0, PTRACE_O_EXITKILL | PTRACE_O_TRACESECCOMP) == -1) {
          PLOGE("seize");

          return false;
        }

        WAIT_OR_DIE;
      }
    #else
      long seize_opts = PTRACE_O_EXITKILL | PTRACE_O_TRACESECCOMP;
      if (tango_flag) seize_opts |= PTRACE_O_TRACESYSGOOD;

      if (ptrace(PTRACE_SEIZE, pid, 0, seize_opts) == -1) {
        if (tango_flag) PLOGE("seize for tango");
        else PLOGE("seize");

        return false;
      }

      if (!tango_flag) WAIT_OR_DIE;
    #endif
  } else {
    if (ptrace(PTRACE_SEIZE, pid, 0, 0) == -1) {
      PLOGE("seize");

      return false;
    }

    WAIT_OR_DIE;
  }

    if (tango_flag) {
      if (ptrace(PTRACE_INTERRUPT, pid, 0, 0) == -1) {
        PLOGE("interrupt");

        ptrace(PTRACE_DETACH, pid, 0, 0);

        return false;
      }

      /* INFO: Drain to INTERRUPT's SIGTRAP + EVENT_STOP */
      if (!wait_for_event_stop(pid)) {
        LOGE("Failed to drain to event stop for tango injection");

        ptrace(PTRACE_DETACH, pid, 0, SIGCONT);

        return false;
      }

      struct tango_linker_watch watch = { 0 };
      if (!tango_wait_linker_ready(pid, &watch)) {
        LOGE("Failed to wait for linker ready for injection");

        ptrace(PTRACE_DETACH, pid, 0, SIGCONT);

        return false;
      }

      /* INFO: Leave syscall-stop state before injection */
      ptrace(PTRACE_CONT, pid, 0, 0);
      if (ptrace(PTRACE_INTERRUPT, pid, 0, 0) == -1) {
        PLOGE("Failed to interrupt process for injection");

        ptrace(PTRACE_DETACH, pid, 0, SIGCONT);

        return false;
      }

      if (!wait_for_event_stop(pid)) {
        LOGE("Failed to drain to event stop for injection");

        ptrace(PTRACE_DETACH, pid, 0, SIGCONT);

        return false;
      }

      const char *lib_path = "/data/adb/modules/rezygisk/lib/libzygisk.so";
      bool result = inject_tango(pid, lib_path, watch.libc_init_resolved, watch.libc_init_got_slot);
      if (!result) LOGE("Failed to inject tango");

      ptrace(PTRACE_DETACH, pid, 0, SIGCONT);

      return result;
    }

  if (STOPPED_WITH(SIGSTOP, PTRACE_EVENT_STOP)) {
    char *lib_path = "/data/adb/modules/rezygisk/lib" LP_SELECT("", "64") "/libzygisk.so";
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
