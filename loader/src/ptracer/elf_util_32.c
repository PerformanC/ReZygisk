#include "elf_util_32.h"

#ifdef __aarch64__

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logging.h"

#ifndef ELF32_ST_TYPE
  #define ELF32_ST_TYPE(i) ((i) & 0xf)
#endif

#ifndef ELF32_ST_BIND
  #define ELF32_ST_BIND(i) ((i) >> 4)
#endif

static uint32_t elf_hash(const char *name) {
  uint32_t h = 0, g = 0;

  while (*name) {
    h = (h << 4) + (unsigned char)*name++;
    g = h & 0xf0000000;

    if (g) {
      h ^= g >> 24;
    }

    h &= ~g;
  }

  return h;
}

static uint32_t gnu_hash(const char *name) {
  uint32_t h = 5381;

  while (*name) {
    h = (h << 5) + h + (unsigned char)(*name++);
  }

  return h;
}

static Elf32_Shdr *offsetOf_Shdr(Elf32_Ehdr *head, Elf32_Off off) {
  return (Elf32_Shdr *)(((uintptr_t)head) + off);
}

static char *offsetOf_char(Elf32_Ehdr *head, Elf32_Off off) {
  return (char *)(((uintptr_t)head) + off);
}

static Elf32_Sym *offsetOf_Sym(Elf32_Ehdr *head, Elf32_Off off) {
  return (Elf32_Sym *)(((uintptr_t)head) + off);
}

static Elf32_Word *offsetOf_Word(Elf32_Ehdr *head, Elf32_Off off) {
  return (Elf32_Word *)(((uintptr_t)head) + off);
}

static size_t calculate_valid_symtabs_amount(struct elf_32 *img) {
  size_t count = 0;

  if (img->symtab_start == NULL || img->symstr_offset_for_symtab == 0) {
    LOGE("Invalid symtab_start or symstr_offset_for_symtab, cannot count valid symbols");

    return 0;
  }

  char *symtab_strings = offsetOf_char(img->header, img->symstr_offset_for_symtab);

  for (Elf32_Off i = 0; i < img->symtab_count; i++) {
    const char *sym_name = symtab_strings + img->symtab_start[i].st_name;
    if (!sym_name)
      continue;

    unsigned int st_type = ELF32_ST_TYPE(img->symtab_start[i].st_info);

    if ((st_type == STT_FUNC || st_type == STT_OBJECT) && img->symtab_start[i].st_size > 0 && img->symtab_start[i].st_name != 0)
      count++;
  }

  return count;
}

void elf_32_destroy(struct elf_32 *img) {
  if (!img) return;

  if (img->symtabs_) {
    size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
    if (valid_symtabs_amount > 0) {
      for (size_t i = 0; i < valid_symtabs_amount; i++) {
        free(img->symtabs_[i].name);
      }
    }

    free(img->symtabs_);
    img->symtabs_ = NULL;
  }

  if (img->elf) {
    free(img->elf);
    img->elf = NULL;
  }

  if (img->header) {
    free(img->header);
    img->header = NULL;
  }

  free(img);
}

struct elf_32 *elf_32_create(const char *elf) {
  struct elf_32 *img = (struct elf_32 *)calloc(1, sizeof(struct elf_32));
  if (!img) {
    LOGE("Failed to allocate memory for struct elf_32");

    return NULL;
  }

  img->elf = strdup(elf);
  if (!img->elf) {
    LOGE("Failed to duplicate elf path string");

    free(img);

    return NULL;
  }

  /* INFO: Unlike the native elf_util, we don't use dl_iterate_phdr
             to find the base. The base is set externally via
             img->base after creation, or we just parse the file
             for symbol offsets without needing the remote base. */

  int fd = open(img->elf, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    LOGE("failed to open %s", img->elf);

    elf_32_destroy(img);

    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    LOGE("fstat() failed for %s", img->elf);

    close(fd);
    elf_32_destroy(img);

    return NULL;
  }

  img->size = st.st_size;

