#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sched.h>
#include <signal.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include "elf_util.h"
#include "elf_util_32.h"

struct maps *parse_maps(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    LOGE("Failed to open %s", filename);

    return NULL;
  }

  struct maps *maps = (struct maps *)malloc(sizeof(struct maps));
  if (!maps) {
    LOGE("Failed to allocate memory for maps");

    fclose(fp);

    return NULL;
  }

  /* INFO: To ensure in the realloc the libc will know it is meant
             to allocate, and not reallocate from a garbage address. */
  maps->maps = NULL;

  char line[4096 * 2];
  size_t i = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    line[strcspn(line, "\n")] = '\0';

    uintptr_t addr_start;
    uintptr_t addr_end;
    uintptr_t addr_offset;
    ino_t inode;
    unsigned int dev_major;
    unsigned int dev_minor;
    char permissions[5] = "";
    int path_offset;

    sscanf(line,
           "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %x:%x %lu %n%*s",
           &addr_start, &addr_end, permissions, &addr_offset, &dev_major, &dev_minor,
           &inode, &path_offset);

    while (isspace(line[path_offset])) {
      path_offset++;
    }

    struct map *tmp_maps = (struct map *)realloc(maps->maps, (i + 1) * sizeof(struct map));
    if (!tmp_maps) {
      LOGE("Failed to allocate memory for maps->maps");

      maps->size = i;

      fclose(fp);
      free_maps(maps);

      return NULL;
    }
    maps->maps = tmp_maps;

    maps->maps[i].start = addr_start;
    maps->maps[i].end = addr_end;
    maps->maps[i].offset = addr_offset;

    maps->maps[i].perms = 0;
    if (permissions[0] == 'r') maps->maps[i].perms |= PROT_READ;
    if (permissions[1] == 'w') maps->maps[i].perms |= PROT_WRITE;
    if (permissions[2] == 'x') maps->maps[i].perms |= PROT_EXEC;

    maps->maps[i].is_private = permissions[3] == 'p';
    maps->maps[i].dev = makedev(dev_major, dev_minor);
    maps->maps[i].inode = inode;
    maps->maps[i].path = strdup(line + path_offset);
    if (!maps->maps[i].path) {
      LOGE("Failed to allocate memory for maps->maps[%zu].path", i);

      maps->size = i;

      fclose(fp);
      free_maps(maps);

      return NULL;
    }

    i++;
  }

  fclose(fp);

  maps->size = i;

  return maps;
}

void free_maps(struct maps *maps) {
  for (size_t i = 0; i < maps->size; i++) {
    free((void *)maps->maps[i].path);
  }

  free(maps->maps);
  free(maps);
}

ssize_t write_proc(int pid, uintptr_t remote_addr, const void *buf, size_t len) {
  LOGV("write to remote addr %" PRIxPTR " size %zu", remote_addr, len);

  struct iovec local = {
    .iov_base = (void *)buf,
    .iov_len = len
  };

  struct iovec remote = {
    .iov_base = (void *)remote_addr,
    .iov_len = len
  };

  ssize_t l = process_vm_writev(pid, &local, 1, &remote, 1, 0);
  if (l == -1) PLOGE("process_vm_writev");
  else if ((size_t)l != len) LOGW("not fully written: %zu, excepted %zu", l, len);

  return l;
}

ssize_t read_proc(int pid, uintptr_t remote_addr, void *buf, size_t len) {
  struct iovec local = {
    .iov_base = (void *)buf,
    .iov_len = len
  };

  struct iovec remote = {
    .iov_base = (void *)remote_addr,
    .iov_len = len
  };

  ssize_t l = process_vm_readv(pid, &local, 1, &remote, 1, 0);
  if (l == -1) PLOGE("process_vm_readv");
  else if ((size_t)l != len) LOGW("not fully read: %zu, excepted %zu", l, len);

  return l;
}

