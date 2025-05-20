#ifndef _RENDEZVOS_ELF_PRINT_H_
#define _RENDEZVOS_ELF_PRINT_H_
#include <modules/log/log.h>
void print_elf_header(vaddr elf_header_ptr);
void print_elf_ph32(Elf32_Phdr* phdr);
void print_elf_ph64(Elf64_Phdr* phdr);
#endif