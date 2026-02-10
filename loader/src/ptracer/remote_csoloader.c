/* INFO: Remote CSOLoader, part of CSOLoader. Follows the same licensing
           as the original one (CSOLoader project). */

#include "remote_csoloader.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <elf.h>
#include <link.h>

#include "logging.h"
#include "socket_utils.h"

#ifndef ALIGN_DOWN
  #define ALIGN_DOWN(x, a) ((x) & ~((a)-1))
#endif
#ifndef ALIGN_UP
  #define ALIGN_UP(x, a) (((x) + ((a)-1)) & ~((a)-1))
#endif

static uintptr_t page_start(uintptr_t addr, size_t page_size) {
  return ALIGN_DOWN(addr, page_size);
}

static uintptr_t page_end(uintptr_t addr, size_t page_size) {
  return ALIGN_DOWN(addr + page_size - 1, page_size);
}

/* INFO: Parse ELF headers and compute the total mapping size for PT_LOAD segments. */
static bool compute_load_layout(int fd, size_t page_size, ElfW(Ehdr) *eh,
                                ElfW(Phdr) **out_phdr, ElfW(Addr) *out_min_vaddr,
                                size_t *out_map_size) {
  if (!read_loop_offset(fd, eh, sizeof(*eh), 0)) {
    LOGE("Failed to read ELF header");

    return false;
  }

  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) {
    LOGE("Invalid ELF magic");

    return false;
  }

  size_t phdr_sz = (size_t)eh->e_phnum * sizeof(ElfW(Phdr));
  ElfW(Phdr) *phdr = (ElfW(Phdr) *)malloc(phdr_sz);
  if (!phdr) {
    LOGE("Failed to allocate memory for program headers");

    return false;
  }

  if (!read_loop_offset(fd, phdr, phdr_sz, (off_t)eh->e_phoff)) {
    LOGE("Failed to read program headers");

    free(phdr);

    return false;
  }

  /* INFO: Find min/max vaddr across all PT_LOAD segments */
  ElfW(Addr) lo = (ElfW(Addr))UINTPTR_MAX;
  ElfW(Addr) hi = 0;

  for (int i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;
    if (phdr[i].p_vaddr < lo) lo = phdr[i].p_vaddr;

    ElfW(Addr) end = phdr[i].p_vaddr + phdr[i].p_memsz;
    if (end > hi) hi = end;
  }

  if (hi <= lo) {
    LOGE("Invalid PT_LOAD segments");

    free(phdr);

    return false;
  }

  /* INFO: Page-align the address range */
  lo = (ElfW(Addr))page_start((uintptr_t)lo, page_size);
  hi = (ElfW(Addr))page_end((uintptr_t)hi, page_size);

  *out_min_vaddr = lo;
  *out_map_size = (size_t)(hi - lo);
  *out_phdr = phdr;

  return true;
}

/* INFO: Convert a virtual address to file offset using PT_LOAD segment mapping. */
static bool vaddr_to_offset(const ElfW(Phdr) *phdr, int phnum, ElfW(Addr) vaddr, off_t *out_off) {
  for (int i = 0; i < phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    ElfW(Addr) seg_start = phdr[i].p_vaddr;
    ElfW(Addr) seg_end = phdr[i].p_vaddr + phdr[i].p_filesz;

    if (vaddr < seg_start || vaddr >= seg_end) continue;

    *out_off = (off_t)phdr[i].p_offset + (off_t)(vaddr - seg_start);

    return true;
  }

  LOGE("Failed to find vaddr to offset mapping for vaddr: 0x%" PRIxPTR, (uintptr_t)vaddr);

  return false;
}

/* INFO: Find the full path of a loaded module by its soname in remote maps. */
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
  off_t dyn_off;
  size_t dyn_sz;

  off_t symtab_off;
  off_t strtab_off;
  off_t rel_off;
  size_t rel_sz;
  off_t rela_off;
  size_t rela_sz;
  off_t jmprel_off;
  size_t jmprel_sz;
  int pltrel_type;

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

