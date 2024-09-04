#include <sys/ptrace.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/auxv.h>
#include <elf.h>
#include <link.h>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/system_properties.h>
#include <string>
#include <cinttypes>

#include "utils.hpp"

static void clear_static_string(int pid, struct user_regs_struct& regs, const char *path) {
    unsigned int len = strlen(path) + 1;
    char empty[len];
    memset(empty, 0x1, len);
    empty[len] = '\0';

    regs.REG_SP += len;

    push_string(pid, regs, empty);
}

bool inject_on_main(int pid, const char *lib_path) {
  LOGI("injecting %s to zygote %d", lib_path, pid);

  /*
    parsing KernelArgumentBlock

    https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/private/KernelArgumentBlock.h;l=30;drc=6d1ee77ee32220e4202c3066f7e1f69572967ad8
  */

  struct user_regs_struct regs {},
                          backup {};

  /* WARNING: C++ keyword */
  std::vector<MapInfo> map = MapInfo::Scan(std::to_string(pid));
  if (!get_regs(pid, regs)) return false;

  uintptr_t arg = (uintptr_t)regs.REG_SP;

  LOGV("kernel argument %" PRIxPTR " %s", arg, get_addr_mem_region(map, arg).c_str());

  int argc;
  char **argv = (char **)((uintptr_t *)arg + 1);
  LOGV("argv %p", (void *)argv);

  read_proc(pid, arg, &argc, sizeof(argc));
  LOGV("argc %d", argc);

  /* WARNING: C++ keyword */
  auto envp = argv + argc + 1;
  LOGV("envp %p", (void *)envp);

  /* WARNING: C++ keyword */
  auto p = envp;
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
  LOGV("auxv %p %s", auxv, get_addr_mem_region(map, (uintptr_t) auxv).c_str());

  ElfW(auxv_t) *v = auxv;
  uintptr_t entry_addr = 0;
  uintptr_t addr_of_entry_addr = 0;

  while (1) {
    ElfW(auxv_t) buf;

    read_proc(pid, (uintptr_t)v, &buf, sizeof(buf));

    if (buf.a_type == AT_ENTRY) {
      entry_addr = (uintptr_t)buf.a_un.a_val;
      addr_of_entry_addr = (uintptr_t)v + offsetof(ElfW(auxv_t), a_un);

      LOGV("entry address %" PRIxPTR " %s (entry=%" PRIxPTR ", entry_addr=%" PRIxPTR ")", entry_addr,
            get_addr_mem_region(map, entry_addr).c_str(), (uintptr_t)v, addr_of_entry_addr);

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

  uintptr_t break_addr = (-0x05ec1cff & ~1) | ((uintptr_t)entry_addr & 1);
  if (!write_proc(pid, (uintptr_t)addr_of_entry_addr, &break_addr, sizeof(break_addr))) return false;

  ptrace(PTRACE_CONT, pid, 0, 0);

  int status;
  wait_for_trace(pid, &status, __WALL);
  if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSEGV) {
    if (!get_regs(pid, regs)) return false;

    if (static_cast<uintptr_t>(regs.REG_IP & ~1) != (break_addr & ~1)) {
      LOGE("stopped at unknown addr %p", (void *) regs.REG_IP);

      return false;
    }

    /* The linker has been initialized now, we can do dlopen */
    LOGD("stopped at entry");

    /* restore entry address */
    if (!write_proc(pid, (uintptr_t) addr_of_entry_addr, &entry_addr, sizeof(entry_addr))) return false;

    /* backup registers */
    memcpy(&backup, &regs, sizeof(regs));

    /* WARNING: C++ keyword */
    map = MapInfo::Scan(std::to_string(pid));

    /* WARNING: C++ keyword */
    std::vector<MapInfo> local_map = MapInfo::Scan();
    void *libc_return_addr = find_module_return_addr(map, "libc.so");
    LOGD("libc return addr %p", libc_return_addr);

    /* call dlopen */
    void *dlopen_addr = find_func_addr(local_map, map, "libdl.so", "dlopen");
    if (dlopen_addr == NULL) return false;

    /* WARNING: C++ keyword */
    std::vector<long> args;

    /* WARNING: C++ keyword */
    uintptr_t str = push_string(pid, regs, lib_path);

    args.clear();
    args.push_back((long) str);
    args.push_back((long) RTLD_NOW);

    uintptr_t remote_handle = remote_call(pid, regs, (uintptr_t)dlopen_addr, (uintptr_t)libc_return_addr, args);
    clear_static_string(pid, regs, lib_path);
    
    LOGD("remote handle %p", (void *)remote_handle);
    if (remote_handle == 0) {
      LOGE("handle is null");

      /* call dlerror */
      void *dlerror_addr = find_func_addr(local_map, map, "libdl.so", "dlerror");
      if (dlerror_addr == NULL) {
        LOGE("find dlerror");

        return false;
      }

      args.clear();

      uintptr_t dlerror_str_addr = remote_call(pid, regs, (uintptr_t)dlerror_addr, (uintptr_t)libc_return_addr, args);
      LOGD("dlerror str %p", (void*) dlerror_str_addr);
      if (dlerror_str_addr == 0) return false;

      void *strlen_addr = find_func_addr(local_map, map, "libc.so", "strlen");
      if (strlen_addr == NULL) {
        LOGE("find strlen");

        return false;
      }

      args.clear();
      args.push_back(dlerror_str_addr);

      uintptr_t dlerror_len = remote_call(pid, regs, (uintptr_t)strlen_addr, (uintptr_t)libc_return_addr, args);
      if (dlerror_len <= 0) {
        LOGE("dlerror len <= 0");

        return false;
      }

      /* NOTICE: C++ -> C */
      char *err = (char *)malloc(dlerror_len + 1);
      read_proc(pid, (uintptr_t) dlerror_str_addr, err, dlerror_len);

      LOGE("dlerror info %s", err);

      free(err);

      return false;
    }

    /* call dlsym(handle, "entry") */
    void *dlsym_addr = find_func_addr(local_map, map, "libdl.so", "dlsym");
    if (dlsym_addr == NULL) return false;

    args.clear();
    const char *entry = "entry";
    str = push_string(pid, regs, entry);
    args.push_back(remote_handle);
    args.push_back((long) str);

    uintptr_t injector_entry = remote_call(pid, regs, (uintptr_t)dlsym_addr, (uintptr_t)libc_return_addr, args);
    clear_static_string(pid, regs, entry);

    LOGD("injector entry %p", (void *)injector_entry);
    if (injector_entry == 0) {
      LOGE("injector entry is null");

      return false;
    }

    /* call injector entry(handle, path) */

    args.clear();
    args.push_back(remote_handle);
    const char *tmp_path = zygiskd::GetTmpPath().c_str();
    str = push_string(pid, regs, tmp_path);
    args.push_back((long) str);

    remote_call(pid, regs, injector_entry, (uintptr_t)libc_return_addr, args);
    clear_static_string(pid, regs, tmp_path);

    /* reset pc to entry */
    backup.REG_IP = (long) entry_addr;
    LOGD("invoke entry");

    /* restore registers */
    if (!set_regs(pid, backup)) return false;

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

  if (ptrace(PTRACE_SEIZE, pid, 0, PTRACE_O_EXITKILL) == -1) {
    PLOGE("seize");

    return false;
  }

  WAIT_OR_DIE

  if (STOPPED_WITH(SIGSTOP, PTRACE_EVENT_STOP)) {
    /* WARNING: C++ keyword */
    std::string lib_path = zygiskd::GetTmpPath();
    lib_path += "/lib" LP_SELECT("", "64") "/libzygisk.so";

    if (!inject_on_main(pid, lib_path.c_str())) {
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
