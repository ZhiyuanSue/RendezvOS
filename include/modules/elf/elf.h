#ifndef _RENDEZVOS_ELF_H_
#define _RENDEZVOS_ELF_H_

#include "elf_types.h"

// === === header
typedef struct {
        unsigned char e_ident[16]; /* ELF identification */
        Elf64_Half e_type; /* Object file type */
        Elf64_Half e_machine; /* Machine type */
        Elf64_Word e_version; /* Object file version */
        Elf64_Addr e_entry; /* Entry point address */
        Elf64_Off e_phoff; /* Program header offset */
        Elf64_Off e_shoff; /* Section header offset */
        Elf64_Word e_flags; /* Processor-specific flags */
        Elf64_Half e_ehsize; /* ELF header size */
        Elf64_Half e_phentsize; /* Size of program header entry */
        Elf64_Half e_phnum; /* Number of program header entries */
        Elf64_Half e_shentsize; /* Size of section header entry */
        Elf64_Half e_shnum; /* Number of section header entries */
        Elf64_Half e_shstrndx; /* Section name string table index */
} Elf64_Ehdr;

#define EV_CURRENT 1

// byte name of e_ident
#define EI_MAG0       0 // File identiﬁcation
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4 // File class
#define EI_DATA       5 // Data encoding
#define EI_VERSION    6 // File version
#define EI_OSABI      7 // OS/ABI identiﬁcation
#define EI_ABIVERSION 8 // ABI version
#define EI_PAD        9 // Start of padding bytes
#define EI_NIDENT     16 // Size of e_ident[]

// value of e_ident[EI_CLASS]
#define ELFCLASS32 1 // 32-bit objects
#define ELFCLASS64 2 // 64-bit objects

// value of e_ident[EI_DATA]
#define ELFDATA2LSB 1 // Object ﬁle data structures are little-endian
#define ELFDATA2MSB 2 // Object ﬁle data structures are big-endian

// value of e_ident[EI_OSABI]
#define ELFOSABI_SYSV       0 // System V ABI
#define ELFOSABI_HPUX       1 // HP-UX operating system
#define ELFOSABI_STANDALONE 255 // Standalone (embedded) application

// value of e_type
#define ET_NONE   0 // No ﬁle type
#define ET_REL    1 // Relocatable object ﬁle
#define ET_EXEC   2 // Executable ﬁle
#define ET_DYN    3 // Shared object ﬁle
#define ET_CORE   4 // Core ﬁle
#define ET_LOOS   0xFE00 // Environment-speciﬁc use
#define ET_HIOS   0xFEFF
#define ET_LOPROC 0xFF00 // Processor-speciﬁc use
#define ET_HIPROC 0xFFFF

// === === section
typedef struct {
        Elf64_Word sh_name; /* Section name */
        Elf64_Word sh_type; /* Section type */
        Elf64_Xword sh_flags; /* Section attributes */
        Elf64_Addr sh_addr; /* Virtual address in memory */
        Elf64_Off sh_offset; /* Offset in file */
        Elf64_Xword sh_size; /* Size of section */
        Elf64_Word sh_link; /* Link to other section */
        Elf64_Word sh_info; /* Miscellaneous information */
        Elf64_Xword sh_addralign; /* Address alignment boundary */
        Elf64_Xword sh_entsize; /* Size of entries, if section has table */
} Elf64_Shdr;

// special section index
#define SHN_UNDEF  0 // Used to mark an undeﬁned or meaningless section reference
#define SHN_LOPROC 0xFF00 // Processor-speciﬁc use
#define SHN_HIPROC 0xFF1F
#define SHN_LOOS   0xFF20 // Environment-speciﬁc use
#define SHN_HIOS   0xFF3F
#define SHN_ABS \
        0xFFF1 // Indicates that the corresponding reference is an absolute
               // value
#define SHN_COMMON \
        0xFFF2 // Indicates a symbol that has been declared as a common block
               // (Fortran COMMON or C tentative declaration)

// value of sh_type
#define SHT_NULL     0 // Marks an unused section header
#define SHT_PROGBITS 1 // Contains information deﬁned by the program
#define SHT_SYMTAB   2 // Contains a linker symbol table
#define SHT_STRTAB   3 // Contains a string table
#define SHT_RELA     4 // Contains “Rela” type relocation entries
#define SHT_HASH     5 // Contains a symbol hash table
#define SHT_DYNAMIC  6 // Contains dynamic linking tables
#define SHT_NOTE     7 // Contains note information
#define SHT_NOBITS \
        8 // Contains uninitialized space; does not occupy any space in the ﬁle
#define SHT_REL    9 // Contains “Rel” type relocation entries
#define SHT_SHLIB  10 // Reserved
#define SHT_DYNSYM 11 // Contains a dynamic loader symbol table
#define SHT_LOOS   0x60000000 // Environment-speciﬁc use
#define SHT_HIOS   0x6FFFFFFF
#define SHT_LOPROC 0x70000000 // Processor-speciﬁc use
#define SHT_HIPROC 0x7FFFFFFF

// === === symbol table
typedef struct {
        Elf64_Word st_name; /* Symbol name */
        unsigned char st_info; /* Type and Binding attributes */
        unsigned char st_other; /* Reserved */
        Elf64_Half st_shndx; /* Section table index */
        Elf64_Addr st_value; /* Symbol value */
        Elf64_Xword st_size; /* Size of object (e.g., common) */
} Elf64_Sym;

// === === relocation entries
typedef struct {
        Elf64_Addr r_offset; /* Address of reference */
        Elf64_Xword r_info; /* Symbol index and type of relocation */
} Elf64_Rel;
typedef struct {
        Elf64_Addr r_offset; /* Address of reference */
        Elf64_Xword r_info; /* Symbol index and type of relocation */
        Elf64_Sxword r_addend; /* Constant part of expression */
} Elf64_Rela;
#define ELF64_R_SYM(i)     ((i) >> 32)
#define ELF64_R_TYPE(i)    ((i)&0xffffffffL)
#define ELF64_R_INFO(s, t) (((s) << 32) + ((t)&0xffffffffL))

// === === program header table
typedef struct {
        Elf64_Word p_type; /* Type of segment */
        Elf64_Word p_flags; /* Segment attributes */
        Elf64_Off p_offset; /* Offset in file */
        Elf64_Addr p_vaddr; /* Virtual address in memory */
        Elf64_Addr p_paddr; /* Reserved */
        Elf64_Xword p_filesz; /* Size of segment in file */
        Elf64_Xword p_memsz; /* Size of segment in memory */
        Elf64_Xword p_align; /* Alignment of segment */
} Elf64_Phdr;

// === === dynamic table
typedef struct {
        Elf64_Sxword d_tag;
        union {
                Elf64_Xword d_val;
                Elf64_Addr d_ptr;
        } d_un;
} Elf64_Dyn;
extern Elf64_Dyn _DYNAMIC[];

#endif