  if (img->size <= sizeof(Elf32_Ehdr)) {
    LOGE("Invalid file size %zu for %s", img->size, img->elf);

    close(fd);
    elf_32_destroy(img);

    return NULL;
  }

  img->header = (Elf32_Ehdr *)malloc(img->size);
  if (!img->header) {
    LOGE("Failed to allocate %zu bytes for %s", img->size, img->elf);

    close(fd);
    elf_32_destroy(img);

    return NULL;
  }

  size_t total_read = 0;
  while (total_read < (size_t)img->size) {
    ssize_t n = TEMP_FAILURE_RETRY(read(fd, (char *)img->header + total_read, img->size - total_read));
    if (n < 0) {
      LOGE("read() failed for %s: %s", img->elf, strerror(errno));

      close(fd);
      elf_32_destroy(img);

      return NULL;
    }

    if (n == 0) {
      LOGE("Unexpected EOF while reading %s", img->elf);

      close(fd);
      elf_32_destroy(img);

      return NULL;
    }

    total_read += (size_t)n;
  }

  close(fd);

  if (memcmp(img->header->e_ident, ELFMAG, SELFMAG) != 0) {
    LOGE("Invalid ELF header for %s", img->elf);

    elf_32_destroy(img);

    return NULL;
  }

  if (img->header->e_ident[EI_CLASS] != ELFCLASS32) {
    LOGE("Not a 32-bit ELF: %s", img->elf);

    elf_32_destroy(img);

    return NULL;
  }

  if (img->header->e_shoff == 0 || img->header->e_shentsize == 0 || img->header->e_shnum == 0) {
    LOGW("Section header table missing or invalid in %s", img->elf);
  } else {
    img->section_header = offsetOf_Shdr(img->header, img->header->e_shoff);
  }

  if (img->header->e_phoff == 0 || img->header->e_phentsize == 0 || img->header->e_phnum == 0) {
    LOGW("Program header table missing or invalid in %s", img->elf);
  }

  Elf32_Shdr *dynsym_shdr = NULL;
  Elf32_Shdr *symtab_shdr = NULL;

  char *section_str = NULL;
  if (img->section_header && img->header->e_shstrndx != SHN_UNDEF) {
    if (img->header->e_shstrndx < img->header->e_shnum) {
      Elf32_Shdr *shstrtab_hdr = img->section_header + img->header->e_shstrndx;
      section_str = offsetOf_char(img->header, shstrtab_hdr->sh_offset);
    } else {
      LOGW("Section header string table index (%u) out of bounds (%u)", img->header->e_shstrndx, img->header->e_shnum);
    }
  } else {
    LOGW("Section header string table index not set or no section headers");
  }