/* INFO: Parse PT_DYNAMIC and extract relocation/symbol table info. */
static bool elf_load_dyn_info(int fd, const ElfW(Ehdr) *eh, const ElfW(Phdr) *phdr, struct elf_dyn_info *out) {
  memset(out, 0, sizeof(*out));
  out->pltrel_type = 0;

  ElfW(Dyn) *dyn = NULL;
  size_t *needed_str_offsets = NULL;
  bool success = false;

  /* INFO: Find PT_DYNAMIC segment */
  const ElfW(Phdr) *dyn_phdr = NULL;
  for (int i = 0; i < eh->e_phnum; i++) {
    if (phdr[i].p_type != PT_DYNAMIC) continue;

    dyn_phdr = &phdr[i];

    break;
  }

  if (!dyn_phdr || dyn_phdr->p_filesz == 0) {
    LOGE("Failed to find PT_DYNAMIC");

    return false;
  }

  out->dyn_off = (off_t)dyn_phdr->p_offset;
  out->dyn_sz = (size_t)dyn_phdr->p_filesz;

  size_t dyn_count = out->dyn_sz / sizeof(ElfW(Dyn));

  dyn = (ElfW(Dyn) *)calloc(dyn_count, sizeof(ElfW(Dyn)));
  if (!dyn) {
    LOGE("Failed to allocate memory for dynamic entries");

    return false;
  }

  if (!read_loop_offset(fd, dyn, dyn_count * sizeof(ElfW(Dyn)), out->dyn_off)) {
    LOGE("Failed to read dynamic entries");

    goto cleanup;
  }

  ElfW(Addr) symtab_vaddr = 0;
  ElfW(Addr) strtab_vaddr = 0;
  ElfW(Addr) gnu_hash_vaddr = 0;
  ElfW(Addr) rel_vaddr = 0;
  ElfW(Addr) rela_vaddr = 0;
  ElfW(Addr) jmprel_vaddr = 0;
  size_t rel_sz = 0;
  size_t rela_sz = 0;
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
      case DT_SYMTAB:    symtab_vaddr = (ElfW(Addr))dyn[i].d_un.d_ptr; break;
      case DT_STRTAB:    strtab_vaddr = (ElfW(Addr))dyn[i].d_un.d_ptr; break;
      case DT_STRSZ:     strsz = (size_t)dyn[i].d_un.d_val; break;
      case DT_SYMENT:    syment = (size_t)dyn[i].d_un.d_val; break;
      case DT_REL:       rel_vaddr = (ElfW(Addr))dyn[i].d_un.d_ptr; break;
      case DT_RELSZ:     rel_sz = (size_t)dyn[i].d_un.d_val; break;
      case DT_RELA:      rela_vaddr = (ElfW(Addr))dyn[i].d_un.d_ptr; break;
      case DT_RELASZ:    rela_sz = (size_t)dyn[i].d_un.d_val; break;
      case DT_JMPREL:    jmprel_vaddr = (ElfW(Addr))dyn[i].d_un.d_ptr; break;
      case DT_PLTRELSZ:  jmprel_sz = (size_t)dyn[i].d_un.d_val; break;
      case DT_PLTREL:    out->pltrel_type = (int)dyn[i].d_un.d_val; break;
      case DT_GNU_HASH:  gnu_hash_vaddr = (ElfW(Addr))dyn[i].d_un.d_ptr; break;
      case DT_NEEDED: {
        if (needed_str_offsets && needed_i < needed_count)
          needed_str_offsets[needed_i++] = (size_t)dyn[i].d_un.d_val;

        break;
      }
      case DT_NULL: i = dyn_count; break;
    }
  }

  /* INFO: Validate required entries */
  if (!syment) syment = sizeof(ElfW(Sym));

  if (!symtab_vaddr || !strtab_vaddr || !strsz) {
    LOGE("Missing DT_SYMTAB/DT_STRTAB/DT_STRSZ");

    goto cleanup;
  }

  /* INFO: Convert virtual addresses to file offsets */
  if (!vaddr_to_offset(phdr, eh->e_phnum, symtab_vaddr, &out->symtab_off) ||
      !vaddr_to_offset(phdr, eh->e_phnum, strtab_vaddr, &out->strtab_off)) {
    LOGE("Failed vaddr_to_offset for symtab/strtab");

    goto cleanup;
  }

  /* INFO: Convert relocation table virtual addresses to file offsets */
  if (rel_vaddr && rel_sz) {
    if (!vaddr_to_offset(phdr, eh->e_phnum, rel_vaddr, &out->rel_off)) {
      LOGE("Failed vaddr_to_offset for DT_REL");

      goto cleanup;
    }
    out->rel_sz = rel_sz;
  }

  if (rela_vaddr && rela_sz) {
    if (!vaddr_to_offset(phdr, eh->e_phnum, rela_vaddr, &out->rela_off)) {
      LOGE("Failed vaddr_to_offset for DT_RELA");

      goto cleanup;
    }
    out->rela_sz = rela_sz;
  }

  if (jmprel_vaddr && jmprel_sz) {
    if (!vaddr_to_offset(phdr, eh->e_phnum, jmprel_vaddr, &out->jmprel_off)) {
      LOGE("Failed vaddr_to_offset for DT_JMPREL");

      goto cleanup;
    }
    out->jmprel_sz = jmprel_sz;
  }

  /* INFO: Read string table into memory */
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

  out->nsyms = 0;

  if (gnu_hash_vaddr) {
    off_t gnu_hash_off = 0;

    if (vaddr_to_offset(phdr, eh->e_phnum, gnu_hash_vaddr, &gnu_hash_off)) {
      uint32_t header[4];

      if (read_loop_offset(fd, header, sizeof(header), gnu_hash_off)) {
        uint32_t nbuckets = header[0];
        uint32_t symoffset = header[1];
        uint32_t bloom_size = header[2];

        /* INFO: Calculate offset to buckets array (after bloom filter) */
        size_t bloom_words = bloom_size * sizeof(ElfW(Addr));
        off_t buckets_off = gnu_hash_off + 16 + (off_t)bloom_words;

        /* INFO: Find max bucket value to determine highest symbol index */
        uint32_t max_bucket = 0;

        for (uint32_t b = 0; b < nbuckets; b++) {
          uint32_t bucket_val;

          if (!read_loop_offset(fd, &bucket_val, sizeof(bucket_val), buckets_off + (off_t)(b * 4)))
            break;

          if (bucket_val > max_bucket) max_bucket = bucket_val;
        }

        if (max_bucket >= symoffset) {
          /* INFO: Walk chain from max_bucket to find last symbol */
          off_t chains_off = buckets_off + (off_t)(nbuckets * 4);
          uint32_t chain_idx = max_bucket - symoffset;
          uint32_t chain_val;

          while (read_loop_offset(fd, &chain_val, sizeof(chain_val), chains_off + (off_t)(chain_idx * 4))) {
            if (chain_val & 1) {
              out->nsyms = max_bucket + 1;

              break;
            }

            max_bucket++;
            chain_idx++;
          }

          if (!out->nsyms) out->nsyms = max_bucket + 1;
        } else {
          out->nsyms = symoffset;
        }
      }
    }
  }

  success = true;