bool get_regs(int pid, struct user_regs_struct *regs) {
  #if defined(__x86_64__) || defined(__i386__)
    if (ptrace(PTRACE_GETREGS, pid, 0, regs) == -1) {
      PLOGE("getregs");

      return false;
    }
  #elif defined(__aarch64__) || defined(__arm__)
    struct iovec iov = {
      .iov_base = regs,
      .iov_len = sizeof(struct user_regs_struct),
    };

    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov) == -1) {
      PLOGE("GETREGSET failed, trying GETREGS");

      if (ptrace(/* PTRACE_GETREGS */ 12, pid, 0, regs) == -1) {
        PLOGE("GETREGS");

        return false;
      }

      return true;
    }
  #endif

  return true;
}

bool set_regs(int pid, struct user_regs_struct *regs) {
  #if defined(__x86_64__) || defined(__i386__)
    if (ptrace(PTRACE_SETREGS, pid, 0, regs) == -1) {
      PLOGE("setregs");

      return false;
    }
  #elif defined(__aarch64__) || defined(__arm__)
    struct iovec iov = {
      .iov_base = regs,
      .iov_len = sizeof(struct user_regs_struct),
    };

    if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov) == -1) {
      PLOGE("SETREGSET failed, trying SETREGS");

      if (ptrace(/* PTRACE_SETREGS */ 13, pid, 0, regs) == -1) {
        PLOGE("SETREGS");

        return false;
      }

      return true;
    }
  #endif

  return true;
}

void get_addr_mem_region(struct maps *info, uintptr_t addr, char *buf, size_t buf_size) {
  for (size_t i = 0; i < info->size; i++) {
    const struct map *m = &info->maps[i];
    if (m->start <= addr && m->end > addr) {
      const char *path = m->path ? m->path : "<anonymous>";
      snprintf(buf, buf_size, "%s %s%s%s",
               path,
               m->perms & PROT_READ ? "r" : "-",
               m->perms & PROT_WRITE ? "w" : "-",
               m->perms & PROT_EXEC ? "x" : "-");

      return;
    }
  }

  snprintf(buf, buf_size, "<unknown>");
}

/* INFO: strrchr but without modifying the string */
const char *position_after(const char *str, const char needle) {
  const char *positioned = strrchr(str, needle);
  return positioned ? positioned + 1 : str;
}

void *find_module_return_addr(struct maps *map, const char *suffix) {
  for (size_t i = 0; i < map->size; i++) {
    const struct map *m = &map->maps[i];
    const char *file_name;

    if (!m->path || (m->perms & PROT_EXEC)) continue;

    file_name = position_after(m->path, '/');
    if (strlen(file_name) < strlen(suffix) || strncmp(file_name, suffix, strlen(suffix)) != 0) continue;

    return (void *)m->start;
  }

  return NULL;
}

void *find_module_base(struct maps *map, const char *file) {
  for (size_t i = 0; i < map->size; i++) {
    const struct map *m = &map->maps[i];
    if (!m->path || m->offset != 0) continue;
    if (strcmp(m->path, file) != 0) continue;

    return (void *)m->start;
  }

  return NULL;
}

void *find_func_addr(struct maps *local_info, struct maps *remote_info, const char *module, const char *func) {
  uint8_t *local_base = (uint8_t *)find_module_base(local_info, module);
  if (local_base == NULL) {
    LOGD("failed to find local base for module %s", module);

    return NULL;
  }

  uint8_t *remote_base = (uint8_t *)find_module_base(remote_info, module);
  if (remote_base == NULL) {
    LOGD("failed to find remote base for module %s", module);

    return NULL;
  }

  LOGD("found local base %p remote base %p", local_base, remote_base);

  ElfImg *mod = ElfImg_create(module, local_base);
  if (mod == NULL) {
    LOGW("failed to create elf img %s", module);

    return NULL;
  }

  uint8_t *sym = (uint8_t *)getSymbAddress(mod, func);
  if (sym == NULL) {
    LOGD("failed to find symbol %s in %s", func, module);

    ElfImg_destroy(mod);

    return NULL;
  }

  LOGD("found symbol %s in %s: %p", func, module, sym);

  uintptr_t addr = (uintptr_t)(sym - local_base) + (uintptr_t)remote_base;
  LOGD("addr %p", (void *)addr);

  ElfImg_destroy(mod);

  return (void *)addr;
}

