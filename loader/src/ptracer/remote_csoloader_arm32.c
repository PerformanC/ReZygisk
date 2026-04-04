/* INFO: Remote CSOLoader, part of CSOLoader. Follows the same licensing
           as the original one (CSOLoader project). */

#include "remote_csoloader_arm32.h"

#ifdef __aarch64__

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <elf.h>

#include "elf_util_32.h"
#include "socket_utils.h"

#ifndef ALIGN_DOWN
  #define ALIGN_DOWN(x, a) ((x) & ~((a)-1))
#endif
#ifndef ALIGN_UP
  #define ALIGN_UP(x, a) (((x) + ((a)-1)) & ~((a)-1))
#endif

#ifndef R_ARM_RELATIVE
  #define R_ARM_RELATIVE   23
#endif
#ifndef R_ARM_GLOB_DAT
  #define R_ARM_GLOB_DAT   21
#endif
#ifndef R_ARM_JUMP_SLOT
  #define R_ARM_JUMP_SLOT  22
#endif
#ifndef R_ARM_ABS32
  #define R_ARM_ABS32       2
#endif

#ifndef MAP_FIXED_NOREPLACE
  #define MAP_FIXED_NOREPLACE 0x100000
#endif

static uint32_t page_start(uint32_t addr, uint32_t page_size) {
  return ALIGN_DOWN(addr, page_size);
}

static uint32_t page_end(uint32_t addr, uint32_t page_size) {
  return ALIGN_UP(addr, page_size);
}

static bool compute_load_layout(int fd, uint32_t page_size, Elf32_Ehdr *eh, Elf32_Phdr **out_phdr, Elf32_Addr *out_min_vaddr, uint32_t *out_map_size) {
  if (!read_loop_offset(fd, eh, sizeof(*eh), 0)) {
    LOGE("Failed to read ELF header");

    return false;
  }

  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) {
    LOGE("Invalid ELF magic");

    return false;
  }

  if (eh->e_ident[EI_CLASS] != ELFCLASS32) {
    LOGE("Invalid ELF class");

    return false;
  }

  if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(Elf32_Phdr)) {
    LOGE("Invalid program headers");

    return false;
  }

  size_t phdr_sz = (size_t)eh->e_phnum * eh->e_phentsize;
  Elf32_Phdr *phdr = (Elf32_Phdr *)malloc(phdr_sz);
  if (!phdr) {
    LOGE("Failed to allocate memory for program headers");

    return false;
  }

  if (!read_loop_offset(fd, phdr, phdr_sz, (off_t)eh->e_phoff)) {
    LOGE("Failed to read program headers");

    free(phdr);

    return false;
  }

  Elf32_Addr lo = UINT32_MAX;
  Elf32_Addr hi = 0;

  for (int i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;
    if (phdr[i].p_vaddr < lo) lo = phdr[i].p_vaddr;

    Elf32_Addr end = phdr[i].p_vaddr + phdr[i].p_memsz;
    if (end > hi) hi = end;
  }

  if (hi <= lo) {
    LOGE("Invalid PT_LOAD segments");

    free(phdr);

    return false;
  }

  lo = page_start(lo, page_size);
  hi = page_end(hi, page_size);

  if (out_min_vaddr) *out_min_vaddr = lo;
  if (out_map_size) *out_map_size = hi - lo;
  if (out_phdr) *out_phdr = phdr;

  return true;
}

/* INFO: Convert a virtual address to file offset using PT_LOAD segment mapping. */
static bool vaddr_to_offset(const Elf32_Phdr *phdr, int phnum, Elf32_Addr vaddr, off_t *out_off) {
  for (int i = 0; i < phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    Elf32_Addr seg_start = phdr[i].p_vaddr;
    Elf32_Addr seg_end = phdr[i].p_vaddr + phdr[i].p_filesz;

    if (vaddr < seg_start || vaddr >= seg_end) continue;

    *out_off = (off_t)phdr[i].p_offset + (off_t)(vaddr - seg_start);

    return true;
  }

  LOGE("Failed to find vaddr to offset mapping for vaddr: 0x%x", vaddr);

  return false;
}