cleanup:
  free(dyn);
  if (!success) free(needed_str_offsets);

  return success;
}

/* INFO: Look up a symbol by name in the dynamic symbol table. */
static bool find_dynsym_value(int fd, const struct elf_dyn_info *info, const char *sym_name, ElfW(Addr) *out_value) {
  for (size_t i = 0; i < info->nsyms; i++) {
    ElfW(Sym) sym;
    if (!read_loop_offset(fd, &sym, sizeof(sym), info->symtab_off + (off_t)(i * info->syment)))
      break;

    if (sym.st_name == 0 || sym.st_name >= info->strsz) continue;

    const char *name = &info->strtab[sym.st_name];
    if (strcmp(name, sym_name) != 0 || sym.st_shndx == SHN_UNDEF) continue;

    *out_value = sym.st_value;

    return true;
  }

  LOGE("Symbol not found in dynsym: %s", sym_name);

  return false;
}

#ifdef __LP64__
  #define ELF_R_TYPE ELF64_R_TYPE
  #define ELF_R_SYM ELF64_R_SYM
#else
  #define ELF_R_TYPE ELF32_R_TYPE
  #define ELF_R_SYM ELF32_R_SYM
#endif

/* INFO: Resolve a symbol address - either local or from DT_NEEDED libraries. */
static bool resolve_symbol_addr(int fd, const struct elf_dyn_info *info,
                                struct maps *local_map, struct maps *remote_map,
                                const char *const *needed_paths, uintptr_t load_bias,
                                size_t sym_idx, uintptr_t *out_addr) {
  ElfW(Sym) sym;

  if (!read_loop_offset(fd, &sym, sizeof(sym), info->symtab_off + (off_t)(sym_idx * info->syment)))
    return false;

  /* INFO: Defined symbol - use load_bias + value */
  if (sym.st_shndx != SHN_UNDEF) {
    *out_addr = (uintptr_t)load_bias + (uintptr_t)sym.st_value;

    return true;
  }

  /* INFO: Undefined symbol - resolve from external libraries */
  if (sym.st_name == 0 || sym.st_name >= info->strsz) return false;

  const char *name = &info->strtab[sym.st_name];
  if (!name || !*name) return false;

  /* TODO: In CSOLoader, they're optional. Since it's broken to solve those *ATM*, just bypass. */
  if (strcmp(name, "__register_frame") == 0 || strcmp(name, "__deregister_frame") == 0) {
    LOGW("Bypassing resolution of EH frame function: %s", name);

    *out_addr = 0;

    return true;
  }

  /* INFO: Search in DT_NEEDED libraries */
  for (size_t i = 0; i < info->needed_count; i++) {
    const char *mod_path = needed_paths ? needed_paths[i] : NULL;
    if (!mod_path) continue;

    void *addr = find_func_addr(local_map, remote_map, mod_path, name);
    if (addr) {
      *out_addr = (uintptr_t)addr;

      return true;
    }
  }

  /* Fallback: some vendor images strip .symtab/.hash from secondary libs.
     Many libc symbols (malloc/free/strcmp/….) are still exported from libc.so,
     so try libc explicitly to avoid repeated failures. */
  const char *libc_path = find_remote_module_path(remote_map, "libc.so");
  if (libc_path) {
    void *addr = find_func_addr(local_map, remote_map, libc_path, name);
    if (addr) {
      *out_addr = (uintptr_t)addr;

      return true;
    }
  }

  LOGE("Failed to resolve external symbol %s", name);

  return false;
}