  if (img->section_header) {
    uintptr_t shoff = (uintptr_t)img->section_header;
    for (int i = 0; i < img->header->e_shnum; i++, shoff += img->header->e_shentsize) {
      Elf32_Shdr *section_h = (Elf32_Shdr *)shoff;
      char *sname = section_str ? (section_h->sh_name + section_str) : "<?>";

      switch (section_h->sh_type) {
        case SHT_DYNSYM: {
          dynsym_shdr = section_h;
          img->dynsym_offset = section_h->sh_offset;
          img->dynsym_start = offsetOf_Sym(img->header, img->dynsym_offset);

          break;
        }
        case SHT_SYMTAB: {
          if (strcmp(sname, ".symtab") == 0) {
            symtab_shdr = section_h;
            img->symtab_offset = section_h->sh_offset;
            img->symtab_size = section_h->sh_size;

            size_t entsize = section_h->sh_entsize;
            if (entsize > 0) img->symtab_count = img->symtab_size / entsize;
            else {
              LOGW("Section %s has zero sh_entsize", sname);
              img->symtab_count = 0;
            }

            img->symtab_start = offsetOf_Sym(img->header, img->symtab_offset);
          }

          break;
        }
        case SHT_STRTAB: break;
        case SHT_PROGBITS: break;
        case SHT_HASH: {
          Elf32_Word *d_un = offsetOf_Word(img->header, section_h->sh_offset);

          if (section_h->sh_size >= 2 * sizeof(Elf32_Word)) {
            img->nbucket_ = d_un[0];

            if (img->nbucket_ > 0 && section_h->sh_size >= (2 + img->nbucket_ + d_un[1]) * sizeof(Elf32_Word)) {
              img->bucket_ = d_un + 2;
              img->chain_ = img->bucket_ + img->nbucket_;
            } else {
              LOGW("Invalid SHT_HASH size or nbucket count in section %s", sname);
              img->nbucket_ = 0;
            }
          } else {
            LOGW("SHT_HASH section %s too small", sname);
          }

          break;
        }
        case SHT_GNU_HASH_32: {
          Elf32_Word *d_buf = offsetOf_Word(img->header, section_h->sh_offset);

          if (section_h->sh_size >= 4 * sizeof(Elf32_Word)) {
            img->gnu_nbucket_ = d_buf[0];
            img->gnu_symndx_ = d_buf[1];
            img->gnu_bloom_size_ = d_buf[2];
            img->gnu_shift2_ = d_buf[3];

            /* INFO: ELF32 bloom filter uses 32-bit words (not uintptr_t). */
            size_t expected_min_size = 4 * sizeof(Elf32_Word) +
                                      img->gnu_bloom_size_ * sizeof(uint32_t) +
                                      img->gnu_nbucket_ * sizeof(uint32_t);

            if (img->gnu_nbucket_ > 0 && img->gnu_bloom_size_ > 0 && section_h->sh_size >= expected_min_size) {
              img->gnu_bloom_filter_ = (uint32_t *)(d_buf + 4);
              img->gnu_bucket_ = (uint32_t *)(img->gnu_bloom_filter_ + img->gnu_bloom_size_);
              img->gnu_chain_ = img->gnu_bucket_ + img->gnu_nbucket_;

              uintptr_t chain_start_offset = (uintptr_t)img->gnu_chain_ - (uintptr_t)img->header;
              if (chain_start_offset < section_h->sh_offset || chain_start_offset >= section_h->sh_offset + section_h->sh_size) {
                LOGW("Calculated GNU hash chain seems out of bounds for section %s", sname);

                img->gnu_nbucket_ = 0;
              }
            } else {
              LOGW("Invalid SHT_GNU_HASH size or parameters in section %s", sname);

              img->gnu_nbucket_ = 0;
            }
          } else {
            LOGW("SHT_GNU_HASH section %s too small", sname);
          }

          break;
        }
      }
    }
  }

  Elf32_Shdr *shdr_base = img->section_header;

  if (dynsym_shdr && shdr_base) {
    img->dynsym = dynsym_shdr;

    if (dynsym_shdr->sh_link < img->header->e_shnum) {
      Elf32_Shdr *linked_strtab = shdr_base + dynsym_shdr->sh_link;

      if (linked_strtab->sh_type == SHT_STRTAB) {
        img->strtab = linked_strtab;
        img->symstr_offset = linked_strtab->sh_offset;
        img->strtab_start = (void *)offsetOf_char(img->header, img->symstr_offset);
      } else {
        LOGW("Section %u linked by .dynsym is not SHT_STRTAB (type %u)", dynsym_shdr->sh_link, linked_strtab->sh_type);
      }
    } else {
      LOGE(".dynsym sh_link (%u) is out of bounds (%u)", dynsym_shdr->sh_link, img->header->e_shnum);
    }
  } else {
    LOGW("No .dynsym section found or section headers missing");
  }

  if (symtab_shdr && shdr_base) {
    img->symtab = symtab_shdr;

    if (symtab_shdr->sh_link < img->header->e_shnum) {
      Elf32_Shdr *linked_strtab = shdr_base + symtab_shdr->sh_link;

      if (linked_strtab->sh_type == SHT_STRTAB) {
        img->symstr_offset_for_symtab = linked_strtab->sh_offset;
      } else {
        LOGW("Section %u linked by .symtab is not SHT_STRTAB (type %u)", symtab_shdr->sh_link, linked_strtab->sh_type);

        img->symstr_offset_for_symtab = 0;
      }
    } else {
      LOGE(".symtab sh_link (%u) is out of bounds (%u)", symtab_shdr->sh_link, img->header->e_shnum);

      img->symstr_offset_for_symtab = 0;
    }
  } else {
    img->symtab_start = NULL;
    img->symtab_count = 0;
    img->symstr_offset_for_symtab = 0;
  }

