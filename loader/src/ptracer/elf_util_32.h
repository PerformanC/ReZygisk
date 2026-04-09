#ifndef ELF_UTIL_32_H
#define ELF_UTIL_32_H

#include <stdint.h>

#ifdef __aarch64__

#include <elf.h>

#define SHT_GNU_HASH_32 0x6ffffff6

struct symtabs_32 {
  char *name;
  Elf32_Sym *sym;
};

struct elf_32 {
  char *elf;
  uint32_t base;
  Elf32_Ehdr *header;
  size_t size;
  Elf32_Sword bias;
  Elf32_Shdr *section_header;

  Elf32_Shdr *dynsym;
  Elf32_Off dynsym_offset;
  Elf32_Sym *dynsym_start;
  Elf32_Shdr *strtab;
  Elf32_Off symstr_offset;
  void *strtab_start;

  uint32_t nbucket_;
  uint32_t *bucket_;
  uint32_t *chain_;

  uint32_t gnu_nbucket_;
  uint32_t gnu_symndx_;
  uint32_t gnu_bloom_size_;
  uint32_t gnu_shift2_;
  uint32_t *gnu_bloom_filter_;
  uint32_t *gnu_bucket_;
  uint32_t *gnu_chain_;

  Elf32_Shdr *symtab;
  Elf32_Off symtab_offset;
  size_t symtab_size;
  size_t symtab_count;
  Elf32_Sym *symtab_start;
  Elf32_Off symstr_offset_for_symtab;

  struct symtabs_32 *symtabs_;
};

struct sym_info_32 {
  const char *name;
  Elf32_Addr address;
};

void elf_32_destroy(struct elf_32 *img);

struct elf_32 *elf_32_create(const char *elf);

Elf32_Addr elf_32_symb_offset(struct elf_32 *img, const char *name, unsigned char *sym_type);

Elf32_Addr elf_32_symb_address(struct elf_32 *img, const char *name);

struct sym_info_32 elf_32_get_symbol(struct elf_32 *img, uint32_t addr);

/* INFO: Find the GOT slot file offset for a given symbol in DT_JMPREL
           (R_ARM_JUMP_SLOT entries). Returns r_offset, or 0 if not found. */
Elf32_Addr elf_32_find_plt_got_offset(struct elf_32 *img, const char *sym_name);

/* INFO: Find the GOT slot file offset of the LAST R_ARM_JUMP_SLOT entry
           in DT_JMPREL. Returns r_offset, or 0 if not found. */
Elf32_Addr elf_32_find_last_plt_got_offset(struct elf_32 *img);

#endif /* __aarch64__ */

#endif /* ELF_UTIL_32_H */
