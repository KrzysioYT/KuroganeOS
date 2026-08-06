#pragma once

#include <stdint.h>

#define EI_NIDENT 16

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

typedef struct {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct {
    Elf64_Addr r_offset;
    Elf64_Xword r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

#define PT_LOAD       1
#define PT_DYNAMIC    2

#define ET_DYN        3
#define EM_X86_64     62
#define ELFCLASS64    2
#define ELFDATA2LSB   1
#define EV_CURRENT    1

#define PF_X          1
#define PF_W          2
#define PF_R          4

#define DT_NULL       0
#define DT_NEEDED     1
#define DT_PLTRELSZ   2
#define DT_PLTGOT     3
#define DT_RELA       7
#define DT_RELASZ     8
#define DT_RELAENT    9
#define DT_INIT       12
#define DT_FINI       13
#define DT_REL        17
#define DT_RELSZ      18
#define DT_RELENT     19
#define DT_PLTREL     20
#define DT_TEXTREL    22
#define DT_JMPREL     23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RELRSZ     35
#define DT_RELR       36
#define DT_RELRENT    37
#define DT_RELACOUNT  UINT64_C(0x6ffffff9)

#define R_X86_64_RELATIVE 8

#define ELF64_R_SYM(info)  ((Elf64_Word)((info) >> 32))
#define ELF64_R_TYPE(info) ((Elf64_Word)(info))
