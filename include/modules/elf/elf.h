#ifndef _RENDEZVOS_ELF_H_
#define _RENDEZVOS_ELF_H_

#include "elf_32.h"
#include "elf_64.h"

/**
 * @brief Validate ELF magic (0x7F ELF) and EI_VERSION == EV_CURRENT.
 *
 * Do not check ohters.
 *
 * @param elf_header_ptr Mapped / buffered Elf*_Ehdr address
 * @return true if magic+version OK
 */
bool check_elf_header(vaddr elf_header_ptr);
/**
 * @brief get e_ident[EI_CLASS] (ELFCLASS32 / 64 / NONE).
 */
static inline u8 get_elf_class(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        return elf_ident[EI_CLASS];
}
/**
 * @brief get e_ident[EI_DATA] (LSB / MSB).
 */
static inline u8 get_elf_data_encode(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        return elf_ident[EI_DATA];
}
/**
 * @brief get e_ident[EI_OSABI].
 */
static inline u8 get_elf_osabi(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        return elf_ident[EI_OSABI];
}
/**
 * @brief get e_ident[EI_ABIVERSION].
 */
static inline u8 get_elf_abi_version(vaddr elf_header_ptr)
{
        unsigned char *elf_ident = (unsigned char *)elf_header_ptr;
        return elf_ident[EI_ABIVERSION];
}

/**
 * @brief Read e_type (ET_EXEC / ET_DYN / …) for 32- or 64-bit header.
 * @return ET_NONE if class is neither 32 nor 64.
 */
u16 get_elf_type(vaddr elf_header_ptr);
/**
 * @brief Read e_machine (EM_X86_64 / EM_AARCH64 / …).
 * @return ET_NONE if class invalid
 */
u16 get_elf_machine(vaddr elf_header_ptr);

#define ELF32_HEADER(elf_header_ptr) ((Elf32_Ehdr *)elf_header_ptr)
#define ELF64_HEADER(elf_header_ptr) ((Elf64_Ehdr *)elf_header_ptr)
/* elf section header */
/*32 bits*/
#define ELF32_FIRST_SH(elf_header_ptr) \
        (Elf32_Shdr *)(elf_header_ptr + ELF32_HEADER(elf_header_ptr)->e_shoff)
#define ELF32_SH_NUM(elf_header_ptr) (ELF32_HEADER(elf_header_ptr)->e_shnum)
#define get_section_header_ptr_by_index_32(elf_header_ptr, index) \
        ((Elf32_Shdr *)ELF32_FIRST_SH_ADDR(elf_header_ptr) + index)

#define for_each_section_header_32(elf_header_ptr)                            \
        for (Elf32_Shdr *shdr_ptr = ELF32_FIRST_SH(elf_header_ptr),           \
                        *shdr_eptr = shdr_ptr + ELF32_SH_NUM(elf_header_ptr); \
             shdr_ptr < shdr_eptr;                                            \
             shdr_ptr++)

/*64 bits*/
#define ELF64_FIRST_SH(elf_header_ptr) \
        (Elf64_Shdr *)(elf_header_ptr + ELF64_HEADER(elf_header_ptr)->e_shoff)
#define ELF64_SH_NUM(elf_header_ptr) (ELF64_HEADER(elf_header_ptr)->e_shnum)
#define get_section_header_ptr_by_index_64(elf_header_ptr, index) \
        ((Elf64_Shdr *)ELF64_FIRST_SH_ADDR(elf_header_ptr) + index)

/**
 * @brief using shdr_ptr Iterate Elf64_Shdr from e_shoff for e_shnum entries.
 * Caller must have validated class and that the table fits the buffer.
 */
#define for_each_section_header_64(elf_header_ptr)                            \
        for (Elf64_Shdr *shdr_ptr = ELF64_FIRST_SH(elf_header_ptr),           \
                        *shdr_eptr = shdr_ptr + ELF64_SH_NUM(elf_header_ptr); \
             shdr_ptr < shdr_eptr;                                            \
             shdr_ptr++)

/* elf program header */
/* 32 bits */
#define ELF32_FIRST_PH(elf_header_ptr) \
        (Elf32_Phdr *)(elf_header_ptr + ELF32_HEADER(elf_header_ptr)->e_phoff)
#define ELF32_PH_NUM(elf_header_ptr) (ELF32_HEADER(elf_header_ptr)->e_phnum)
#define for_each_program_header_32(elf_header_ptr)                            \
        for (Elf32_Phdr *phdr_ptr = ELF32_FIRST_PH(elf_header_ptr),           \
                        *phdr_eptr = phdr_ptr + ELF32_PH_NUM(elf_header_ptr); \
             phdr_ptr < phdr_eptr;                                            \
             phdr_ptr++)
/* 64 bits */
#define ELF64_FIRST_PH(elf_header_ptr) \
        (Elf64_Phdr *)(elf_header_ptr + ELF64_HEADER(elf_header_ptr)->e_phoff)
#define ELF64_PH_NUM(elf_header_ptr) (ELF64_HEADER(elf_header_ptr)->e_phnum)
/**
 * @brief using @c phdr_ptr iterate Elf64_Phdr.
 */
#define for_each_program_header_64(elf_header_ptr)                            \
        for (Elf64_Phdr *phdr_ptr = ELF64_FIRST_PH(elf_header_ptr),           \
                        *phdr_eptr = phdr_ptr + ELF64_PH_NUM(elf_header_ptr); \
             phdr_ptr < phdr_eptr;                                            \
             phdr_ptr++)
#endif