  /* INFO: Compute bias from PT_LOAD segment. */
  bool bias_calculated = false;
  if (img->header->e_phoff > 0 && img->header->e_phnum > 0) {
    Elf32_Phdr *phdr = (Elf32_Phdr *)((uintptr_t)img->header + img->header->e_phoff);

    for (int i = 0; i < img->header->e_phnum; ++i) {
      if (phdr[i].p_type == PT_LOAD && phdr[i].p_offset == 0) {
        img->bias = (Elf32_Sword)(phdr[i].p_vaddr - phdr[i].p_offset);
        bias_calculated = true;

        break;
      }
    }

    if (!bias_calculated) for (int i = 0; i < img->header->e_phnum; ++i) {
      if (phdr[i].p_type != PT_LOAD) continue;

      img->bias = (Elf32_Sword)(phdr[i].p_vaddr - phdr[i].p_offset);
      bias_calculated = true;

      break;
    }
  }

  if (!bias_calculated)
    LOGE("Failed to calculate bias for %s. Assuming bias is 0.", img->elf);

  if (!img->dynsym_start || !img->strtab_start) {
    if (img->header->e_type == ET_DYN) {
      LOGE("Failed to find .dynsym or its string table (.dynstr) in %s", img->elf);
    } else {
      LOGW("No .dynsym or .dynstr found in %s (might be expected for ET_EXEC)", img->elf);
    }
  }

  if (!img->gnu_bucket_ && !img->bucket_)
    LOGW("No hash table (.gnu.hash or .hash) found in %s. Dynamic symbol lookup might be slow or fail.", img->elf);

  return img;
}

static bool load_symtabs(struct elf_32 *img) {
  if (img->symtabs_) return true;

  if (!img->symtab_start || img->symstr_offset_for_symtab == 0 || img->symtab_count == 0) {
    return false;
  }

  size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
  if (valid_symtabs_amount == 0) {
    LOGW("No valid symbols (FUNC/OBJECT with size > 0) found in .symtab for %s", img->elf);

    return false;
  }

  img->symtabs_ = (struct symtabs_32 *)calloc(valid_symtabs_amount, sizeof(struct symtabs_32));
  if (!img->symtabs_) {
    LOGE("Failed to allocate memory for symtabs array");

    return false;
  }

  char *symtab_strings = offsetOf_char(img->header, img->symstr_offset_for_symtab);
  size_t current_valid_index = 0;

  for (Elf32_Off pos = 0; pos < img->symtab_count; pos++) {
    Elf32_Sym *current_sym = &img->symtab_start[pos];
    unsigned int st_type = ELF32_ST_TYPE(current_sym->st_info);

    if ((st_type == STT_FUNC || st_type == STT_OBJECT) && current_sym->st_size > 0 && current_sym->st_name != 0) {
      const char *st_name = symtab_strings + current_sym->st_name;
      if (!st_name)
        continue;

      Elf32_Shdr *symtab_str_shdr = img->section_header + img->symtab->sh_link;
      if (current_sym->st_name >= symtab_str_shdr->sh_size) {
        LOGE("Symbol name offset out of bounds");

        continue;
      }

      img->symtabs_[current_valid_index].name = strdup(st_name);
      if (!img->symtabs_[current_valid_index].name) {
        LOGE("Failed to duplicate symbol name: %s", st_name);

        for (size_t k = 0; k < current_valid_index; ++k) {
          free(img->symtabs_[k].name);
        }

        free(img->symtabs_);
        img->symtabs_ = NULL;

        return false;
      }

      img->symtabs_[current_valid_index].sym = current_sym;

      current_valid_index++;
      if (current_valid_index == valid_symtabs_amount) break;
    }
  }

  return true;
}