static bool write_remote_addr(int pid, uintptr_t addr, ElfW(Addr) value) {
  return write_proc(pid, addr, &value, sizeof(value)) == (ssize_t)sizeof(value);
}

static bool read_remote_addr(int pid, uintptr_t addr, ElfW(Addr) *out) {
  return read_proc(pid, addr, out, sizeof(*out)) == (ssize_t)sizeof(*out);
}

/* INFO: Process RELA-format relocations from a given offset/size. */
static bool apply_rela_section(int pid, int fd, const struct elf_dyn_info *info,
                               struct maps *local_map, struct maps *remote_map,
                               const char *const *needed_paths, uintptr_t load_bias,
                               off_t rela_off, size_t rela_sz) {
  size_t count = rela_sz / sizeof(ElfW(Rela));

  for (size_t i = 0; i < count; i++) {
    ElfW(Rela) r;
    if (!read_loop_offset(fd, &r, sizeof(r), rela_off + (off_t)(i * sizeof(r)))) return false;

    unsigned type = (unsigned)ELF_R_TYPE(r.r_info);
    unsigned sym = (unsigned)ELF_R_SYM(r.r_info);
    uintptr_t target = (uintptr_t)load_bias + (uintptr_t)r.r_offset;
    ElfW(Addr) value = 0;

    #if defined(__aarch64__)
      if (type == R_AARCH64_RELATIVE) {
        value = (ElfW(Addr))load_bias + (ElfW(Addr))r.r_addend;
      } else if (type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_ABS64) {
        uintptr_t sym_addr = 0;
        if (!resolve_symbol_addr(fd, info, local_map, remote_map, needed_paths, load_bias, sym, &sym_addr))
          return false;

        value = sym_addr ? (ElfW(Addr))sym_addr + (ElfW(Addr))r.r_addend : 0;
      } else {
        LOGE("Unsupported AArch64 RELA type %u", type);

        return false;
      }
    #elif defined(__x86_64__)
      if (type == R_X86_64_RELATIVE) {
        value = (ElfW(Addr))load_bias + (ElfW(Addr))r.r_addend;
      } else if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT || type == R_X86_64_64) {
        uintptr_t sym_addr = 0;
        if (!resolve_symbol_addr(fd, info, local_map, remote_map, needed_paths, load_bias, sym, &sym_addr))
          return false;

        value = sym_addr ? (ElfW(Addr))sym_addr + (ElfW(Addr))r.r_addend : 0;
      } else {
        LOGE("Unsupported x86_64 RELA type %u", type);
        return false;
      }
    #else
      (void) info; (void) local_map; (void) remote_map; (void) sym; (void) type; (void) needed_paths;

      if (type == 0) value = (ElfW(Addr))load_bias + (ElfW(Addr))r.r_addend;
      else {
        LOGE("Unsupported RELA type %u", type);

        return false;
      }
    #endif

    if (!write_remote_addr(pid, target, value)) return false;
  }

  return true;
}