void align_stack(struct user_regs_struct *regs, long preserve) {
  /* INFO: ~0xf is a negative value, and REG_SP is unsigned,
             so we must cast REG_SP to signed type before subtracting
             then cast back to unsigned type.
  */
  regs->REG_SP = (uintptr_t)((intptr_t)(regs->REG_SP - preserve) & ~0xf);
}

uintptr_t remote_call(int pid, struct user_regs_struct *regs, uintptr_t func_addr, uintptr_t return_addr, long *args, size_t args_size) {
  align_stack(regs, 0);

  LOGV("calling remote function %" PRIxPTR " args %zu", func_addr, args_size);

  for (size_t i = 0; i < args_size; i++) {
    LOGV("arg %p", (void *)args[i]);
  }

  #if defined(__x86_64__)
    if (args_size >= 1) regs->rdi = args[0];
    if (args_size >= 2) regs->rsi = args[1];
    if (args_size >= 3) regs->rdx = args[2];
    if (args_size >= 4) regs->rcx = args[3];
    if (args_size >= 5) regs->r8 = args[4];
    if (args_size >= 6) regs->r9 = args[5];
    if (args_size > 6) {
      long remain = (args_size - 6L) * sizeof(long);
      align_stack(regs, remain);

      if (!write_proc(pid, (uintptr_t) regs->REG_SP, &args[6], remain)) LOGE("failed to push arguments");
    }

    regs->REG_SP -= sizeof(long);

    if (!write_proc(pid, (uintptr_t) regs->REG_SP, &return_addr, sizeof(return_addr))) LOGE("failed to write return addr");

    regs->REG_IP = func_addr;
  #elif defined(__i386__)
    if (args_size > 0) {
      long remain = (args_size) * sizeof(long);
      align_stack(regs, remain);

      if (!write_proc(pid, (uintptr_t) regs->REG_SP, args, remain)) LOGE("failed to push arguments");
    }

    regs->REG_SP -= sizeof(long);

    if (!write_proc(pid, (uintptr_t) regs->REG_SP, &return_addr, sizeof(return_addr))) LOGE("failed to write return addr");

    regs->REG_IP = func_addr;
  #elif defined(__aarch64__)
    for (size_t i = 0; i < args_size && i < 8; i++) {
      regs->regs[i] = args[i];
    }

    if (args_size > 8) {
      long remain = (args_size - 8) * sizeof(long);
      align_stack(regs, remain);

      write_proc(pid, (uintptr_t)regs->REG_SP, &args[8], remain);
    }

    regs->regs[30] = return_addr;
    regs->REG_IP = func_addr;
  #elif defined(__arm__)
    for (size_t i = 0; i < args_size && i < 4; i++) {
      regs->uregs[i] = args[i];
    }

    if (args_size > 4) {
      long remain = (args_size - 4) * sizeof(long);
      align_stack(regs, remain);

      write_proc(pid, (uintptr_t)regs->REG_SP, &args[4], remain);
    }

    regs->uregs[14] = return_addr;
    regs->REG_IP = func_addr;

    unsigned long CPSR_T_MASK = 1lu << 5;

    if ((regs->REG_IP & 1) != 0) {
      regs->REG_IP = regs->REG_IP & ~1;
      regs->uregs[16] = regs->uregs[16] | CPSR_T_MASK;
    } else {
      regs->uregs[16] = regs->uregs[16] & ~CPSR_T_MASK;
    }
  #endif

  if (!set_regs(pid, regs)) {
    LOGE("failed to set regs");

    return 0;
  }

  ptrace(PTRACE_CONT, pid, 0, 0);

  int status;
  wait_for_trace(pid, &status, __WALL);
  if (!get_regs(pid, regs)) {
    LOGE("failed to get regs after call");

    return 0;
  }

  if (WSTOPSIG(status) == SIGSEGV) {
    if ((uintptr_t)regs->REG_IP != return_addr) {
      LOGE("wrong return addr %p", (void *) regs->REG_IP);

      return 0;
    }

    return regs->REG_RET;
  } else {
    char status_str[64];
    parse_status(status, status_str, sizeof(status_str));

    LOGE("stopped by other reason %s at addr %p", status_str, (void *)regs->REG_IP);
  }

  return 0;
}

