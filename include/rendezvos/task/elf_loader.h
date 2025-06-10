#ifndef _RENDEZVOS_ELF_LOADER_
#define _RENDEZVOS_ELF_LOADER_
#include <modules/elf/elf.h>
#include <rendezvos/error.h>
#include <modules/elf/elf_print.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/allocator.h>
extern struct nexus_node* nexus_root;
error_t load_elf_Phdr_64(Elf64_Phdr* phdr_ptr);
error_t load_elf_program(vaddr elf_start, vaddr elf_end);
error_t gen_task_from_elf(vaddr elf_start, vaddr elf_end);

#endif