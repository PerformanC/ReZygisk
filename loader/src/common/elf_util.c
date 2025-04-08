/* INFO: This file is written in C99 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <unistd.h>

#include "logging.h"

#include "elf_util.h"

#define SHT_GNU_HASH 0x6ffffff6

uint32_t ElfHash(const char *name) {
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

uint32_t GnuHash(const char *name) {
  uint32_t h = 5381;

  while (*name) {
    h = (h << 5) + h + (unsigned char)(*name++);
  }

  return h;
}

ElfW(Shdr) *offsetOf_Shdr(ElfW(Ehdr) * head, ElfW(Off) off) {
  return (ElfW(Shdr) *)(((uintptr_t)head) + off);
}

char *offsetOf_char(ElfW(Ehdr) * head, ElfW(Off) off) {
  return (char *)(((uintptr_t)head) + off);
}

ElfW(Sym) *offsetOf_Sym(ElfW(Ehdr) * head, ElfW(Off) off) {
  return (ElfW(Sym) *)(((uintptr_t)head) + off);
}

ElfW(Word) *offsetOf_Word(ElfW(Ehdr) * head, ElfW(Off) off) {
  return (ElfW(Word) *)(((uintptr_t)head) + off);
}

int dl_cb(struct dl_phdr_info *info, size_t size, void *data) {
  (void) size;

  if ((info)->dlpi_name == NULL) return 0;

  ElfImg *img = (ElfImg *)data;

  if (strstr(info->dlpi_name, img->elf)) {  
    img->elf = strdup(info->dlpi_name);
    img->base = (void *)info->dlpi_addr;

    return 1;
  }

  return 0;
}

bool find_module_base(ElfImg *img) {
  dl_iterate_phdr(dl_cb, img);

  return img->base != NULL;
}

size_t calculate_valid_symtabs_amount(ElfImg *img) {
  size_t count = 0;

  if (img->symtab_start == NULL || img->symstr_offset_for_symtab == 0) return count;

  for (ElfW(Off) i = 0; i < img->symtab_count; i++) {
    unsigned int st_type = ELF_ST_TYPE(img->symtab_start[i].st_info);

    if ((st_type == STT_FUNC || st_type == STT_OBJECT) && img->symtab_start[i].st_size)
      count++;
  }

  return count;
}

void ElfImg_destroy(ElfImg *img) {
  if (img->elf) {
    free(img->elf);
    img->elf = NULL;
  }

  if (img->symtabs_) {
    size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
    if (valid_symtabs_amount == 0) goto finalize;

    for (size_t i = 0; i < valid_symtabs_amount; i++) {
      free(img->symtabs_[i].name);
    }

    free(img->symtabs_);
    img->symtabs_ = NULL;
  }

  if (img->header) {
    munmap(img->header, img->size);
    img->header = NULL;
  }

  finalize:
    free(img);
    img = NULL;
}

ElfImg *ElfImg_create(const char *elf) {
  ElfImg *img = (ElfImg *)calloc(1, sizeof(ElfImg));
  if (!img) {
    LOGE("Failed to allocate memory for ElfImg");

    return NULL;
  }

  img->bias = -4396;
  img->elf = strdup(elf);
  img->base = NULL;

  if (!find_module_base(img)) {
    LOGE("Failed to find module base for %s", img->elf);

    ElfImg_destroy(img);

    return NULL;
  }

  int fd = open(img->elf, O_RDONLY);
  if (fd < 0) {
    LOGE("failed to open %s", img->elf);

    ElfImg_destroy(img);

    return NULL;
  }

  img->size = lseek(fd, 0, SEEK_END);
  if (img->size <= 0) {
    LOGE("lseek() failed for %s", img->elf);

    ElfImg_destroy(img);

    return NULL;
  }

  img->header = (ElfW(Ehdr) *)mmap(NULL, img->size, PROT_READ, MAP_SHARED, fd, 0);

  close(fd);

  img->section_header = offsetOf_Shdr(img->header, img->header->e_shoff);

  uintptr_t shoff = (uintptr_t)img->section_header;
  char *section_str = offsetOf_char(img->header, img->section_header[img->header->e_shstrndx].sh_offset);

  for (int i = 0; i < img->header->e_shnum; i++, shoff += img->header->e_shentsize) {
    ElfW(Shdr) *section_h = (ElfW(Shdr *))shoff;

    char *sname = section_h->sh_name + section_str;
    size_t entsize = section_h->sh_entsize;

    switch (section_h->sh_type) {
      case SHT_DYNSYM: {
        if (img->bias == -4396) {
          img->dynsym = section_h;
          img->dynsym_offset = section_h->sh_offset;
          img->dynsym_start = offsetOf_Sym(img->header, img->dynsym_offset);
        }

        break;
      }
      case SHT_SYMTAB: {
        if (strcmp(sname, ".symtab") == 0) {
          img->symtab = section_h;
          img->symtab_offset = section_h->sh_offset;
          img->symtab_size = section_h->sh_size;
          img->symtab_count = img->symtab_size / entsize;
          img->symtab_start = offsetOf_Sym(img->header, img->symtab_offset);
        }

        break;
      }
      case SHT_STRTAB: {
        if (img->bias == -4396) {
          img->strtab = section_h;
          img->symstr_offset = section_h->sh_offset;
          img->strtab_start = offsetOf_Sym(img->header, img->symstr_offset);
        }

        if (strcmp(sname, ".strtab") == 0) {
          img->symstr_offset_for_symtab = section_h->sh_offset;
        }

        break;
      }
      case SHT_PROGBITS: {
        if (img->strtab == NULL || img->dynsym == NULL)
          break;

        if (img->bias == -4396) {
          img->bias = (off_t)section_h->sh_addr - (off_t)section_h->sh_offset;
        }

        break;
      }
      case SHT_HASH: {
        ElfW(Word) *d_un = offsetOf_Word(img->header, section_h->sh_offset);
        img->nbucket_ = d_un[0];
        img->bucket_ = d_un + 2;
        img->chain_ = img->bucket_ + img->nbucket_;

        break;
      }
      case SHT_GNU_HASH: {
        ElfW(Word) *d_buf = (ElfW(Word) *)(((size_t)img->header) + section_h->sh_offset);
        img->gnu_nbucket_ = d_buf[0];
        img->gnu_symndx_ = d_buf[1];
        img->gnu_bloom_size_ = d_buf[2];
        img->gnu_shift2_ = d_buf[3];
        img->gnu_bloom_filter_ = (uintptr_t *)(d_buf + 4);
        img->gnu_bucket_ = (uint32_t *)(img->gnu_bloom_filter_ + img->gnu_bloom_size_);
        img->gnu_chain_ = img->gnu_bucket_ + img->gnu_nbucket_ - img->gnu_symndx_;

        break;
      }
    }
  }

  return img;
}

ElfW(Addr) ElfLookup(ElfImg *restrict img, const char *restrict name, uint32_t hash) {
  if (img->nbucket_ == 0)
    return 0;

  char *strings = (char *)img->strtab_start;

  for (size_t n = img->bucket_[hash % img->nbucket_]; n != 0; n = img->chain_[n]) {
    ElfW(Sym) *sym = img->dynsym_start + n;

    if (strncmp(name, strings + sym->st_name, strlen(name)) == 0)
      return sym->st_value;
  }
  return 0;
}

ElfW(Addr) GnuLookup(ElfImg *restrict img, const char *name, uint32_t hash) {
  static size_t bloom_mask_bits = sizeof(ElfW(Addr)) * 8;

  if (img->gnu_nbucket_ == 0 || img->gnu_bloom_size_ == 0)
    return 0;

  size_t bloom_word =
      img->gnu_bloom_filter_[(hash / bloom_mask_bits) % img->gnu_bloom_size_];
  uintptr_t mask = 0 | (uintptr_t)1 << (hash % bloom_mask_bits) |
                   (uintptr_t)1 << ((hash >> img->gnu_shift2_) % bloom_mask_bits);
  if ((mask & bloom_word) == mask) {
    size_t sym_index = img->gnu_bucket_[hash % img->gnu_nbucket_];
    if (sym_index >= img->gnu_symndx_) {
      char *strings = (char *)img->strtab_start;
      do {
        ElfW(Sym) *sym = img->dynsym_start + sym_index;

        if (((img->gnu_chain_[sym_index] ^ hash) >> 1) == 0 &&
            name == strings + sym->st_name) {
          return sym->st_value;
        }
      } while ((img->gnu_chain_[sym_index++] & 1) == 0);
    }
  }

  return 0;
}

ElfW(Addr) LinearLookup(ElfImg *img, const char *restrict name) {
  size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
  if (valid_symtabs_amount == 0) return 0;

  if (!img->symtabs_) {
    img->symtabs_ = (struct symtabs *)calloc(1, sizeof(struct symtabs) * valid_symtabs_amount);
    if (!img->symtabs_) return 0;


    if (img->symtab_start != NULL && img->symstr_offset_for_symtab != 0) {
      ElfW(Off) i = 0;
      for (ElfW(Off) pos = 0; pos < img->symtab_count; pos++) {
        unsigned int st_type = ELF_ST_TYPE(img->symtab_start[pos].st_info);
        const char *st_name = offsetOf_char(img->header, img->symstr_offset_for_symtab + img->symtab_start[pos].st_name);

        if ((st_type == STT_FUNC || st_type == STT_OBJECT) && img->symtab_start[pos].st_size) {
          img->symtabs_[i].name = strdup(st_name);
          img->symtabs_[i].sym = &img->symtab_start[pos];

          i++;
        }
      }
    }
  }

  for (size_t i = 0; i < valid_symtabs_amount; i++) {
    if (strcmp(name, img->symtabs_[i].name) != 0) continue;

    return img->symtabs_[i].sym->st_value;
  }

  return 0;
}

ElfW(Addr) LinearLookupByPrefix(ElfImg *img, const char *name) {
  size_t valid_symtabs_amount = calculate_valid_symtabs_amount(img);
  if (valid_symtabs_amount == 0) return 0;

  if (!img->symtabs_) {
    img->symtabs_ = (struct symtabs *)malloc(sizeof(struct symtabs) * valid_symtabs_amount);
    if (!img->symtabs_) return 0;

    if (img->symtab_start != NULL && img->symstr_offset_for_symtab != 0) {
      ElfW(Off) i = 0;
      for (ElfW(Off) pos = 0; pos < img->symtab_count; pos++) {
        unsigned int st_type = ELF_ST_TYPE(img->symtab_start[pos].st_info);
        const char *st_name = offsetOf_char(img->header, img->symstr_offset_for_symtab + img->symtab_start[pos].st_name);

        if ((st_type == STT_FUNC || st_type == STT_OBJECT) && img->symtab_start[pos].st_size) {
          img->symtabs_[i].name = strdup(st_name);
          img->symtabs_[i].sym = &img->symtab_start[pos];

          i++;
        }
      }
    }
  }

  for (size_t i = 0; i < valid_symtabs_amount; i++) {
    if (strlen(img->symtabs_[i].name) < strlen(name))
      continue;

    if (strncmp(img->symtabs_[i].name, name, strlen(name)) == 0)
      return img->symtabs_[i].sym->st_value;
  }

  return 0;
}

ElfW(Addr) getSymbOffset(ElfImg *img, const char *name) {
  ElfW(Addr) offset = GnuLookup(img, name, GnuHash(name));
  if (offset > 0) return offset;

  offset = ElfLookup(img, name, ElfHash(name));
  if (offset > 0) return offset;

  offset = LinearLookup(img, name);
  if (offset > 0) return offset;

  return 0;
}

ElfW(Addr) getSymbAddress(ElfImg *img, const char *name) {
  ElfW(Addr) offset = getSymbOffset(img, name);

  if (offset < 0 || !img->base) return 0;

  return ((uintptr_t)img->base + offset - img->bias);
}

ElfW(Addr) getSymbAddressByPrefix(ElfImg *img, const char *prefix) {
  ElfW(Addr) offset = LinearLookupByPrefix(img, prefix);

  if (offset < 0 || !img->base) return 0;

  return (ElfW(Addr))((uintptr_t)img->base + offset - img->bias);
}

void *getSymbValueByPrefix(ElfImg *img, const char *prefix) {
  ElfW(Addr) address = getSymbAddressByPrefix(img, prefix);

  return address == 0 ? NULL : *((void **)address);
}