/* INFO: Process REL-format relocations from a given offset/size. */
static bool apply_rel_section(int pid, int fd, const struct elf_dyn_info *info,
                              struct maps *local_map, struct maps *remote_map,
                              const char *const *needed_paths, uintptr_t load_bias,
                              off_t rel_off, size_t rel_sz) {
  size_t count = rel_sz / sizeof(ElfW(Rel));

  for (size_t i = 0; i < count; i++) {
    ElfW(Rel) r;
    if (!read_loop_offset(fd, &r, sizeof(r), rel_off + (off_t)(i * sizeof(r)))) return false;

    unsigned type = (unsigned)ELF_R_TYPE(r.r_info);
    unsigned sym = (unsigned)ELF_R_SYM(r.r_info);
    uintptr_t target = (uintptr_t)load_bias + (uintptr_t)r.r_offset;
    ElfW(Addr) addend = 0;
    ElfW(Addr) value = 0;

    #if defined(__arm__)
      if (type == R_ARM_RELATIVE) {
        if (!read_remote_addr(pid, target, &addend)) return false;

        value = (ElfW(Addr))load_bias + addend;
      } else if (type == R_ARM_GLOB_DAT || type == R_ARM_JUMP_SLOT || type == R_ARM_ABS32) {
        uintptr_t sym_addr = 0;
        if (!resolve_symbol_addr(fd, info, local_map, remote_map, needed_paths, load_bias, sym, &sym_addr))
          return false;

        if (sym_addr == 0) value = 0;
        else if (type == R_ARM_ABS32) {
          if (!read_remote_addr(pid, target, &addend)) return false;

          value = (ElfW(Addr))sym_addr + addend;
        } else {
          value = (ElfW(Addr))sym_addr;
        }
      } else {
        LOGE("Unsupported ARM REL type %u", type);

        return false;
      }
    #elif defined(__i386__)
      if (type == R_386_RELATIVE) {
        if (!read_remote_addr(pid, target, &addend)) return false;

        value = (ElfW(Addr))load_bias + addend;
      } else if (type == R_386_GLOB_DAT || type == R_386_JMP_SLOT || type == R_386_32) {
        uintptr_t sym_addr = 0;
        if (!resolve_symbol_addr(fd, info, local_map, remote_map, needed_paths, load_bias, sym, &sym_addr))
          return false;

        if (sym_addr == 0) value = 0;
        else if (type == R_386_32) {
          if (!read_remote_addr(pid, target, &addend)) return false;

          value = (ElfW(Addr))sym_addr + addend;
        } else {
          value = (ElfW(Addr))sym_addr;
        }
      } else {
        LOGE("Unsupported i386 REL type %u", type);

        return false;
      }
    #else
      (void) info; (void) local_map; (void) remote_map; (void) sym; (void) type; (void) needed_paths; (void) addend; (void) read_remote_addr;

      LOGE("Unsupported REL relocation on this arch");

      return false;
    #endif

    if (!write_remote_addr(pid, target, value)) return false;
  }

  return true;
}

