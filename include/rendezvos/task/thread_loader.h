#ifndef _RENDEZVOS_THREAD_LOADER_
#define _RENDEZVOS_THREAD_LOADER_
#include <common/string.h>
#include <modules/elf/elf.h>
#include <rendezvos/error.h>
#include <modules/elf/elf_print.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/mm/allocator.h>
error_t load_elf_Phdr_64(vaddr elf_start, Elf64_Phdr* phdr_ptr, VS_Common* vs);
typedef void* (*append_info_handler)(Arch_Task_Context* ctx);
error_t run_elf_program(vaddr elf_start, vaddr elf_end, VS_Common* vs,
                        append_info_handler handler);
error_t gen_task_from_elf(Thread_Base** elf_thread_ptr,
                          size_t append_tcb_info_len,
                          size_t append_thread_info_len, vaddr elf_start,
                          vaddr elf_end, append_info_handler handler);
typedef void* (*kthread_func)(void*);
error_t gen_thread_from_func(Thread_Base** func_thread_ptr, kthread_func thread,
                             char* thread_name, Task_Manager* tm, void* arg);
#endif