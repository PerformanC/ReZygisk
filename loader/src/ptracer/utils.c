#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <ctype.h>
#include <inttypes.h>
#include <linux/limits.h>

#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/sysmacros.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "elf_util.h"

#include "utils.h"

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

void get_addr_mem_region(struct maps_info *info, uintptr_t addr, char *buf, size_t buf_size) {
  for (size_t i = 0; i < info->length; i++) {
    const struct map_entry  *m = &info->maps[i];
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

void *find_module_return_addr(struct maps_info *map, const char *suffix) {
  for (size_t i = 0; i < map->length; i++) {
    const struct map_entry  *m = &map->maps[i];
    const char *file_name;

    if (!m->path || (m->perms & PROT_EXEC)) continue;

    file_name = position_after(m->path, '/');
    if (strlen(file_name) < strlen(suffix) || strncmp(file_name, suffix, strlen(suffix)) != 0) continue;

    return (void *)m->start;
  }

  return NULL;
}

void *find_module_base(struct maps_info *map, const char *file) {
  for (size_t i = 0; i < map->length; i++) {
    const struct map_entry  *m = &map->maps[i];
    if (!m->path || m->offset != 0) continue;
    if (strcmp(m->path, file) != 0) continue;

    return (void *)m->start;
  }

  return NULL;
}

void *find_func_addr(struct maps_info *local_info, struct maps_info *remote_info, const char *module, const char *func) {
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

      if (write_proc(pid, (uintptr_t) regs->REG_SP, &args[6], remain) != remain)
        LOGE("failed to push arguments");
    }

    regs->REG_SP -= sizeof(long);

    if (write_proc(pid, (uintptr_t) regs->REG_SP, &return_addr, sizeof(return_addr)) != sizeof(return_addr))
      LOGE("failed to write return addr");

    regs->REG_IP = func_addr;
  #elif defined(__i386__)
    if (args_size > 0) {
      long remain = (args_size) * sizeof(long);
      align_stack(regs, remain);

      if (write_proc(pid, (uintptr_t) regs->REG_SP, args, remain) != remain)
        LOGE("failed to push arguments");
    }

    regs->REG_SP -= sizeof(long);

    if (write_proc(pid, (uintptr_t) regs->REG_SP, &return_addr, sizeof(return_addr)) != sizeof(return_addr))
      LOGE("failed to write return addr");

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

uintptr_t find_syscall_gadget(int pid, struct maps_info *remote_map) {
  /* INFO: Find a syscall instruction (svc #0 on aarch64, svc 0 on arm32,
           syscall on x86_64, int 0x80 on i386) in executable memory.
           We search vdso first as it's always present. */

  #if defined(__aarch64__)
    const uint32_t svc_insn = 0xD4000001; /* svc #0 */
    const size_t insn_size = 4;
    const uintptr_t insn_bias = 0;
  #elif defined(__arm__)
    const uint16_t thumb_svc_insn = 0xDF00;
    const uint32_t arm_svc_insn = 0xEF000000;
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

    for (size_t i = 0; i < remote_map->length; i++) {
      const struct map_entry  *m = &remote_map->maps[i];
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

      /* INFO: The binary, in ARM32, might contain either ARM or Thumb instructions
                 depending of how it was compiled. So, for safety, we included both
                 as possibilities for the syscall gadget. Thumb instruction set is
                 different, so take it in consideration too. */
      #ifdef __arm__
        for (size_t j = 0; j + sizeof(arm_svc_insn) <= region_size; j += sizeof(uint32_t)) {
          if (memcmp(buf + j, &arm_svc_insn, sizeof(arm_svc_insn)) != 0) continue;

          LOGD("found ARM syscall gadget in %s at offset 0x%zx", vdso_only ? "vdso" : (m->path ? m->path : "<anon>"), j);

          free(buf);

          return m->start + j;
        }

        for (size_t j = 0; j + sizeof(thumb_svc_insn) <= region_size; j += sizeof(uint16_t)) {
          if (memcmp(buf + j, &thumb_svc_insn, sizeof(thumb_svc_insn)) != 0) continue;

          LOGD("found Thumb syscall gadget in %s at offset 0x%zx", vdso_only ? "vdso" : (m->path ? m->path : "<anon>"), j);

          free(buf);

          return m->start + j + 1;
        }
      #else
        for (size_t j = 0; j + insn_size <= region_size; j += insn_size) {
          if (memcmp(buf + j, &svc_insn, insn_size) != 0) continue;

          uintptr_t addr = m->start + j + insn_bias;

          LOGD("found syscall gadget in %s at offset 0x%zx", vdso_only ? "vdso" : (m->path ? m->path : "<anon>"), j);

          free(buf);

          return addr;
        }
      #endif

      free(buf);
    }
  }

  LOGE("Failed to find syscall gadget in remote process");

  return 0;
}


bool wait_for_event_stop(int pid) {
  while (1) {
    int status;
    wait_for_trace(pid, &status, __WALL);

    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP && (status >> 16) == PTRACE_EVENT_STOP)
      return true;

    if (!WIFSTOPPED(status)) return false;

    if (ptrace(PTRACE_CONT, pid, 0, (status >> 16) ? 0 : WSTOPSIG(status)) == -1) {
      PLOGE("PTRACE_CONT while draining to EVENT_STOP");

      return false;
    }
  }
}