static Elf32_Addr gnu_symbol_lookup(struct elf_32 *restrict img, const char *name, uint32_t hash, unsigned char *sym_type) {
  if (img->gnu_nbucket_ == 0 || img->gnu_bloom_size_ == 0 || !img->gnu_bloom_filter_ || !img->gnu_bucket_ || !img->gnu_chain_ || !img->dynsym_start || !img->strtab_start)
    return 0;

  /* INFO: ELF32 bloom filter uses 32-bit words (not uintptr_t). */
  static const size_t bloom_mask_bits = 32;

  size_t bloom_idx = (hash / bloom_mask_bits) % img->gnu_bloom_size_;
  uint32_t bloom_word = img->gnu_bloom_filter_[bloom_idx];
  uint32_t mask = ((uint32_t)1 << (hash % bloom_mask_bits)) |
                  ((uint32_t)1 << ((hash >> img->gnu_shift2_) % bloom_mask_bits));

  if ((mask & bloom_word) != mask) {
    return 0;
  }

  uint32_t sym_index = img->gnu_bucket_[hash % img->gnu_nbucket_];
  if (sym_index < img->gnu_symndx_) {
    LOGW("Symbol %s hash %u maps to bucket %u index %u (below gnu_symndx %u), not exported?", name, hash, hash % img->gnu_nbucket_, sym_index, img->gnu_symndx_);

    return 0;
  }

  char *strings = (char *)img->strtab_start;
  uint32_t chain_val = img->gnu_chain_[sym_index - img->gnu_symndx_];

  Elf32_Word dynsym_count = img->dynsym->sh_size / img->dynsym->sh_entsize;
  if (sym_index >= dynsym_count) {
    LOGE("Symbol index %u out of bounds", sym_index);

    return 0;
  }

  Elf32_Sym *sym = img->dynsym_start + sym_index;

  if (sym->st_name >= img->strtab->sh_size) {
    LOGE("Symbol name offset %u out of bounds", sym->st_name);

    return 0;
  }

  if ((((chain_val ^ hash) >> 1) == 0 && strcmp(name, strings + sym->st_name) == 0) && sym->st_shndx != SHN_UNDEF) {
    unsigned int type = ELF32_ST_TYPE(sym->st_info);
    if (sym_type) *sym_type = type;

    return sym->st_value;
  }

  while ((chain_val & 1) == 0) {
    sym_index++;

    if (sym_index >= dynsym_count) {
      LOGE("Symbol index %u out of bounds during chain walk", sym_index);

      return 0;
    }

    chain_val = img->gnu_chain_[sym_index - img->gnu_symndx_];
    sym = img->dynsym_start + sym_index;

    if (sym->st_name >= img->strtab->sh_size) {
      LOGE("Symbol name offset %u out of bounds", sym->st_name);

      break;
    }

    if ((((chain_val ^ hash) >> 1) == 0 && strcmp(name, strings + sym->st_name) == 0) && sym->st_shndx != SHN_UNDEF) {
      unsigned int type = ELF32_ST_TYPE(sym->st_info);
      if (sym_type) *sym_type = type;

      return sym->st_value;
    }
  }

  return 0;
}

static Elf32_Addr elf_symbol_lookup(struct elf_32 *restrict img, const char *restrict name, uint32_t hash, unsigned char *sym_type) {
  if (img->nbucket_ == 0 || !img->bucket_ || !img->chain_ || !img->dynsym_start || !img->strtab_start)
    return 0;

  char *strings = (char *)img->strtab_start;

  for (size_t n = img->bucket_[hash % img->nbucket_]; n != STN_UNDEF; n = img->chain_[n]) {
    Elf32_Sym *sym = img->dynsym_start + n;

    if (strcmp(name, strings + sym->st_name) == 0 && sym->st_shndx != SHN_UNDEF) {
      unsigned int type = ELF32_ST_TYPE(sym->st_info);
      if (sym_type) *sym_type = type;

      return sym->st_value;
    }
  }

  return 0;
}