static const char *find_remote_module_path(struct maps *remote_map, const char *soname) {
  for (size_t i = 0; i < remote_map->size; i++) {
    const struct map *m = &remote_map->maps[i];

    if (!m->path) continue;
    if (m->offset != 0) continue;

    const char *filename = position_after(m->path, '/');
    if (!filename) filename = m->path;

    if (strcmp(filename, soname) == 0) return m->path;
  }

  return NULL;
}

struct elf_dyn_info {
  off_t symtab_off;
  off_t strtab_off;
  off_t rel_off;
  size_t rel_sz;
  off_t jmprel_off;
  size_t jmprel_sz;

  size_t syment;
  size_t strsz;
  size_t nsyms;

  char *strtab;
  size_t needed_count;
  size_t *needed_str_offsets;
};

static void elf_dyn_info_destroy(struct elf_dyn_info *info) {
  if (!info) return;

  free(info->strtab);
  free(info->needed_str_offsets);
  memset(info, 0, sizeof(*info));
}

static bool elf_load_dyn_info(int fd, const Elf32_Ehdr *eh, const Elf32_Phdr *phdr, struct elf_dyn_info *out) {
  memset(out, 0, sizeof(*out));
  out->syment = sizeof(Elf32_Sym);

  Elf32_Dyn *dyn = NULL;
  size_t *needed_str_offsets = NULL;
  bool success = false;

  const Elf32_Phdr *dyn_phdr = NULL;
  for (int i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type != PT_DYNAMIC) continue;

    dyn_phdr = &phdr[i];

    break;
  }

  if (!dyn_phdr || dyn_phdr->p_filesz == 0) {
    LOGE("Failed to find PT_DYNAMIC");

    return false;
  }

  size_t dyn_count = dyn_phdr->p_filesz / sizeof(Elf32_Dyn);

  dyn = (Elf32_Dyn *)calloc(dyn_count, sizeof(Elf32_Dyn));
  if (!dyn) {
    LOGE("Failed to allocate memory for dynamic entries");

    return false;
  }

  if (!read_loop_offset(fd, dyn, dyn_count * sizeof(Elf32_Dyn), (off_t)dyn_phdr->p_offset)) {
    LOGE("Failed to read dynamic entries");

    goto cleanup;
  }

  Elf32_Addr symtab_vaddr = 0;
  Elf32_Addr strtab_vaddr = 0;
  Elf32_Addr hash_vaddr = 0;
  Elf32_Addr rel_vaddr = 0;
  Elf32_Addr jmprel_vaddr = 0;
  size_t rel_sz = 0;
  size_t jmprel_sz = 0;
  size_t strsz = 0;
  size_t syment = 0;

  size_t needed_count = 0;
  for (size_t i = 0; i < dyn_count; i++) {
    if (dyn[i].d_tag == DT_NEEDED) needed_count++;
    if (dyn[i].d_tag == DT_NULL) break;
  }

  if (needed_count) {
    needed_str_offsets = (size_t *)calloc(needed_count, sizeof(size_t));
    if (!needed_str_offsets) {
      LOGE("Failed to allocate memory for DT_NEEDED offsets");

      goto cleanup;
    }
  }

  size_t needed_i = 0;
  for (size_t i = 0; i < dyn_count; i++) {
    uintptr_t tag = (uintptr_t)dyn[i].d_tag;
    switch (tag) {
      case DT_SYMTAB:   symtab_vaddr = dyn[i].d_un.d_ptr; break;
      case DT_STRTAB:   strtab_vaddr = dyn[i].d_un.d_ptr; break;
      case DT_STRSZ:    strsz = dyn[i].d_un.d_val; break;
      case DT_SYMENT:   syment = dyn[i].d_un.d_val; break;
      case DT_REL:      rel_vaddr = dyn[i].d_un.d_ptr; break;
      case DT_RELSZ:    rel_sz = dyn[i].d_un.d_val; break;
      case DT_JMPREL:   jmprel_vaddr = dyn[i].d_un.d_ptr; break;
      case DT_PLTRELSZ: jmprel_sz = dyn[i].d_un.d_val; break;
      case DT_HASH:     hash_vaddr = dyn[i].d_un.d_ptr; break;
      case DT_NEEDED: {
        if (needed_str_offsets && needed_i < needed_count)
          needed_str_offsets[needed_i++] = dyn[i].d_un.d_val;

        break;
      }
      case DT_NULL: i = dyn_count; break;
    }
  }

  if (!syment) syment = sizeof(Elf32_Sym);

  if (!symtab_vaddr || !strtab_vaddr || !strsz) {
    LOGE("Missing DT_SYMTAB/DT_STRTAB/DT_STRSZ");

    goto cleanup;
  }

  if (!vaddr_to_offset(phdr, eh->e_phnum, symtab_vaddr, &out->symtab_off) || !vaddr_to_offset(phdr, eh->e_phnum, strtab_vaddr, &out->strtab_off)) {
    LOGE("Failed vaddr_to_offset for symtab/strtab");

    goto cleanup;
  }

  if (rel_vaddr && rel_sz) {
    if (!vaddr_to_offset(phdr, eh->e_phnum, rel_vaddr, &out->rel_off)) {
      LOGE("Failed vaddr_to_offset for DT_REL");

      goto cleanup;
    }
    out->rel_sz = rel_sz;
  }

  if (jmprel_vaddr && jmprel_sz) {
    if (!vaddr_to_offset(phdr, eh->e_phnum, jmprel_vaddr, &out->jmprel_off)) {
      LOGE("Failed vaddr_to_offset for DT_JMPREL");

      goto cleanup;
    }
    out->jmprel_sz = jmprel_sz;
  }

  out->strtab = (char *)malloc(strsz + 1);
  if (!out->strtab) {
    LOGE("Failed to allocate memory for string table");

    goto cleanup;
  }

  if (!read_loop_offset(fd, out->strtab, strsz, out->strtab_off)) {
    LOGE("Failed to read string table");

    free(out->strtab);
    out->strtab = NULL;

    goto cleanup;
  }
  out->strtab[strsz] = '\0';

  out->syment = syment;
  out->strsz = strsz;
  out->needed_count = needed_count;
  out->needed_str_offsets = needed_str_offsets;

  if (hash_vaddr) {
    off_t hash_off = 0;

    if (vaddr_to_offset(phdr, eh->e_phnum, hash_vaddr, &hash_off)) {
      uint32_t hash_hdr[2];

      if (read_loop_offset(fd, hash_hdr, sizeof(hash_hdr), hash_off))
        out->nsyms = hash_hdr[1];
    }
  }

  success = true;