int fork_dont_care() {
  pid_t pid = fork();

  if (pid < 0) PLOGE("fork 1");
  else if (pid == 0) {
    pid = fork();
    if (pid < 0) PLOGE("fork 2");
    else if (pid > 0) exit(0);
  } else {
    int status;
    waitpid(pid, &status, __WALL);
  }

  return pid;
}

uintptr_t find_syscall_gadget(int pid, struct maps *remote_map) {
  /* INFO: Find a syscall instruction (svc #0 on aarch64, svc 0 on arm32,
           syscall on x86_64, int 0x80 on i386) in executable memory.
           We search vdso first as it's always present. */

  #if defined(__aarch64__)
    const uint32_t svc_insn = 0xD4000001; /* svc #0 */
    const size_t insn_size = 4;
    const uintptr_t insn_bias = 0;
  #elif defined(__arm__)
    /* ARM Thumb: 0xDF00 (svc 0) or ARM: 0xEF000000 (svc 0) */
    const uint32_t svc_insn = 0xDF00;
    const size_t insn_size = 2;
    const uintptr_t insn_bias = 1;
  #elif defined(__x86_64__)
    const uint16_t svc_insn = 0x050F; /* syscall */
    const size_t insn_size = 2;
    const uintptr_t insn_bias = 0;
  #elif defined(__i386__)
    const uint16_t svc_insn = 0x80CD; /* int 0x80 */
    const size_t insn_size = 2;
    const uintptr_t insn_bias = 0;
  #endif

  for (int pass = 0; pass < 2; pass++) {
    bool vdso_only = pass == 0;

    for (size_t i = 0; i < remote_map->size; i++) {
      const struct map *m = &remote_map->maps[i];
      bool is_vdso = m->path && strstr(m->path, "[vdso]") != NULL;
      size_t region_size = m->end - m->start;

      if (!(m->perms & PROT_EXEC) || is_vdso != vdso_only) continue;

      if (region_size > (vdso_only ? 0x10000 : 0x100000))
        region_size = vdso_only ? 0x10000 : 0x100000;

      uint8_t *buf = malloc(region_size);
      if (!buf) continue;

      if (read_proc(pid, m->start, buf, region_size) != (ssize_t)region_size) {
        free(buf);

        continue;
      }

      for (size_t j = 0; j + insn_size <= region_size; j += insn_size) {
        if (memcmp(buf + j, &svc_insn, insn_size) != 0) continue;

        uintptr_t addr = m->start + j + insn_bias;

        LOGD("found syscall gadget in %s at offset 0x%zx", vdso_only ? "vdso" : (m->path ? m->path : "<anon>"), j);

        free(buf);

        return addr;
      }

      free(buf);
    }
  }

  LOGE("Failed to find syscall gadget in remote process");

  return 0;
}

#ifdef __aarch64__

bool tango_step_to_syscall(int pid) {
  while (true) {
    int status;
    wait_for_trace(pid, &status, __WALL);

    if (!WIFSTOPPED(status)) return false;
    if (WSTOPSIG(status) == (SIGTRAP | 0x80)) return true;

    ptrace(PTRACE_SYSCALL, pid, 0, (status >> 16) ? 0 : WSTOPSIG(status));
  }
}