static Elf32_Addr linear_symbol_lookup(struct elf_32 *img, const char *restrict name, unsigned char *sym_type) {
  if (!load_symtabs(img)) {
    return 0;
  }

  size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
  if (valid_symtabs_amount == 0) {
    LOGW("No valid symbols (FUNC/OBJECT with size > 0) found in .symtab for %s", img->elf);

    return 0;
  }

  for (size_t i = 0; i < valid_symtabs_amount; i++) {
    if (!img->symtabs_[i].name || strcmp(name, img->symtabs_[i].name) != 0)
      continue;

    if (img->symtabs_[i].sym->st_shndx == SHN_UNDEF)
      continue;

    unsigned int type = ELF32_ST_TYPE(img->symtabs_[i].sym->st_info);
    if (sym_type) *sym_type = type;

    return img->symtabs_[i].sym->st_value;
  }

  return 0;
}

Elf32_Addr elf_32_symb_offset(struct elf_32 *img, const char *name, unsigned char *sym_type) {
  Elf32_Addr offset = 0;

  offset = gnu_symbol_lookup(img, name, gnu_hash(name), sym_type);
  if (offset != 0) return offset;

  offset = elf_symbol_lookup(img, name, elf_hash(name), sym_type);
  if (offset != 0) return offset;

  offset = linear_symbol_lookup(img, name, sym_type);
  if (offset != 0) return offset;

  return 0;
}

Elf32_Addr elf_32_symb_address(struct elf_32 *img, const char *name) {
  unsigned char sym_type = 0;
  Elf32_Addr offset = elf_32_symb_offset(img, name, &sym_type);

  if (offset == 0 || !img->base) return 0;

  /* INFO: Cannot resolve STT_GNU_IFUNC from the host (aarch64) —
             the resolver is ARM32 code. Just return base + offset.
             The caller must handle IFUNC symbols if needed. */
  if (sym_type == STT_GNU_IFUNC) {
    LOGW("elf32: IFUNC symbol %s — returning resolver offset (cannot execute ARM32 resolver from host)", name);
  }

  return (Elf32_Addr)(img->base + offset - img->bias);
}

struct sym_info_32 elf_32_get_symbol(struct elf_32 *img, uint32_t addr) {
  if (!load_symtabs(img)) {
    return (struct sym_info_32) {
      .name = NULL,
      .address = 0
    };
  }

  size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
  if (valid_symtabs_amount == 0) {
    return (struct sym_info_32) {
      .name = NULL,
      .address = 0
    };
  }

  for (size_t i = 0; i < valid_symtabs_amount; i++) {
    Elf32_Sym *sym = img->symtabs_[i].sym;
    if (!sym || sym->st_value == 0 || sym->st_size == 0) continue;

    Elf32_Addr sym_start = (Elf32_Addr)(img->base + sym->st_value - img->bias);
    Elf32_Addr sym_end = sym_start + sym->st_size;

    if (addr >= sym_start && addr < sym_end) {
      return (struct sym_info_32) {
        .name = img->symtabs_[i].name,
        .address = sym_start
      };
    }
  }

  return (struct sym_info_32) {
    .name = NULL,
    .address = 0
  };
}

#ifndef R_ARM_JUMP_SLOT
  #define R_ARM_JUMP_SLOT  22
#endif


/* INFO: Convert a 32-bit virtual address to a pointer into the
           file image loaded at img->header. Returns NULL if no
           PT_LOAD segment covers the address. */
static void *elf32_vaddr_to_ptr(struct elf_32 *img, Elf32_Addr vaddr) {
  Elf32_Phdr *phdr = (Elf32_Phdr *)((uintptr_t)img->header + img->header->e_phoff);

  for (int i = 0; i < img->header->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD) continue;

    if (vaddr >= phdr[i].p_vaddr && vaddr < phdr[i].p_vaddr + phdr[i].p_filesz) {
      Elf32_Off off = phdr[i].p_offset + (vaddr - phdr[i].p_vaddr);

      if (off < img->size)
        return (void *)((uintptr_t)img->header + off);
    }
  }

  return NULL;
}

/* INFO: Internal: locate PLT relocation table and associated
           symbol / string tables from PT_DYNAMIC. Returns false
           if any required table is missing. */