cleanup:
  free(dyn);
  if (!success) free(needed_str_offsets);

  return success;
}

/* INFO: Look up a symbol by name in the dynamic symbol table. */
static bool find_dynsym_value(int fd, const struct elf_dyn_info *info, const char *sym_name, Elf32_Addr *out_value) {
  for (size_t i = 0; i < info->nsyms; i++) {
    Elf32_Sym sym;
    if (!read_loop_offset(fd, &sym, sizeof(sym), info->symtab_off + (off_t)(i * info->syment)))
      break;

    if (sym.st_name == 0 || sym.st_name >= info->strsz) continue;

    const char *name = &info->strtab[sym.st_name];
    if (strcmp(name, sym_name) != 0 || sym.st_shndx == SHN_UNDEF) continue;

    *out_value = sym.st_value;

    return true;
  }

  return false;
}

/* INFO: Find a free gap in the 32-bit address range of the remote process. */
static uint32_t find_32bit_mmap_gap(struct maps *remote_map, uint32_t needed_size, uint32_t page_size) {
  needed_size = page_end(needed_size, page_size);
  uint32_t search_start = 0x10000;

  for (size_t i = 0; i < remote_map->size; i++) {
    uintptr_t map_start = (uintptr_t)remote_map->maps[i].start;
    uintptr_t map_end = (uintptr_t)remote_map->maps[i].end;

    if (map_start >= 0x100000000ULL) break;

    uint32_t ms = (uint32_t)map_start;
    uint32_t me = map_end > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)map_end;

    if (ms > search_start) {
      uint32_t aligned = page_end(search_start, page_size);
      if (aligned >= search_start && aligned + needed_size <= ms) return aligned;
    }

    if (me > search_start) search_start = me;
  }

  if (search_start < 0xFFFF0000u) {
    uint32_t aligned = page_end(search_start, page_size);
    if (aligned >= search_start && aligned + needed_size <= 0xFFFF0000u) return aligned;
  }

  return 0;
}