bool tango_drain_to_event_stop(int pid) {
  while (true) {
    int status;
    wait_for_trace(pid, &status, __WALL);

    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP && (status >> 16) == PTRACE_EVENT_STOP)
      return true;

    if (!WIFSTOPPED(status)) return false;

    ptrace(PTRACE_CONT, pid, 0, (status >> 16) ? 0 : WSTOPSIG(status));
  }
}

static bool tango_init_linker_watch(int pid, struct maps *remote_map, struct tango_linker_watch *watch) {
  memset(watch, 0, sizeof(*watch));

  for (size_t i = 0; i < remote_map->size; i++) {
    const struct map *m = &remote_map->maps[i];
    if (!m->path || (uintptr_t)m->start >= 0x100000000ULL || m->offset != 0 || !strstr(m->path, "app_process32")) continue;

    struct elf_32 *img = elf_32_create(m->path);
    if (!img) {
      LOGD("Failed to parse ELF '%s'", m->path);

      return false;
    }

    uint32_t load_bias = (uint32_t)(uintptr_t)m->start - (uint32_t)img->bias;
    Elf32_Addr got_off = elf_32_find_plt_got_offset(img, "__libc_init");

    elf_32_destroy(img);

    if (!got_off) {
      LOGD("Failed to find __libc_init in JMPREL of '%s'", m->path);

      return false;
    }

    watch->libc_init_got_slot = got_off + load_bias;

    break;
  }

  if (!watch->libc_init_got_slot) return false;

  if (read_proc(pid, (uintptr_t)watch->libc_init_got_slot, &watch->libc_init_initial, 4) != 4) {
    LOGD("Failed to read __libc_init GOT@0x%x", watch->libc_init_got_slot);

    memset(watch, 0, sizeof(*watch));

    return false;
  }

  return true;
}

bool tango_wait_linker_ready(int pid, struct tango_linker_watch *watch) {
  while (true) {
    if (!watch->libc_init_got_slot) {
      char maps_path[64];
      snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

      struct maps *remote_map = parse_maps(maps_path);
      if (!remote_map) {
        LOGE("Failed to parse remote maps for pid %d", pid);

        return false;
      }

      if (tango_init_linker_watch(pid, remote_map, watch)) {
        LOGI("Found __libc_init GOT@0x%x (initial=0x%x), waiting for linker", watch->libc_init_got_slot, watch->libc_init_initial);
      }

      free_maps(remote_map);
    } else {
      uint32_t got_current = 0;

      if (read_proc(pid, (uintptr_t)watch->libc_init_got_slot, &got_current, 4) == 4 && got_current != 0 && got_current != watch->libc_init_initial) {
        watch->libc_init_resolved = got_current;

        LOGI("Resolved __libc_init (0x%x -> 0x%x, pid %d)", watch->libc_init_initial, got_current, pid);

        return true;
      }
    }

    if (ptrace(PTRACE_SYSCALL, pid, 0, 0) == -1) {
      PLOGE("Failed to step syscall");
      return false;
    }

    if (!tango_step_to_syscall(pid)) {
      LOGE("Process %d died while waiting for injection point", pid);

      return false;
    }
  }
}

