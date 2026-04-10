#ifndef _RENDEZVOS_THREAD_LOADER_
#define _RENDEZVOS_THREAD_LOADER_
#include <common/string.h>
#include <modules/elf/elf.h>
#include <rendezvos/error.h>
#include <modules/elf/elf_print.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/mm/allocator.h>
error_t load_elf_Phdr_64(vaddr elf_start, Elf64_Phdr* phdr_ptr, VS_Common* vs);
/*
 * ELF load result information (core-defined, Linux-agnostic).
 *
 * This struct captures *generic* facts about the loaded user image that are
 * useful to higher layers (linux compat, other ABIs) without embedding Linux
 * policy in core.
 */
typedef struct elf_load_info {
        vaddr elf_start;
        vaddr elf_end;
        vaddr entry_addr;
        vaddr max_load_end; /* max(PT_LOAD.vaddr + memsz), page-aligned */
        vaddr user_sp; /* initial user SP after stack setup */
        u16 phnum;
        u16 phentsize;
} elf_load_info_t;

/*
 * ELF thread init handler:
 * called after PT_LOAD/PT_DYNAMIC are handled and user stack is prepared,
 * before dropping to userspace.
 */
typedef void* (*elf_init_handler_t)(Arch_Task_Context* ctx,
                                    const elf_load_info_t* info);
error_t run_elf_program(vaddr elf_start, vaddr elf_end, VS_Common* vs,
                        elf_init_handler_t elf_init);
error_t gen_task_from_elf(Thread_Base** elf_thread_ptr,
                          size_t append_tcb_info_len,
                          size_t append_thread_info_len, vaddr elf_start,
                          vaddr elf_end, elf_init_handler_t elf_init);
typedef void* (*kthread_func)(void*);
error_t gen_thread_from_func(Thread_Base** func_thread_ptr, kthread_func thread,
                             char* thread_name, Task_Manager* tm, void* arg);
#endif