static bool elf32_get_plt_tables(struct elf_32 *img,
                                 Elf32_Rel **rels_out, uint32_t *n_rels_out,
                                 Elf32_Sym **syms_out, const char **strs_out,
                                 uint32_t *strsz_out) {
  if (!img->header || img->header->e_phoff == 0 || img->header->e_phnum == 0)
    return false;

  Elf32_Phdr *phdr = (Elf32_Phdr *)((uintptr_t)img->header + img->header->e_phoff);

  Elf32_Dyn *dyn_start = NULL;
  size_t dyn_count = 0;

  for (int i = 0; i < img->header->e_phnum; i++) {
    if (phdr[i].p_type == PT_DYNAMIC) {
      if (phdr[i].p_offset + phdr[i].p_filesz <= img->size) {
        dyn_start = (Elf32_Dyn *)((uintptr_t)img->header + phdr[i].p_offset);
        dyn_count = phdr[i].p_filesz / sizeof(Elf32_Dyn);
      }

      break;
    }
  }

  if (!dyn_start) return false;

  Elf32_Addr jmprel_va = 0, symtab_va = 0, strtab_va = 0;
  uint32_t pltrelsz = 0, strsz = 0;

  for (size_t i = 0; i < dyn_count; i++) {
    if (dyn_start[i].d_tag == DT_NULL) break;

    switch (dyn_start[i].d_tag) {
      case DT_JMPREL:   jmprel_va = dyn_start[i].d_un.d_ptr; break;
      case DT_PLTRELSZ: pltrelsz  = dyn_start[i].d_un.d_val; break;
      case DT_SYMTAB:   symtab_va = dyn_start[i].d_un.d_ptr; break;
      case DT_STRTAB:   strtab_va = dyn_start[i].d_un.d_ptr; break;
      case DT_STRSZ:    strsz     = dyn_start[i].d_un.d_val; break;
    }
  }

  if (!jmprel_va || !pltrelsz || !symtab_va || !strtab_va) return false;

  Elf32_Rel *rels = (Elf32_Rel *)elf32_vaddr_to_ptr(img, jmprel_va);
  Elf32_Sym *syms = (Elf32_Sym *)elf32_vaddr_to_ptr(img, symtab_va);
  const char *strs = (const char *)elf32_vaddr_to_ptr(img, strtab_va);

  if (!rels || !syms || !strs) return false;

  *rels_out   = rels;
  *n_rels_out = pltrelsz / sizeof(Elf32_Rel);
  *syms_out   = syms;
  *strs_out   = strs;
  *strsz_out  = strsz;

  return true;
}

Elf32_Addr elf_32_find_plt_got_offset(struct elf_32 *img, const char *sym_name) {
  Elf32_Rel *rels;
  uint32_t n_rels;
  uint32_t strsz;
  Elf32_Sym *syms;
  const char *strs;

  if (!elf32_get_plt_tables(img, &rels, &n_rels, &syms, &strs, &strsz))
    return 0;

  for (uint32_t i = 0; i < n_rels; i++) {
    if (ELF32_R_TYPE(rels[i].r_info) != R_ARM_JUMP_SLOT) continue;

    uint32_t sym_idx  = ELF32_R_SYM(rels[i].r_info);
    uint32_t name_off = syms[sym_idx].st_name;

    if (strsz && name_off >= strsz) continue;

    if (strcmp(strs + name_off, sym_name) == 0)
      return rels[i].r_offset;
  }

  return 0;
}

Elf32_Addr elf_32_find_last_plt_got_offset(struct elf_32 *img) {
  Elf32_Rel *rels;
  uint32_t n_rels;
  uint32_t strsz;
  Elf32_Sym *syms;
  const char *strs;

  if (!elf32_get_plt_tables(img, &rels, &n_rels, &syms, &strs, &strsz))
    return 0;

  Elf32_Addr last = 0;

  for (uint32_t i = 0; i < n_rels; i++) {
    if (ELF32_R_TYPE(rels[i].r_info) == R_ARM_JUMP_SLOT)
      last = rels[i].r_offset;
  }

  return last;
}

#endif /* __aarch64__ */