uint32_t find_tramp_padding(int pid, uint32_t rx_start, uint32_t rx_end, size_t needed) {
  uint32_t map_size = rx_end - rx_start;
  int page_count = (int)(map_size / 0x1000);
  if (page_count > 8) page_count = 8;

  uint32_t scan_start = rx_end - (uint32_t)page_count * 0x1000;
  uint32_t zero_run_end = rx_end;

  for (int page = 0; page < page_count; page++) {
    uint32_t page_addr = rx_end - (uint32_t)(page + 1) * 0x1000;
    uint8_t buf[0x1000];
    if (read_proc(pid, (uintptr_t)page_addr, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) break;

    for (int off = (int)sizeof(buf) - 1; off >= 0; off--) {
      if (buf[off] == 0) continue;

      uint32_t candidate = (page_addr + (uint32_t)(off + 1) + 3) & ~3u;
      if (zero_run_end >= candidate && (size_t)(zero_run_end - candidate) >= needed) return candidate;

      zero_run_end = page_addr + (uint32_t)off;
    }
  }

  uint32_t candidate = (scan_start + 3) & ~3u;
  if (zero_run_end >= candidate && (size_t)(zero_run_end - candidate) >= needed) return candidate;

  LOGD("Failed to find %zu-byte trampoline padding in 0x%x-0x%x", needed, rx_start, rx_end);

  return 0;
}

/* INFO: This allows to bypass RELRO memory protection */
bool ptrace_poke_u32(pid_t pid, uintptr_t addr, uint32_t value) {
  uintptr_t aligned = addr & ~(uintptr_t)7;
  uintptr_t shift = (addr & (uintptr_t)7) * 8;

  errno = 0;
  unsigned long data = (unsigned long)ptrace(PTRACE_PEEKDATA, pid, (void *)aligned, 0);
  if (errno != 0) {
    PLOGE("ptrace peekdata at 0x%" PRIxPTR, addr);

    return false;
  }

  unsigned long masked = data & ~((unsigned long)0xFFFFFFFFu << shift);
  unsigned long patched = masked | ((unsigned long)value << shift);
  if (ptrace(PTRACE_POKEDATA, pid, (void *)aligned, (void *)patched) == -1) {
    PLOGE("ptrace pokedata at 0x%" PRIxPTR, addr);

    return false;
  }

  return true;
}

uintptr_t find_arm32_ret_gadget(int pid, struct maps *remote_map) {
  const uint16_t bx_lr = 0x4770;

  for (size_t i = 0; i < remote_map->size; i++) {
    const struct map *m = &remote_map->maps[i];
    if (!(m->perms & PROT_EXEC)) continue;
    if ((uintptr_t)m->start >= 0x100000000ULL) continue;

    size_t region_size = (uintptr_t)m->end - (uintptr_t)m->start;
    if (region_size > 0x10000) region_size = 0x10000;

    uint8_t *buf = malloc(region_size);
    if (!buf) continue;

    if (read_proc(pid, (uintptr_t)m->start, buf, region_size) != (ssize_t)region_size) {
      free(buf);

      continue;
    }

    for (size_t j = 0; j + 2 <= region_size; j += 2) {
      if (memcmp(buf + j, &bx_lr, sizeof(bx_lr)) != 0) continue;

      uintptr_t addr = (uintptr_t)m->start + j + 1;

      free(buf);

      LOGD("found arm32 ret gadget (BX LR) at 0x%" PRIxPTR " in %s",addr - 1, m->path ? m->path : "<anon>");

      return addr;
    }

    free(buf);
  }

  LOGE("Failed to find arm32 ret gadget in 32-bit guest regions");

  return 0;
}
#endif /* __aarch64__ */

#ifdef __aarch64__
  #define AARCH64_PSTATE_BTYPE_MASK (3ull << 10)
#endif

long remote_syscall(int pid, struct user_regs_struct *regs, uintptr_t syscall_gadget, long sysnr, long *args, size_t args_size) {
  LOGV("remote syscall %ld args %zu at gadget %p", sysnr, args_size, (void *)syscall_gadget);

  long ret = 0;

  #if defined(__aarch64__)
    struct user_regs_struct saved_regs = *regs;

    /* x8 = syscall number, x0-x5 = args */
    regs->regs[8] = sysnr;
    for (size_t i = 0; i < 6; i++) {
      regs->regs[i] = 0;
    }
    for (size_t i = 0; i < args_size && i < 6; i++) {
      regs->regs[i] = args[i];
    }
    regs->REG_IP = syscall_gadget;
    /* INFO: BTYPE so stepping the aarch64 vDSO svc will be accepted by the CPU */
    regs->pstate &= ~AARCH64_PSTATE_BTYPE_MASK;
  #elif defined(__arm__)
    /* r7 = syscall number, r0-r5 = args */
    regs->uregs[7] = sysnr;
    for (size_t i = 0; i < 6; i++) {
      regs->uregs[i] = 0;
    }
    for (size_t i = 0; i < args_size && i < 6; i++) {
      regs->uregs[i] = args[i];
    }
    regs->REG_IP = syscall_gadget;

    /* INFO: Handle Thumb mode */
    unsigned long CPSR_T_MASK = 1lu << 5;
    if ((syscall_gadget & 1) != 0) {
      regs->REG_IP = syscall_gadget & ~1;
      regs->uregs[16] |= CPSR_T_MASK;
    } else {
      regs->uregs[16] &= ~CPSR_T_MASK;
    }
  #elif defined(__x86_64__)
    /* rax = syscall number, rdi,rsi,rdx,r10,r8,r9 = args */
    regs->REG_SYSNR = sysnr;
    regs->rax = sysnr;
    regs->rdi = 0;
    regs->rsi = 0;
    regs->rdx = 0;
    regs->r10 = 0;
    regs->r8 = 0;
    regs->r9 = 0;

    if (args_size >= 1) regs->rdi = args[0];
    if (args_size >= 2) regs->rsi = args[1];
    if (args_size >= 3) regs->rdx = args[2];
    if (args_size >= 4) regs->r10 = args[3];
    if (args_size >= 5) regs->r8 = args[4];
    if (args_size >= 6) regs->r9 = args[5];
    regs->REG_IP = syscall_gadget;
  #elif defined(__i386__)
    /* eax = syscall number, ebx,ecx,edx,esi,edi,ebp = args */
    regs->REG_SYSNR = sysnr;
    regs->eax = sysnr;
    regs->ebx = 0;
    regs->ecx = 0;
    regs->edx = 0;
    regs->esi = 0;
    regs->edi = 0;
    regs->ebp = 0;

    if (args_size >= 1) regs->ebx = args[0];
    if (args_size >= 2) regs->ecx = args[1];
    if (args_size >= 3) regs->edx = args[2];
    if (args_size >= 4) regs->esi = args[3];
    if (args_size >= 5) regs->edi = args[4];
    if (args_size >= 6) regs->ebp = args[5];
    regs->REG_IP = syscall_gadget;
  #endif

  if (!set_regs(pid, regs)) {
    LOGE("failed to set regs for syscall");

    return -1;
  }

  /* INFO: Single-step through the syscall instruction */
  if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) == -1) {
    PLOGE("PTRACE_SINGLESTEP");

    ret = -1;
    goto restore_regs;
  }

  int status = 0;
  int step_retries = 0;
  while (1) {
    pid_t waited = waitpid(pid, &status, __WALL);
    if (waited == -1) {
      if (errno == EINTR) continue;
      PLOGE("waitpid after PTRACE_SINGLESTEP");

      ret = -1;
      goto restore_regs;
    }
    if (waited != pid) continue;

    if (!WIFSTOPPED(status)) {
      char status_str[64];
      parse_status(status, status_str, sizeof(status_str));
      LOGE("remote syscall stop is not ptrace-stop: %s", status_str);

      ret = -1;
      goto restore_regs;
    }

    int stop_sig = WSTOPSIG(status);
    int stop_event = (status >> 16) & 0xff;

    if ((stop_sig == SIGSTOP || stop_sig == SIGTRAP) && stop_event == PTRACE_EVENT_STOP) {
      if (step_retries++ >= 4) {
        char status_str[64];
        parse_status(status, status_str, sizeof(status_str));
        LOGE("remote syscall stuck in ptrace-stop: %s", status_str);

        ret = -1;
        goto restore_regs;
      }

      LOGV("remote syscall got pending ptrace-stop, re-single-step (retry %d)", step_retries);

      if (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) == -1) {
        PLOGE("PTRACE_SINGLESTEP retry");

        ret = -1;
        goto restore_regs;
      }

      continue;
    }

    if (stop_sig == SIGTRAP && stop_event == 0) break;

    char status_str[64];
    parse_status(status, status_str, sizeof(status_str));
    LOGE("remote syscall unexpected stop: %s", status_str);

    ret = -1;
    goto restore_regs;
  }

  if (!get_regs(pid, regs)) {
    LOGE("failed to get regs after syscall");

    ret = -1;
    goto restore_regs;
  }

  /* INFO: Extract the return value */
  ret = (long)regs->REG_RET;

  restore_regs:
    #ifdef __aarch64__
      *regs = saved_regs;
      if (!set_regs(pid, regs)) LOGE("failed to restore regs after syscall error");

      return ret;
    #endif

    return ret;
}