#ifndef R_ARM_JUMP_SLOT
  #define R_ARM_JUMP_SLOT 22
#endif

static bool elf32_vaddr_to_off(const Elf32_Phdr *phdr, int phnum, Elf32_Addr vaddr, off_t *out_off) {
  for (int i = 0; i < phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    Elf32_Addr seg_start = phdr[i].p_vaddr;
    Elf32_Addr seg_end = phdr[i].p_vaddr + phdr[i].p_filesz;
    if (vaddr < seg_start || vaddr >= seg_end) continue;

    *out_off = (off_t)phdr[i].p_offset + (off_t)(vaddr - seg_start);

    return true;
  }

  return false;
}

static bool find_jump_slot_got_offset_elf32(const char *elf_path, const char *symbol, uint32_t *out_bias, uint32_t *out_got_off) {
  int fd = open(elf_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    PLOGE("open ELF32 %s", elf_path);

    return false;
  }

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(Elf32_Ehdr)) {
    LOGE("Failed to stat ELF32 %s", elf_path);

    close(fd);

    return false;
  }

  Elf32_Ehdr eh;
  if (pread(fd, &eh, sizeof(eh), 0) != (ssize_t)sizeof(eh)) {
    LOGE("Failed to read ELF32 header");

    close(fd);

    return false;
  }

  if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS32 || eh.e_phnum == 0) {
    LOGE("Invalid ELF32 header in %s", elf_path);

    close(fd);

    return false;
  }

  Elf32_Phdr *phdr = calloc(eh.e_phnum, sizeof(Elf32_Phdr));
  if (!phdr) {
    LOGE("Failed to allocate memory for program headers");

    close(fd);

    return false;
  }

  if (pread(fd, phdr, sizeof(Elf32_Phdr) * eh.e_phnum, eh.e_phoff) != (ssize_t)(sizeof(Elf32_Phdr) * eh.e_phnum)) {
    LOGE("Failed to read program headers");

    free(phdr);
    close(fd);

    return false;
  }

  Elf32_Addr min_vaddr = UINT32_MAX;
  Elf32_Addr dyn_vaddr = 0;
  Elf32_Word dyn_size = 0;
  for (int i = 0; i < eh.e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD && phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
    if (phdr[i].p_type == PT_DYNAMIC) {
      dyn_vaddr = phdr[i].p_vaddr;
      dyn_size = phdr[i].p_filesz;
    }
  }

  if (!dyn_vaddr || !dyn_size || min_vaddr == UINT32_MAX) {
    free(phdr);
    close(fd);

    return false;
  }

  off_t dyn_off = 0;
  if (!elf32_vaddr_to_off(phdr, eh.e_phnum, dyn_vaddr, &dyn_off)) {
    free(phdr);
    close(fd);

    return false;
  }

  size_t dyn_count = dyn_size / sizeof(Elf32_Dyn);
  Elf32_Dyn *dyn = calloc(dyn_count, sizeof(Elf32_Dyn));
  if (!dyn) {
    free(phdr);
    close(fd);

    return false;
  }

  if (pread(fd, dyn, dyn_count * sizeof(Elf32_Dyn), dyn_off) != (ssize_t)(dyn_count * sizeof(Elf32_Dyn))) {
    free(dyn);
    free(phdr);
    close(fd);

    return false;
  }

  Elf32_Addr jmprel = 0, symtab = 0, strtab = 0;
  Elf32_Word pltrelsz = 0, pltrel = 0;
  for (size_t i = 0; i < dyn_count; i++) {
    switch (dyn[i].d_tag) {
      case DT_JMPREL: jmprel = dyn[i].d_un.d_ptr; break;
      case DT_PLTRELSZ: pltrelsz = dyn[i].d_un.d_val; break;
      case DT_PLTREL: pltrel = dyn[i].d_un.d_val; break;
      case DT_SYMTAB: symtab = dyn[i].d_un.d_ptr; break;
      case DT_STRTAB: strtab = dyn[i].d_un.d_ptr; break;
      default: break;
    }
  }

  if (!jmprel || !pltrelsz || !symtab || !strtab || !(pltrel == DT_REL || pltrel == DT_RELA)) {
    LOGE("Failed to find necessary dynamic entries in %s", elf_path);

    free(dyn);
    free(phdr);
    close(fd);

    return false;
  }

  off_t rel_off = 0, sym_off = 0, str_off = 0;
  if (!elf32_vaddr_to_off(phdr, eh.e_phnum, jmprel, &rel_off) ||
      !elf32_vaddr_to_off(phdr, eh.e_phnum, symtab, &sym_off) ||
      !elf32_vaddr_to_off(phdr, eh.e_phnum, strtab, &str_off)
  ) {
    LOGE("Failed to convert necessary virtual addresses to file offsets in %s", elf_path);

    free(dyn);
    free(phdr);
    close(fd);

    return false;
  }

  bool found = false;
  size_t entsz = (pltrel == DT_REL) ? sizeof(Elf32_Rel) : sizeof(Elf32_Rela);
  size_t count = pltrelsz / entsz;
  for (size_t i = 0; i < count; i++) {
    Elf32_Addr r_offset = 0;
    Elf32_Word r_info = 0;

    if (pltrel == DT_REL) {
      Elf32_Rel rel;
      if (pread(fd, &rel, sizeof(rel), rel_off + (off_t)(i * sizeof(rel))) != (ssize_t)sizeof(rel)) break;

      r_offset = rel.r_offset;
      r_info = rel.r_info;
    } else {
      Elf32_Rela rela;
      if (pread(fd, &rela, sizeof(rela), rel_off + (off_t)(i * sizeof(rela))) != (ssize_t)sizeof(rela)) break;

      r_offset = rela.r_offset;
      r_info = rela.r_info;
    }

    if (ELF32_R_TYPE(r_info) != R_ARM_JUMP_SLOT) continue;
    Elf32_Word sym_index = ELF32_R_SYM(r_info);

    Elf32_Sym sym;
    if (pread(fd, &sym, sizeof(sym), sym_off + (off_t)(sym_index * sizeof(sym))) != (ssize_t)sizeof(sym)) continue;
    if (sym.st_name == 0) continue;

    char name[128] = { 0 };
    if (pread(fd, name, sizeof(name) - 1, str_off + (off_t)sym.st_name) <= 0) continue;
    if (strcmp(name, symbol) != 0) continue;

    *out_bias = min_vaddr;
    *out_got_off = r_offset;
    found = true;

    break;
  }

  free(dyn);
  free(phdr);
  close(fd);

  return found;
}


