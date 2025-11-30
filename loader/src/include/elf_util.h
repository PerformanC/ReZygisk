#ifndef ELF_UTIL_H
#define ELF_UTIL_H

#include <stdbool.h>
#include <link.h>
#include <sys/types.h>
#include <pthread.h> // Added for threading primitives

#define SHT_GNU_HASH 0x6ffffff6

// Function pointer types for constructors and destructors
typedef void (*linker_simple_func_t)(void);
typedef void (*linker_ctor_function_t)(int, char**, char**);
typedef void (*linker_dtor_function_t)(void);


struct symtabs {
  char *name;
  ElfW(Sym) *sym;
};

typedef struct {
  char *elf;
  void *base;
  ElfW(Ehdr) *header;
  size_t size;
  off_t bias;
  ElfW(Shdr) *section_header;

  ElfW(Shdr) *dynsym;
  ElfW(Off) dynsym_offset;
  ElfW(Sym) *dynsym_start;
  ElfW(Shdr) *strtab;
  ElfW(Off) symstr_offset;
  void *strtab_start;

  uint32_t nbucket_;
  uint32_t *bucket_;
  uint32_t *chain_;

  uint32_t gnu_nbucket_;
  uint32_t gnu_symndx_;
  uint32_t gnu_bloom_size_;
  uint32_t gnu_shift2_;
  uintptr_t *gnu_bloom_filter_;
  uint32_t *gnu_bucket_;
  uint32_t *gnu_chain_;

  ElfW(Shdr) *symtab;
  ElfW(Off) symtab_offset;
  size_t symtab_size;
  size_t symtab_count;
  ElfW(Sym) *symtab_start;
  ElfW(Off) symstr_offset_for_symtab;

  struct symtabs *symtabs_;
} ElfImg;

void ElfImg_destroy(ElfImg *img);

ElfImg *ElfImg_create(const char *elf, void *base);

ElfW(Addr) getSymbOffset(ElfImg *img, const char *name, unsigned int *sym_type);

ElfW(Addr) getSymbAddress(ElfImg *img, const char *name);

ElfW(Addr) getSymbAddressByPrefix(ElfImg *img, const char *prefix);

void *getSymbValueByPrefix(ElfImg *img, const char *prefix);

#endif /* ELF_UTIL_H */
