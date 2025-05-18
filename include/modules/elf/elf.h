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
u16 get_elf_type(vaddr elf_header_ptr);
#endif