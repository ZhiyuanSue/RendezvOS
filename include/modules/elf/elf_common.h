#ifndef _RENDEZVOS_ELF_TYPES_H_
#define _RENDEZVOS_ELF_TYPES_H_

#include <common/types.h>

typedef u32 Elf32_Addr;
typedef u16 Elf32_Half;
typedef u32 Elf32_Off;
typedef i32 Elf32_Sword;
typedef u32 Elf32_Word;

typedef u64 Elf64_Addr;
typedef u64 Elf64_Off;
typedef u16 Elf64_Half;
typedef u32 Elf64_Word;
typedef i32 Elf64_Sword;
typedef u64 Elf64_Xword;
typedef i64 Elf64_Sxword;

#define EI_NIDENT 16

// valuse of e_version
#define EV_NONE    0 // Invalid version
#define EV_CURRENT 1 // Current version

// value of magic number
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

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
#define ELFCLASSNONE 0 // Invalid class
#define ELFCLASS32   1 // 32-bit objects
#define ELFCLASS64   2 // 64-bit objects

// value of e_ident[EI_DATA]
#define ELFDATANONE 0 // Invalid data encoding
#define ELFDATA2LSB 1 // Object ﬁle data structures are little-endian
#define ELFDATA2MSB 2 // Object ﬁle data structures are big-endian

// value of e_machine
#define ET_NONE        0 /* No machine */
#define EM_M32         1 /* AT&T WE 32100 */
#define EM_SPARC       2 /* SPARC */
#define EM_386         3 /* intel Architecture */
#define EM_68K         4 /* Motorola 68000 */
#define EM_88K         5 /* Motorola 88000 */
#define EM_860         7 /* Intel 80860 */
#define EM_MIPS        8 /* MIPS Architecture */
#define EM_MIPS_RS3_LE 10 /* MIPS R3000 little-endian */
#define EM_MIPS_RS4_BE 10 /* MIPS R4000 big-endian */
#define EM_PARISC      15 /* HPPA */
#define EM_SPARC32PLUS 18 /* Sun's "v8plus" */
#define EM_PPC         20 /* PowerPC */
#define EM_PPC64       21 /* PowerPC64 */
#define EM_S390        22 /* IBM System 390 Processor */
#define EM_SPU         23 /* Cell BE SPU */
#define EM_V800        36 /* NEC V800 */
#define EM_FR20        37 /* Fujitsu FR20 */
#define EM_RH32        38 /* TRW RH-32 */
#define EM_RCE         39 /* Motorola RCE */
#define EM_ARM         40 /* ARM 32 bit */
#define EM_ALPHA       41 /* Digital Alpha */
#define EM_SH          42 /* SuperH */
#define EM_SPARCV9     43 /* SPARC v9 64-bit */
#define EM_TRICORE     44 /* TriCore embedded processor */
#define EM_ARC         45 /* Argonaut Risc Core */
#define EM_H8_300      46 /* Renesas H8/300 */
#define EM_IA_64       50 /* HP/Intel IA-64 */
#define EM_MIPS_X      51 /* Stanford MIPS-X */
#define EM_X86_64      62 /* AMD x86-64 */
#define EM_AARCH64     183 /* ARM 64 bit */
#define EM_RISCV       243 /* RISC-V */
#define EM_LOONGARCH   258 /* LoongArch */

// special section index
#define SHN_UNDEF     0 // Used to mark an undeﬁned or meaningless section reference
#define SHN_LORESERVE 0xFF00 // Processor-speciﬁc use
#define SHN_LOPROC    0xFF00 // Processor-speciﬁc use
#define SHN_HIPROC    0xFF1F
#define SHN_LOOS      0xFF20 // Environment-speciﬁc use
#define SHN_HIOS      0xFF3F
#define SHN_ABS \
        0xFFF1 // Indicates that the corresponding reference is an absolute
               // value
#define SHN_COMMON \
        0xFFF2 // Indicates a symbol that has been declared as a common block
               // (Fortran COMMON or C tentative declaration)
#define HIRESERVE 0xFFFF

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
#define SHT_LOUSER 0x80000000
#define SHT_HIUSER 0xFFFFFFFF

// value of sh_flags
#define SHF_WRITE     0x1
#define SHF_ALLOC     0x2
#define SHF_EXECINSTR 0x4
#define SHF_MASKOS    0x0F000000
#define SHF_MASKPROC  0xF0000000

// value of symbol binding
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STB_LOOS   10
#define STB_HIOS   12
#define STB_LOPROC 13
#define STB_HIPORC 15

// value of symbol type
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STT_SECTION 3
#define STT_FILE    4
#define STT_LOPROC  13
#define STT_HIPROC  15

// value of segment types
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_LOOS    0x60000000
#define PT_HIOS    0x6FFFFFFF
#define LOPROC     0x70000000
#define HIPROC     0x7FFFFFFF

// value of p flags
#define PF_X        0x1
#define PF_W        0x2
#define PF_R        0x4
#define PF_MASKOS   0x00FF0000
#define PF_MASKPROC 0xFF000000

#endif