static bool read_remote_addr(int pid, uintptr_t addr, uint32_t *out) {
  return read_proc(pid, addr, out, sizeof(*out)) == (ssize_t)sizeof(*out);
}

static bool write_remote_addr(int pid, uintptr_t addr, uint32_t value) {
  return write_proc(pid, addr, &value, sizeof(value)) == (ssize_t)sizeof(value);
}

bool arm32_find_remote_symbol(struct maps *remote_map, const char *lib_path, const char *sym_name, uint32_t *out_addr) {
  long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0) {
    LOGE("sysconf(_SC_PAGESIZE) failed");

    return false;
  }

  const char *filename = position_after(lib_path, '/');
  if (!filename) filename = lib_path;

  uint32_t base = 0;
  for (size_t i = 0; i < remote_map->size; i++) {
    const struct map *m = &remote_map->maps[i];

    if (!m->path || m->offset != 0) continue;

    const char *remote_name = position_after(m->path, '/');
    if (!remote_name) remote_name = m->path;
    if (strcmp(remote_name, filename) != 0) continue;

    base = (uint32_t)(uintptr_t)m->start;
    break;
  }

  if (!base) {
    LOGE("Failed to find remote base for module %s", lib_path);

    return false;
  }

  int fd = open(lib_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    PLOGE("open %s", lib_path);

    return false;
  }

  Elf32_Ehdr eh;
  Elf32_Phdr *phdr = NULL;
  if (!compute_load_layout(fd, (uint32_t)page_size_long, &eh, &phdr, NULL, NULL)) {
    close(fd);

    return false;
  }

  struct elf_dyn_info dinfo;
  bool found = false;
  Elf32_Addr value = 0;

  if (elf_load_dyn_info(fd, &eh, phdr, &dinfo)) {
    found = find_dynsym_value(fd, &dinfo, sym_name, &value);
    elf_dyn_info_destroy(&dinfo);
  }

  free(phdr);
  close(fd);

  if (!found) return false;

  *out_addr = base + value;

  return true;
}

/* INFO: Resolve a symbol address - either local or from DT_NEEDED libraries. */
static bool resolve_symbol_addr(int fd, const struct elf_dyn_info *info,
                                struct maps *remote_map, const char *const *needed_paths,
                                uint32_t load_bias, size_t sym_idx, uintptr_t stub_gadget,
                                uint32_t *out_addr) {
  Elf32_Sym sym;

  if (!read_loop_offset(fd, &sym, sizeof(sym), info->symtab_off + (off_t)(sym_idx * info->syment)))
    return false;

  if (sym.st_shndx != SHN_UNDEF) {
    *out_addr = load_bias + sym.st_value;

    return true;
  }

  if (sym.st_name == 0 || sym.st_name >= info->strsz) return false;

  const char *name = &info->strtab[sym.st_name];
  if (!name || !*name) return false;

  if (strcmp(name, "__register_frame") == 0 || strcmp(name, "__deregister_frame") == 0) {
    LOGW("Bypassing resolution of EH frame function: %s", name);

    *out_addr = 0;

    return true;
  }

  for (size_t i = 0; i < info->needed_count; i++) {
    const char *path = needed_paths ? needed_paths[i] : NULL;
    if (!path) continue;

    uint32_t addr = 0;
    if (!arm32_find_remote_symbol(remote_map, path, name, &addr)) continue;

    *out_addr = addr;

    return true;
  }

  if (ELF32_ST_BIND(sym.st_info) == STB_WEAK) {
    if (stub_gadget) {
      LOGW("Stubbing weak symbol %s -> 0x%" PRIxPTR, name, stub_gadget);
      *out_addr = (uint32_t)stub_gadget;
    } else {
      LOGW("Weak symbol %s unresolved, setting to 0", name);
      *out_addr = 0;
    }

    return true;
  }

  LOGE("Failed to resolve external symbol %s", name);

  return false;
}

