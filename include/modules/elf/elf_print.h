#ifndef _RENDEZVOS_ELF_PRINT_H_
#define _RENDEZVOS_ELF_PRINT_H_
#include <modules/log/log.h>
#define ELF_STR_LEN 32

#define ELF_E_IDENT_CLASS_NUM 3
extern char elf_e_ident_class_str[ELF_E_IDENT_CLASS_NUM][ELF_STR_LEN];

#define ELF_E_IDENT_DATA_NUM 3
extern char elf_e_ident_data_str[ELF_E_IDENT_DATA_NUM][ELF_STR_LEN];

#define ELF_PT_TYPE_NUM 8
extern char elf_pt_type_str[ELF_PT_TYPE_NUM][ELF_STR_LEN];
void print_elf_machine(u16 elf_machine_type);
void print_elf_header(vaddr elf_header_ptr);
void print_elf_ph32(Elf32_Phdr* phdr);
void print_elf_ph64(Elf64_Phdr* phdr);
void print_elf_sh32(Elf32_Shdr* shdr);
void print_elf_sh64(Elf64_Shdr* shdr);
#endif