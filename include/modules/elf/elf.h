#ifndef _RENDEZVOS_ELF_H_
#define _RENDEZVOS_ELF_H_

#include "elf_32.h"
#include "elf_64.h"
// elf ident
bool check_elf_header(vaddr elf_header_ptr);
u8 get_elf_class(vaddr elf_header_ptr);
u8 get_elf_data_encode(vaddr elf_header_ptr);
u8 get_elf_osabi(vaddr elf_header_ptr);
u8 get_elf_abi_version(vaddr elf_header_ptr);

u16 get_elf_type(vaddr elf_header_ptr);
u16 get_elf_machine(vaddr elf_header_ptr);

/* elf program header */
/* 32 bits */
#define ELF32_HEADER(elf_header_ptr) ((Elf32_Ehdr*)elf_header_ptr)
#define ELF32_FIRST_PH_ADDR(elf_header_ptr) \
        (elf_header_ptr + ELF32_HEADER(elf_header_ptr)->e_phoff)
#define ELF32_END_PH_ADDR(elf_header_ptr)              \
        (ELF32_FIRST_PH_ADDR(elf_header_ptr)           \
         + (ELF32_HEADER(elf_header_ptr)->e_phentsize) \
                   * (ELF32_HEADER(elf_header_ptr)->e_phnum))
#define ELF32_NEXT_PH_ADDR(curr_ph_ptr, elf_header_ptr) \
        (curr_ph_ptr + ELF32_HEADER(elf_header_ptr)->e_phentsize)

#define for_each_program_header_32(elf_header_ptr)                       \
        for (Elf32_Phdr* phdr_ptr =                                      \
                     (Elf32_Phdr*)ELF32_FIRST_PH_ADDR(elf_header_ptr);   \
             (vaddr)phdr_ptr < (vaddr)ELF32_END_PH_ADDR(elf_header_ptr); \
             phdr_ptr = (Elf32_Phdr*)ELF32_NEXT_PH_ADDR((vaddr)phdr_ptr, \
                                                        elf_header_ptr))
/* 64 bits */
#define ELF64_HEADER(elf_header_ptr) ((Elf64_Ehdr*)elf_header_ptr)
#define ELF64_FIRST_PH_ADDR(elf_header_ptr) \
        (elf_header_ptr + ELF64_HEADER(elf_header_ptr)->e_phoff)
#define ELF64_END_PH_ADDR(elf_header_ptr)              \
        (ELF64_FIRST_PH_ADDR(elf_header_ptr)           \
         + (ELF64_HEADER(elf_header_ptr)->e_phentsize) \
                   * (ELF64_HEADER(elf_header_ptr)->e_phnum))
#define ELF64_NEXT_PH_ADDR(curr_ph_ptr, elf_header_ptr) \
        (curr_ph_ptr + ELF64_HEADER(elf_header_ptr)->e_phentsize)

#define for_each_program_header_64(elf_header_ptr)                       \
        for (Elf64_Phdr* phdr_ptr =                                      \
                     (Elf64_Phdr*)ELF64_FIRST_PH_ADDR(elf_header_ptr);   \
             (vaddr)phdr_ptr < (vaddr)ELF64_END_PH_ADDR(elf_header_ptr); \
             phdr_ptr = (Elf64_Phdr*)ELF64_NEXT_PH_ADDR((vaddr)phdr_ptr, \
                                                        elf_header_ptr))
#endif