/* INFO: Process REL-format relocations from a given offset/size. */
static bool apply_rel_section(int pid, int fd, const struct elf_dyn_info *info,
                              struct maps *remote_map, const char *const *needed_paths,
                              uint32_t load_bias, uintptr_t stub_gadget,
                              off_t rel_off, size_t rel_sz) {
  size_t count = rel_sz / sizeof(Elf32_Rel);

  for (size_t i = 0; i < count; i++) {
    Elf32_Rel r;
    if (!read_loop_offset(fd, &r, sizeof(r), rel_off + (off_t)(i * sizeof(r)))) return false;

    unsigned type = (unsigned)ELF32_R_TYPE(r.r_info);
    unsigned sym = (unsigned)ELF32_R_SYM(r.r_info);
    uintptr_t target = (uintptr_t)load_bias + (uintptr_t)r.r_offset;
    uint32_t addend = 0;
    uint32_t value = 0;

    if (type == R_ARM_RELATIVE) {
      if (!read_remote_addr(pid, target, &addend)) return false;

      value = load_bias + addend;
    } else if (type == R_ARM_GLOB_DAT || type == R_ARM_JUMP_SLOT || type == R_ARM_ABS32) {
      uint32_t sym_addr = 0;
      if (!resolve_symbol_addr(fd, info, remote_map, needed_paths, load_bias, sym, stub_gadget, &sym_addr))
        return false;

      if (sym_addr == 0) value = 0;
      else if (type == R_ARM_ABS32) {
        if (!read_remote_addr(pid, target, &addend)) return false;

        value = sym_addr + addend;
      } else {
        value = sym_addr;
      }
    } else if (type == 0) {
      continue;
    } else {
      LOGE("Unsupported ARM REL type %u", type);

      return false;
    }

    if (!write_remote_addr(pid, target, value)) return false;
  }

  return true;
}

static bool apply_relocations(int pid, int fd, const struct elf_dyn_info *info,
                              struct maps *remote_map, const char *const *needed_paths,
                              uint32_t load_bias, uintptr_t stub_gadget) {
  if (info->rel_sz && info->rel_off) {
    if (!apply_rel_section(pid, fd, info, remote_map, needed_paths, load_bias, stub_gadget, info->rel_off, info->rel_sz))
      return false;
  }

  if (info->jmprel_sz && info->jmprel_off) {
    if (!apply_rel_section(pid, fd, info, remote_map, needed_paths, load_bias, stub_gadget, info->jmprel_off, info->jmprel_sz))
      return false;
  }

  return true;
}