bool tango_wait_linker_ready(int pid, struct tango_linker_watch *watch) {
  while (1) {
    if (!watch->libc_init_got_slot) {
      /* INFO: The character limit for a 32-bit integer is 10 */
      char pid_str[10 + 1];
      snprintf(pid_str, sizeof(pid_str), "%d", pid);

      struct maps_info *remote_map = parse_maps(pid_str);
      if (!remote_map) {
        LOGE("Failed to parse remote maps for pid %d", pid);

        return false;
      }

      memset(watch, 0, sizeof(*watch));
      for (size_t i = 0; i < remote_map->length; i++) {
        const struct map_entry  *m = &remote_map->maps[i];
        if (!m->path || (uintptr_t)m->start >= 0x100000000ULL || m->offset != 0 || !strstr(m->path, "app_process32")) continue;

        uint32_t bias = 0, got_off = 0;
        if (!find_jump_slot_got_offset_elf32(m->path, "__libc_init", &bias, &got_off)) {
          LOGD("Failed to find __libc_init in JMPREL of '%s'", m->path);

          continue;
        }

        watch->libc_init_got_slot = ((uint32_t)(uintptr_t)m->start - bias) + got_off;

        break;
      }

      if (watch->libc_init_got_slot && read_proc(pid, (uintptr_t)watch->libc_init_got_slot, &watch->libc_init_initial, 4) == 4) {
        LOGI("Found __libc_init GOT@0x%x (initial=0x%x), waiting for linker", watch->libc_init_got_slot, watch->libc_init_initial);
      } else if (watch->libc_init_got_slot) {
        LOGD("Failed to read __libc_init GOT@0x%x", watch->libc_init_got_slot);
        memset(watch, 0, sizeof(*watch));
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

    int status = 0;
    if (!wait_for_ptrace_syscall_stop(pid, &status)) {
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
  uintptr_t word_mask = (uintptr_t)(sizeof(unsigned long) - 1);
  uintptr_t aligned = addr & ~word_mask;
  uintptr_t shift = (addr & word_mask) * 8;

  errno = 0;
  unsigned long data = (unsigned long)ptrace(PTRACE_PEEKDATA, pid, (void *)aligned, 0);
  if (errno != 0) {
    PLOGE("ptrace peekdata at 0x%" PRIxPTR, addr);

    return false;
  }

  uint64_t lane_mask64 = (uint64_t)0xFFFFFFFFu << shift;
  unsigned long masked = data & (unsigned long)~lane_mask64;
  unsigned long patched = masked | (unsigned long)((uint64_t)value << shift);
  if (ptrace(PTRACE_POKEDATA, pid, (void *)aligned, (void *)patched) == -1) {
    PLOGE("ptrace pokedata at 0x%" PRIxPTR, addr);

    return false;
  }

  return true;
}

uintptr_t find_arm32_ret_gadget(int pid, struct maps_info *remote_map) {
  const uint16_t bx_lr = 0x4770;

  for (size_t i = 0; i < remote_map->length; i++) {
    const struct map_entry  *m = &remote_map->maps[i];
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

#ifdef __aarch64__
  #define AARCH64_PSTATE_BTYPE_MASK (3ull << 10)
#endif

bool wait_for_ptrace_syscall_stop(int pid, int *status) {
  int step_retries = 0;
  while (1) {
    pid_t waited = waitpid(pid, status, __WALL);
    if (waited == -1) {
      if (errno == EINTR) continue;

      PLOGE("waitpid");

      return false;
    }

    if (waited != pid) continue;

    if (!WIFSTOPPED(*status)) {
      char status_str[64];
      parse_status(*status, status_str, sizeof(status_str));
      LOGE("Remote syscall stop is not ptrace-stop: %s", status_str);

      return false;
    }

    int stop_sig = WSTOPSIG(*status);
    int stop_event = (*status >> 16) & 0xff;
    bool is_syscall_stop = stop_event == 0 && (stop_sig == SIGTRAP || stop_sig == (SIGTRAP | 0x80));

    if ((stop_sig == SIGSTOP || stop_sig == SIGTRAP) && stop_event == PTRACE_EVENT_STOP) {
      if (step_retries++ >= 4) {
        char status_str[64];
        parse_status(*status, status_str, sizeof(status_str));
        LOGE("Remote syscall stuck in ptrace-stop: %s", status_str);

        return false;
      }

      LOGV("Remote syscall got pending ptrace-stop, retrying (retry %d)", step_retries);

      if (ptrace(PTRACE_SYSCALL, pid, 0, 0) == -1) {
        PLOGE("PTRACE_SYSCALL retry");

        return false;
      }

      continue;
    }

    if (is_syscall_stop) return true;

    char status_str[64];
    parse_status(*status, status_str, sizeof(status_str));
    LOGE("Remote syscall unexpected stop: %s", status_str);

    return false;
  }
}

long remote_syscall(int pid, struct user_regs_struct *regs, uintptr_t syscall_gadget, long sysnr, long *args, size_t args_size) {
  LOGV("Remote syscall %ld args %zu at gadget %p", sysnr, args_size, (void *)syscall_gadget);

  long ret = -1;

  /* Save tracee's current register state (all architectures) */
  struct user_regs_struct saved_regs;
  if (!get_regs(pid, &saved_regs)) {
    LOGE("Failed to get regs for save");

    return -1;
  }

  /* Use *regs as scratch for syscall setup */
  #if defined(__aarch64__)
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
    LOGE("Failed to set regs for syscall");

    goto restore_regs;
  }

  /* INFO: We must perform this code twice. The first time is to step into the syscall entry,
             and the second time is to step out of the syscall exit. */
  for (int i = 0; i < 2; i++) {
    if (ptrace(PTRACE_SYSCALL, pid, 0, 0) == -1) {
      PLOGE("PTRACE_SYSCALL");

      ret = -1;
      goto restore_regs;
    }

    int status;
    if (!wait_for_ptrace_syscall_stop(pid, &status)) goto restore_regs;

    if (i == 0)
      LOGV("Remote syscall %ld got PTRACE_SYSCALL entry-stop, continuing to exit-stop", sysnr);
  }

  if (!get_regs(pid, regs)) {
    LOGE("Failed to get regs after PTRACE_SYSCALL");

    ret = -1;
    goto restore_regs;
  }

  ret = (long)regs->REG_RET;

  LOGV("Remote syscall %ld succeeded: %ld", sysnr, ret);

  restore_regs:
    *regs = saved_regs;
    if (!set_regs(pid, regs)) LOGE("Failed to restore regs after syscall");

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

  if ((size_t)sz >= size) {
    LOGW("Program path truncated (%zd >= %zu)", sz, size);

    sz = size - 1;
  }

  buf[sz] = '\0';

  return 0;
}