static bool apply_relocations(int pid, int fd, const struct elf_dyn_info *info,
                              struct maps *local_map, struct maps *remote_map,
                              const char *const *needed_paths, uintptr_t load_bias) {
  /* INFO: Process RELA section */
  if (info->rela_sz && info->rela_off) {
    if (!apply_rela_section(pid, fd, info, local_map, remote_map, needed_paths, load_bias, info->rela_off, info->rela_sz))
      return false;
  }

  /* INFO: Process REL section */
  if (info->rel_sz && info->rel_off) {
    if (!apply_rel_section(pid, fd, info, local_map, remote_map, needed_paths, load_bias, info->rel_off, info->rel_sz))
      return false;
  }

  /* INFO: Process JMPREL section (PLT relocations) - uses same format as RELA/REL */
  if (info->jmprel_sz && info->jmprel_off) {
    if (info->pltrel_type == DT_RELA) {
      if (!apply_rela_section(pid, fd, info, local_map, remote_map, needed_paths, load_bias, info->jmprel_off, info->jmprel_sz))
        return false;
    } else if (info->pltrel_type == DT_REL) {
      if (!apply_rel_section(pid, fd, info, local_map, remote_map, needed_paths, load_bias, info->jmprel_off, info->jmprel_sz))
        return false;
    } else {
      LOGE("Unknown DT_PLTREL type %d", info->pltrel_type);

      return false;
    }
  }

  return true;
}