void tracee_skip_syscall(int pid) {
  struct user_regs_struct regs;
  if (!get_regs(pid, &regs)) {
    LOGE("Failed to get seccomp regs");

    exit(1);
  }

  regs.REG_SYSNR = -1;
  if (!set_regs(pid, &regs)) {
    LOGE("Failed to set seccomp regs");

    exit(1);
  }

  /* INFO: It might not work, don't check for error */
  #if defined(__aarch64__)
    int sysnr = -1;
    struct iovec iov = {
      .iov_base = &sysnr,
      .iov_len = sizeof(int),
    };
    ptrace(PTRACE_SETREGSET, pid, NT_ARM_SYSTEM_CALL, &iov);
  #elif defined(__arm__)
    ptrace(PTRACE_SET_SYSCALL, pid, 0, (void *) -1);
  #endif
}

void wait_for_trace(int pid, int *status, int flags) {
  while (1) {
    pid_t result = waitpid(pid, status, flags);
    if (result == -1) {
      if (errno == EINTR) continue;

      PLOGE("wait %d failed", pid);
      /* INFO: Allow the caller can detect the failure */
      *status = 255 << 8; /* INFO: WIFEXITED, WEXITSTATUS=255 */

      return;
    }

    /* INFO: We'll fork there. This will signal SIGCHLD. We just ignore and continue
               to avoid blocking/not continuing. */
    if (WIFSTOPPED(*status) && WSTOPSIG(*status) == SIGCHLD) {
      LOGI("process %d stopped by SIGCHLD, continue", pid);

      ptrace(PTRACE_CONT, pid, 0, 0);

      continue;
    } else if (*status >> 8 == (SIGTRAP | (PTRACE_EVENT_SECCOMP << 8))) {
      tracee_skip_syscall(pid);

      ptrace(PTRACE_CONT, pid, 0, 0);

      continue;
    } else if (!WIFSTOPPED(*status)) {
      char status_str[64];
      parse_status(*status, status_str, sizeof(status_str));

      LOGE("process %d not stopped for trace: %s", pid, status_str);

      /* INFO: Return the status to the caller instead of killing the tracer */
      return;
    }

    return;
  }
}

void parse_status(int status, char *buf, size_t len) {
  snprintf(buf, len, "0x%x ", status);

  if (WIFEXITED(status)) {
    snprintf(buf + strlen(buf), len - strlen(buf), "exited with %d", WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    snprintf(buf + strlen(buf), len - strlen(buf), "signaled with %s(%d)", sigabbrev_np(WTERMSIG(status)), WTERMSIG(status));
  } else if (WIFSTOPPED(status)) {
    snprintf(buf + strlen(buf), len - strlen(buf), "stopped by ");

    int stop_sig = WSTOPSIG(status);
    snprintf(buf + strlen(buf), len - strlen(buf), "signal=%s(%d),", sigabbrev_np(stop_sig), stop_sig);
    snprintf(buf + strlen(buf), len - strlen(buf), "event=%s", parse_ptrace_event(status));
  } else {
    snprintf(buf + strlen(buf), len - strlen(buf), "unknown");
  }
}

int get_program(int pid, char *buf, size_t size) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/exe", pid);

  ssize_t sz = readlink(path, buf, size);

  if (sz == -1) {
    PLOGE("readlink /proc/%d/exe", pid);

    return -1;
  }

  buf[sz] = '\0';

  return 0;
}
