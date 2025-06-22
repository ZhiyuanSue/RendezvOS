#ifndef _RENDEZVOS_ELF32_H_
#define _RENDEZVOS_ELF32_H_

#include "elf_common.h"
// === === header
typedef struct {
        unsigned char e_ident[EI_NIDENT];
        Elf32_Half e_type;
        Elf32_Half e_machine;
        Elf32_Word e_version;
        Elf32_Addr e_entry;
        Elf32_Off e_phoff;
        Elf32_Off e_shoff;
        Elf32_Word e_flags;
        Elf32_Half e_ehsize;
        Elf32_Half e_phentsize;
        Elf32_Half e_phnum;
        Elf32_Half e_shentsize;
        Elf32_Half e_shnum;
        Elf32_Half e_shstrndx;
} Elf32_Ehdr;

// === === section
typedef struct {
        Elf32_Word sh_name; /* Section name */
        Elf32_Word sh_type; /* Section type */
        Elf32_Word sh_flags; /* Section attributes */
        Elf32_Addr sh_addr; /* Virtual address in memory */
        Elf32_Off sh_offset; /* Offset in file */
        Elf32_Word sh_size; /* Size of section */
        Elf32_Word sh_link; /* Link to other section */
        Elf32_Word sh_info; /* Miscellaneous information */
        Elf32_Word sh_addralign; /* Address alignment boundary */
        Elf32_Word sh_entsize; /* Size of entries, if section has table */
} Elf32_Shdr;

// === === symbol table
typedef struct {
        Elf32_Word st_name; /* Symbol name */
        Elf32_Addr st_value; /* Symbol value */
        Elf32_Word st_size; /* Size of object (e.g., common) */
        unsigned char st_info; /* Type and Binding attributes */
        unsigned char st_other; /* Reserved */
        Elf32_Half st_shndx; /* Section table index */
} Elf32_Sym;

#define ELF32_ST_BIND(i)    ((i) >> 4)
#define ELF32_ST_TYPE(i)    ((i) & 0xf)
#define ELF32_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))

// === === relocation entries
typedef struct {
        Elf32_Addr r_offset; /* Address of reference */
        Elf32_Word r_info; /* Symbol index and type of relocation */
} Elf32_Rel;
typedef struct {
        Elf32_Addr r_offset; /* Address of reference */
        Elf32_Word r_info; /* Symbol index and type of relocation */
        Elf32_Sword r_addend; /* Constant part of expression */
} Elf32_Rela;

#define ELF32_R_SYM(i)     ((i) >> 8)
#define ELF32_R_TYPE(i)    ((unsigned char)(i))
#define ELF32_R_INFO(s, t) (((s) << 8) + ((unsigned char)(t)))

// === === program header table
typedef struct {
        Elf32_Word p_type; /* Type of segment */
        Elf32_Off p_offset; /* Offset in file */
        Elf32_Addr p_vaddr; /* Virtual address in memory */
        Elf32_Addr p_paddr; /* Reserved */
        Elf32_Word p_filesz; /* Size of segment in file */
        Elf32_Word p_memsz; /* Size of segment in memory */
        Elf32_Word p_flags; /* Segment attributes */
        Elf32_Word p_align; /* Alignment of segment */
} Elf32_Phdr;

#endif