bool arm32_csoloader_load(int pid, struct user_regs_struct *regs,
                          struct maps *remote_map,
                          const char *lib_path,
                          uint32_t *out_base, uint32_t *out_size,
                          uint32_t *out_entry) {
  const struct user_regs_struct regs_saved = *regs;

  uintptr_t syscall_gadget = find_syscall_gadget(pid, remote_map);
  if (!syscall_gadget) {
    LOGE("Failed to find syscall gadget");

    return false;
  }

  uintptr_t stub_gadget = find_arm32_ret_gadget(pid, remote_map);
  if (!stub_gadget) {
    LOGE("Failed to find arm32 ret gadget");

    return false;
  }

  long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0) {
    LOGE("sysconf(_SC_PAGESIZE) failed");

    return false;
  }

  uint32_t page_size = (uint32_t)page_size_long;

  int fd = open(lib_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    PLOGE("open %s", lib_path);

    return false;
  }

  Elf32_Ehdr eh;
  Elf32_Phdr *phdr = NULL;
  Elf32_Addr min_vaddr = 0;
  uint32_t map_size = 0;

  if (!compute_load_layout(fd, page_size, &eh, &phdr, &min_vaddr, &map_size)) {
    LOGE("Failed to parse ELF phdrs for %s", lib_path);

    close(fd);

    return false;
  }

  uint32_t gap_addr = find_32bit_mmap_gap(remote_map, map_size, page_size);
  if (!gap_addr) {
    LOGE("Failed to find free 32-bit gap for %u bytes", map_size);

    free(phdr);
    close(fd);

    return false;
  }

  long args[6];
  args[0] = (long)(uintptr_t)gap_addr;
  args[1] = (long)map_size;
  args[2] = PROT_NONE;
  args[3] = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
  args[4] = -1;
  args[5] = 0;

  struct user_regs_struct call_regs = regs_saved;
  uintptr_t remote_base_64 = (uintptr_t)remote_syscall(pid, &call_regs, syscall_gadget, __NR_mmap, args, 6);
  if (!remote_base_64 || remote_base_64 == (uintptr_t)MAP_FAILED) {
    LOGW("MAP_FIXED_NOREPLACE at 0x%x failed, trying hint-only", gap_addr);

    args[3] = MAP_PRIVATE | MAP_ANONYMOUS;
    call_regs = regs_saved;
    remote_base_64 = (uintptr_t)remote_syscall(pid, &call_regs, syscall_gadget, __NR_mmap, args, 6);
  }

  if (!remote_base_64 || remote_base_64 == (uintptr_t)MAP_FAILED) {
    LOGE("remote mmap reserve failed");

    free(phdr);
    close(fd);

    return false;
  }

  if (remote_base_64 >= 0x100000000ULL) {
    LOGE("remote mmap returned 64-bit addr 0x%" PRIxPTR, remote_base_64);

    call_regs = regs_saved;
    args[0] = (long)remote_base_64;
    args[1] = (long)map_size;
    remote_syscall(pid, &call_regs, syscall_gadget, __NR_munmap, args, 2);

    free(phdr);
    close(fd);

    return false;
  }

  uint32_t remote_base = (uint32_t)remote_base_64;
  uint32_t load_bias = remote_base - min_vaddr;

  size_t path_len = strlen(lib_path) + 1;
  uintptr_t remote_path = regs_saved.REG_SP - ALIGN_UP(path_len, 16);
  if (write_proc(pid, remote_path, lib_path, path_len) != (ssize_t)path_len) {
    LOGE("Failed to write remote path string to stack");

    free(phdr);
    close(fd);

    return false;
  }

  call_regs = regs_saved;
  args[0] = AT_FDCWD;
  args[1] = (long)remote_path;
  args[2] = O_RDONLY | O_CLOEXEC;
  args[3] = 0;

  /* INFO: Ensure remote_call's own stack usage stays below our string */
  call_regs.REG_SP = remote_path;

  long remote_fd = remote_syscall(pid, &call_regs, syscall_gadget, __NR_openat, args, 4);
  if (remote_fd < 0) {
    LOGE("Failed to open remote file: %s (%ld)", lib_path, remote_fd);

    call_regs = regs_saved;
    args[0] = (long)remote_base;
    args[1] = (long)map_size;
    remote_syscall(pid, &call_regs, syscall_gadget, __NR_munmap, args, 2);

    free(phdr);
    close(fd);

    return false;
  }

  void *remote_path_zerod = calloc(1, ALIGN_UP(path_len, 16));
  if (!remote_path_zerod) {
    LOGE("Failed to allocate memory for zeroed path");

    call_regs = regs_saved;
    args[0] = remote_fd;
    remote_syscall(pid, &call_regs, syscall_gadget, SYS_close, args, 1);

    free(phdr);
    close(fd);

    return false;
  }

  if (write_proc(pid, remote_path, remote_path_zerod, ALIGN_UP(path_len, 16)) != (ssize_t)ALIGN_UP(path_len, 16)) {
    LOGE("Failed to zero remote path string on stack");

    free(remote_path_zerod);

    call_regs = regs_saved;
    args[0] = remote_fd;
    remote_syscall(pid, &call_regs, syscall_gadget, SYS_close, args, 1);

    free(phdr);
    close(fd);

    return false;
  }

  free(remote_path_zerod);

  struct {
    uint32_t addr;
    uint32_t len;
    int final_prot;
  } segs[64];

  size_t segs_count = 0;

  for (int i = 0; i < eh.e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    uint32_t seg_start = load_bias + phdr[i].p_vaddr;
    uint32_t seg_page = page_start(seg_start, page_size);
    uint32_t seg_end = load_bias + phdr[i].p_vaddr + phdr[i].p_memsz;
    uint32_t seg_page_end = page_end(seg_end, page_size);
    uint32_t seg_page_len = seg_page_end - seg_page;

    bool is_writable = (phdr[i].p_flags & PF_W) != 0;

    if (is_writable) {
      off_t seg_offset = (off_t)phdr[i].p_offset;
      off_t file_page_offset = (off_t)page_start((uint32_t)seg_offset, page_size);
      uint32_t file_end = load_bias + phdr[i].p_vaddr + phdr[i].p_filesz;
      uint32_t file_page_end = page_end(file_end, page_size);

      if (phdr[i].p_filesz > 0) {
        size_t file_map_len = (size_t)(file_page_end - seg_page);
        args[0] = (long)seg_page;
        args[1] = (long)file_map_len;
        args[2] = PROT_READ | PROT_WRITE;
        args[3] = MAP_FIXED | MAP_PRIVATE;
        args[4] = remote_fd;
        args[5] = file_page_offset;

        call_regs = regs_saved;
        long seg_ret = remote_syscall(pid, &call_regs, syscall_gadget, __NR_mmap, args, 6);
        if (seg_ret < 0) {
          LOGE("remote mmap writable file-backed segment failed for phdr %d", i);

          call_regs = regs_saved;
          args[0] = remote_fd;
          remote_syscall(pid, &call_regs, syscall_gadget, __NR_close, args, 1);

          free(phdr);
          close(fd);

          return false;
        }

        if (file_page_end > file_end) {
          size_t tail_len = (size_t)(file_page_end - file_end);
          void *zeros = calloc(1, tail_len);
          if (!zeros || write_proc(pid, file_end, zeros, tail_len) != (ssize_t)tail_len) {
            LOGE("Failed to zero tail for phdr %d", i);

            free(zeros);
            call_regs = regs_saved;
            args[0] = remote_fd;
            remote_syscall(pid, &call_regs, syscall_gadget, __NR_close, args, 1);

            free(phdr);
            close(fd);

            return false;
          }

          free(zeros);
        }
      }

      if (seg_page_end > file_page_end) {
        args[0] = (long)file_page_end;
        args[1] = (long)(seg_page_end - file_page_end);
        args[2] = PROT_READ | PROT_WRITE;
        args[3] = MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS;
        args[4] = -1;
        args[5] = 0;

        call_regs = regs_saved;
        long bss_ret = remote_syscall(pid, &call_regs, syscall_gadget, __NR_mmap, args, 6);
        if (bss_ret < 0) {
          LOGE("remote mmap bss segment failed for phdr %d", i);

          call_regs = regs_saved;
          args[0] = remote_fd;
          remote_syscall(pid, &call_regs, syscall_gadget, __NR_close, args, 1);

          free(phdr);
          close(fd);

          return false;
        }
      }
    } else {
      off_t seg_offset = (off_t)phdr[i].p_offset;
      off_t file_page_offset = (off_t)page_start((uint32_t)seg_offset, page_size);
      uint32_t file_end = load_bias + phdr[i].p_vaddr + phdr[i].p_filesz;
      uint32_t file_page_end = page_end(file_end, page_size);
      size_t file_map_len = (size_t)(file_page_end - seg_page);
      int prot = 0;

      if (phdr[i].p_flags & PF_R) prot |= PROT_READ;
      if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;

      args[0] = (long)seg_page;
      args[1] = (long)file_map_len;
      args[2] = prot;
      args[3] = MAP_FIXED | MAP_PRIVATE;
      args[4] = remote_fd;
      args[5] = file_page_offset;

      call_regs = regs_saved;
      long seg_ret = remote_syscall(pid, &call_regs, syscall_gadget, __NR_mmap, args, 6);
      if (seg_ret < 0) {
        LOGE("remote mmap file-backed segment failed for phdr %d", i);

        call_regs = regs_saved;
        args[0] = remote_fd;
        remote_syscall(pid, &call_regs, syscall_gadget, __NR_close, args, 1);

        free(phdr);
        close(fd);

        return false;
      }
    }

    int prot = 0;
    if (phdr[i].p_flags & PF_R) prot |= PROT_READ;
    if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
    if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;

    if (segs_count < (sizeof(segs) / sizeof(segs[0]))) {
      segs[segs_count].addr = seg_page;
      segs[segs_count].len = seg_page_len;
      segs[segs_count].final_prot = prot;
      segs_count++;
    }
  }

  call_regs = regs_saved;
  args[0] = remote_fd;
  remote_syscall(pid, &call_regs, syscall_gadget, __NR_close, args, 1);

  struct elf_dyn_info dinfo;
  if (!elf_load_dyn_info(fd, &eh, phdr, &dinfo)) {
    LOGE("Failed to load ELF dynamic info");

    free(phdr);
    close(fd);

    return false;
  }

  const char **needed_paths = NULL;
  if (dinfo.needed_count) {
    needed_paths = (const char **)calloc(dinfo.needed_count, sizeof(char *));
    if (!needed_paths) {
      LOGE("Failed to allocate memory for needed paths");

      elf_dyn_info_destroy(&dinfo);
      free(phdr);
      close(fd);

      return false;
    }

    for (size_t i = 0; i < dinfo.needed_count; i++) {
      size_t off = dinfo.needed_str_offsets[i];
      if (off >= dinfo.strsz) continue;

      const char *soname = &dinfo.strtab[off];
      needed_paths[i] = find_remote_module_path(remote_map, soname);
    }
  }

  if (!apply_relocations(pid, fd, &dinfo, remote_map, needed_paths, load_bias, stub_gadget)) {
    LOGE("Failed to apply relocations");

    free((void *)needed_paths);
    elf_dyn_info_destroy(&dinfo);
    free(phdr);
    close(fd);

    return false;
  }

  for (size_t i = 0; i < segs_count; i++) {
    args[0] = (long)segs[i].addr;
    args[1] = (long)segs[i].len;
    args[2] = segs[i].final_prot;

    call_regs = regs_saved;
    remote_syscall(pid, &call_regs, syscall_gadget, __NR_mprotect, args, 3);
  }

  struct elf_32 *entry_img = elf_32_create(lib_path);
  Elf32_Addr entry_value = 0;
  if (!entry_img) {
    LOGE("Failed to open ELF %s for entry lookup", lib_path);

    free((void *)needed_paths);
    elf_dyn_info_destroy(&dinfo);
    free(phdr);
    close(fd);

    return false;
  }

  entry_value = elf_32_symb_offset(entry_img, "entry", NULL);
  elf_32_destroy(entry_img);

  if (!entry_value) {
    LOGE("Failed to resolve entry symbol");

    free((void *)needed_paths);
    elf_dyn_info_destroy(&dinfo);
    free(phdr);
    close(fd);

    return false;
  }

  free((void *)needed_paths);
  elf_dyn_info_destroy(&dinfo);
  free(phdr);
  close(fd);

  *out_base = remote_base;
  *out_size = map_size;
  *out_entry = load_bias + entry_value;

  LOGI("remote mapped %s at 0x%x (size %u), entry 0x%x", lib_path, remote_base, map_size, *out_entry);

  return true;
}

#endif /*__aarch64__ */