bool remote_csoloader_load_and_resolve_entry(int pid, struct user_regs_struct *regs,
                                             uintptr_t libc_return_addr,
                                             struct maps *local_map, struct maps *remote_map,
                                             const char *libc_path, const char *lib_path,
                                             uintptr_t *out_base, size_t *out_total_size,
                                             uintptr_t *out_entry) {
  const struct user_regs_struct regs_saved = *regs;

  long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0) {
    LOGE("sysconf(_SC_PAGESIZE) failed");

    return false;
  }

  size_t page_size = (size_t)page_size_long;

  int fd = open(lib_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    PLOGE("open %s", lib_path);

    return false;
  }

  /* INFO: Parse ELF headers and compute mapping size */
  ElfW(Ehdr) eh;
  ElfW(Phdr) *phdr = NULL;
  ElfW(Addr) min_vaddr = 0;
  size_t map_size = 0;

  if (!compute_load_layout(fd, page_size, &eh, &phdr, &min_vaddr, &map_size)) {
    LOGE("Failed to parse ELF phdrs for %s", lib_path);
    close(fd);

    return false;
  }

  /* INFO: Resolve required libc functions in remote process */
  void *mmap_addr = find_func_addr(local_map, remote_map, libc_path, "mmap");
  if (!mmap_addr) {
    LOGE("Failed to resolve remote mmap");

    free(phdr);
    close(fd);

    return false;
  }

  void *mprotect_addr = find_func_addr(local_map, remote_map, libc_path, "mprotect");
  if (!mprotect_addr) {
    LOGE("Failed to resolve remote mprotect");

    free(phdr);
    close(fd);

    return false;
  }

  void *open_addr = find_func_addr(local_map, remote_map, libc_path, "open");
  if (!open_addr) {
    LOGE("Failed to resolve remote open");

    free(phdr);
    close(fd);

    return false;
  }

  void *close_addr = find_func_addr(local_map, remote_map, libc_path, "close");
  if (!close_addr) {
    LOGE("Failed to resolve remote close");

    free(phdr);
    close(fd);

    return false;
  }

  long args[6];

  /* INFO: Copy library path into remote memory for file-backed mappings */
  size_t path_len = strlen(lib_path) + 1;
  args[0] = 0;
  args[1] = (long)path_len;
  args[2] = PROT_READ | PROT_WRITE;
  args[3] = MAP_PRIVATE | MAP_ANONYMOUS;
  args[4] = -1;
  args[5] = 0;

  struct user_regs_struct call_regs = regs_saved;
  uintptr_t remote_path = remote_call(pid, &call_regs, (uintptr_t)mmap_addr, libc_return_addr, args, 6);
  if (!remote_path || remote_path == (uintptr_t)MAP_FAILED) {
    LOGE("remote mmap for path failed: %p", (void *)remote_path);

    free(phdr);
    close(fd);

    return false;
  }

  if (write_proc(pid, remote_path, lib_path, path_len) != (ssize_t)path_len) {
    LOGE("Failed to write remote path string");

    free(phdr);
    close(fd);

    return false;
  }

  args[0] = (long)remote_path;
  args[1] = O_RDONLY | O_CLOEXEC;
  args[2] = 0;

  call_regs = regs_saved;
  long remote_fd = (long)remote_call(pid, &call_regs, (uintptr_t)open_addr, libc_return_addr, args, 3);
  if (remote_fd < 0) {
    LOGE("Failed to open remote file: %s", lib_path);

    free(phdr);
    close(fd);

    return false;
  }

  /* INFO: Reserve address space with PROT_NONE */
  args[0] = 0;
  args[1] = (long)map_size;
  args[2] = PROT_NONE;
  args[3] = MAP_PRIVATE | MAP_ANONYMOUS;
  args[4] = -1;
  args[5] = 0;

  call_regs = regs_saved;
  uintptr_t remote_base = remote_call(pid, &call_regs, (uintptr_t)mmap_addr, libc_return_addr, args, 6);
  if (!remote_base || remote_base == (uintptr_t)MAP_FAILED) {
    LOGE("remote mmap reserve failed: %p", (void *)remote_base);

    call_regs = regs_saved;

    args[0] = remote_fd;
  
    remote_call(pid, &call_regs, (uintptr_t)close_addr, libc_return_addr, args, 1);

    free(phdr);
    close(fd);

    return false;
  }

  uintptr_t load_bias = remote_base - (uintptr_t)min_vaddr;

  /* INFO: Track segments for later protection finalization */
  struct {
    uintptr_t addr;
    size_t len;
    int final_prot;
  } segs[64];

  size_t segs_count = 0;

  /* INFO: Map non-writable PT_LOAD from file */
  for (int i = 0; i < eh.e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    uintptr_t seg_start = (uintptr_t)phdr[i].p_vaddr + load_bias;
    uintptr_t seg_page = page_start(seg_start, page_size);
    uintptr_t seg_end = (uintptr_t)phdr[i].p_vaddr + (uintptr_t)phdr[i].p_memsz + load_bias;
    uintptr_t seg_page_end = page_end(seg_end, page_size);
    size_t seg_page_len = (size_t)(seg_page_end - seg_page);

    bool is_writable = (phdr[i].p_flags & PF_W) != 0;

    if (is_writable) {
      off_t seg_offset = (off_t)phdr[i].p_offset;
      off_t file_page_offset = (off_t)page_start((uintptr_t)seg_offset, page_size);
      uintptr_t file_end = (uintptr_t)phdr[i].p_vaddr + (uintptr_t)phdr[i].p_filesz + load_bias;
      uintptr_t file_page_end = page_end(file_end, page_size);

      if (phdr[i].p_filesz > 0) {
        call_regs = regs_saved;

        size_t file_map_len = (size_t)(file_page_end - seg_page);
        args[0] = (long)seg_page;
        args[1] = (long)file_map_len;
        args[2] = PROT_READ | PROT_WRITE;
        args[3] = MAP_FIXED | MAP_PRIVATE;
        args[4] = remote_fd;
        args[5] = (long)file_page_offset;

        uintptr_t seg_map = remote_call(pid, &call_regs, (uintptr_t)mmap_addr, libc_return_addr, args, 6);
        if (!seg_map || seg_map == (uintptr_t)MAP_FAILED) {
          LOGE("remote mmap writable file-backed segment failed for phdr %d", i);

          call_regs = regs_saved;

          args[0] = remote_fd;
  
          remote_call(pid, &call_regs, (uintptr_t)close_addr, libc_return_addr, args, 1);
          free(phdr);
          close(fd);

          return false;
        }

        /* INFO: Zero-fill the tail of the last page (p_memsz - p_filesz within page). */
        if (file_page_end > file_end) {
          size_t tail_len = (size_t)(file_page_end - file_end);
          char *zeros = (char *)calloc(1, tail_len);
          if (!zeros || write_proc(pid, file_end, zeros, tail_len) != (ssize_t)tail_len) {
            LOGE("Failed to zero tail for phdr %d", i);

            if (zeros) free(zeros);
            free(phdr);
            close(fd);

            return false;
          }

          free(zeros);
        }
      }

      if (seg_page_end > file_page_end) {
        call_regs = regs_saved;

        args[0] = (long)file_page_end;
        args[1] = (long)(seg_page_end - file_page_end);
        args[2] = PROT_READ | PROT_WRITE;
        args[3] = MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS;
        args[4] = -1;
        args[5] = 0;

        uintptr_t bss_map = remote_call(pid, &call_regs, (uintptr_t)mmap_addr, libc_return_addr, args, 6);
        if (!bss_map || bss_map == (uintptr_t)MAP_FAILED) {
          LOGE("remote mmap bss segment failed for phdr %d", i);

          call_regs = regs_saved;

          args[0] = remote_fd;

          call_regs = regs_saved;
          remote_call(pid, &call_regs, (uintptr_t)close_addr, libc_return_addr, args, 1);
          free(phdr);
          close(fd);

          return false;
        }
      }
    } else {
      off_t seg_offset = (off_t)phdr[i].p_offset;
      off_t file_page_offset = (off_t)page_start((uintptr_t)seg_offset, page_size);
      uintptr_t file_end = (uintptr_t)phdr[i].p_vaddr + (uintptr_t)phdr[i].p_filesz + load_bias;
      uintptr_t file_page_end = page_end(file_end, page_size);

      if (phdr[i].p_filesz > 0) {
        call_regs = regs_saved;

        size_t file_map_len = (size_t)(file_page_end - seg_page);
        args[0] = (long)seg_page;
        args[1] = (long)file_map_len;
        args[2] = PROT_READ | PROT_WRITE;
        args[3] = MAP_FIXED | MAP_PRIVATE;
        args[4] = remote_fd;
        args[5] = (long)file_page_offset;

        uintptr_t seg_map = remote_call(pid, &call_regs, (uintptr_t)mmap_addr, libc_return_addr, args, 6);
        if (!seg_map || seg_map == (uintptr_t)MAP_FAILED) {
          LOGE("remote mmap file-backed segment failed for phdr %d", i);

          call_regs = regs_saved;

          args[0] = remote_fd;

          remote_call(pid, &call_regs, (uintptr_t)close_addr, libc_return_addr, args, 1);
          free(phdr);
          close(fd);

          return false;
        }
      }

      if (seg_page_end > file_page_end) {
        call_regs = regs_saved;

        args[0] = (long)file_page_end;
        args[1] = (long)(seg_page_end - file_page_end);
        args[2] = PROT_READ | PROT_WRITE;
        args[3] = MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS;
        args[4] = -1;
        args[5] = 0;

        uintptr_t bss_map = remote_call(pid, &call_regs, (uintptr_t)mmap_addr, libc_return_addr, args, 6);
        if (!bss_map || bss_map == (uintptr_t)MAP_FAILED) {
          LOGE("remote mmap bss segment failed for phdr %d", i);

          call_regs = regs_saved;

          args[0] = remote_fd;

          call_regs = regs_saved;
          remote_call(pid, &call_regs, (uintptr_t)close_addr, libc_return_addr, args, 1);
          free(phdr);
          close(fd);

          return false;
        }
      }
    }

    /* INFO: Record segment info for later protection finalization */
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

  remote_call(pid, &call_regs, (uintptr_t)close_addr, libc_return_addr, args, 1);

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

  /* INFO: Apply all relocations */
  if (!apply_relocations(pid, fd, &dinfo, local_map, remote_map, needed_paths, load_bias)) {
    LOGE("Failed to apply relocations");

    free((void *)needed_paths);
    elf_dyn_info_destroy(&dinfo);
    free(phdr);
    close(fd);

    return false;
  }

  /* INFO: Finalize segment protections after relocations */
  for (size_t i = 0; i < segs_count; i++) {
    args[0] = (long)segs[i].addr;
    args[1] = (long)segs[i].len;
    args[2] = segs[i].final_prot;

    call_regs = regs_saved;
    remote_call(pid, &call_regs, (uintptr_t)mprotect_addr, libc_return_addr, args, 3);
  }

  ElfW(Addr) entry_value = 0;
  if (!find_dynsym_value(fd, &dinfo, "entry", &entry_value)) {
    LOGE("Failed to resolve entry from ELF dynsym");

    free((void *)needed_paths);
    elf_dyn_info_destroy(&dinfo);
    free(phdr);
    close(fd);

    return false;
  }

  uintptr_t remote_entry = (uintptr_t)load_bias + (uintptr_t)entry_value;

  free((void *)needed_paths);
  elf_dyn_info_destroy(&dinfo);
  free(phdr);
  close(fd);

  *out_base = remote_base;
  *out_total_size = map_size;
  *out_entry = remote_entry;

  LOGI("remote mapped %s at %p (size %zu), entry %p", lib_path, (void *)remote_base, map_size, (void *)remote_entry);

  return true;
}
