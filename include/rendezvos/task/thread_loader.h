#ifndef _RENDEZVOS_THREAD_LOADER_
#define _RENDEZVOS_THREAD_LOADER_
#include <common/string.h>
#include <modules/elf/elf.h>
#include <rendezvos/error.h>
#include <modules/elf/elf_print.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/mm/page_slice.h>

/**
 * @brief ELF load result passed to elf_init_handler_t (core-defined,
 * ABI-neutral).
 */
typedef struct elf_load_info {
        struct page_slice* slice;
        vaddr entry_addr;
        vaddr max_load_end; /* max(PT_LOAD.vaddr + memsz), page-aligned */
        vaddr user_sp; /* initial user SP after stack setup */
        u16 phnum;
        u16 phentsize;
} elf_load_info_t;

/**
 * @brief Optional hook after PT_LOAD and user stack setup, before userspace
 * entry.
 * @param ctx Child thread arch context.
 * @param info Load addresses and program-header metadata.
 * @return Opaque value (unused by core loader).
 */
typedef void* (*elf_init_handler_t)(Arch_Task_Context* ctx,
                                    const elf_load_info_t* info);

/**
 * @brief Map ELF PT_LOAD/PT_DYNAMIC into @p vs on the current thread, run
 * @p elf_init, then drop to userspace at the image entry.
 * @param slice Populated page_slice of the ELF file image; caller retains
 *        ownership (core does not destroy).
 * @param vs Address space to map into.
 * @param elf_init Optional pre-entry hook (may be NULL).
 * @return REND_SUCCESS if control returns; -E_IN_PARAM or -E_RENDEZVOS on
 *         failure. Does not modify slice lifetime.
 */
error_t run_elf_program(struct page_slice* slice, VSpace* vs,
                        elf_init_handler_t elf_init);

/**
 * @brief Create task and user thread, map ELF, set user stack, thread_join.
 * @param elf_thread_ptr Optional out pointer for the new thread.
 * @param append_tcb_info_len Extra TCB tail bytes.
 * @param append_thread_info_len Extra thread tail bytes.
 * @param task_append_fini Optional task append pre-free hook (NULL ok).
 * @param task_append_copy Optional task append post-copy hook (NULL ok).
 * @param thread_append_fini Optional thread append pre-free hook (NULL ok).
 * @param thread_append_copy Optional thread append post-copy_thread hook (NULL
 * ok).
 * @param slice Populated page_slice of the ELF file image.
 * @param elf_init Optional pre-entry hook (may be NULL).
 * @return REND_SUCCESS on success; negative error on failure (rolls back task).
 * @p slice is passed through to the new thread; caller / @p elf_init decide
 * when to release it (core does not destroy).
 */
error_t gen_task_from_elf(Thread_Base** elf_thread_ptr,
                          size_t append_tcb_info_len,
                          size_t append_thread_info_len,
                          task_append_fini_t task_append_fini,
                          task_append_copy_t task_append_copy,
                          thread_append_fini_t thread_append_fini,
                          thread_append_copy_t thread_append_copy,
                          struct page_slice* slice,
                          elf_init_handler_t elf_init);

/**
 * @brief Map ELF64 PT_LOAD and handle PT_DYNAMIC into @p vs.
 * @param slice Populated page_slice of the ELF file image.
 * @param vs Target address space.
 * @param max_load_end_out Optional out: page-aligned max PT_LOAD end (may be
 * NULL).
 * @return REND_SUCCESS; -E_IN_PARAM or -E_RENDEZVOS on bad image or map
 * failure.
 */
error_t load_elf_to_vs(struct page_slice* slice, VSpace* vs,
                       vaddr* max_load_end_out);

/**
 * @brief Map the standard user stack at USER_SPACE_TOP.
 * @param vs Address space receiving the stack mapping.
 * @return Initial user stack pointer (top minus 8), or 0 on failure.
 */
vaddr generate_user_stack(VSpace* vs);

/** @brief Kernel thread entry: void* (*)(void*). */
typedef void* (*kthread_func)(void*);

/**
 * @brief Create a kernel thread and thread_join it to @p tm root task.
 * @param func_thread_ptr Optional out pointer for the new thread.
 * @param thread Entry function.
 * @param thread_name Name string (not copied).
 * @param tm Task manager hosting the thread.
 * @param arg Single integer argument passed to @p thread.
 * @return REND_SUCCESS; -E_IN_PARAM if name or tm is NULL; -E_RENDEZVOS if
 *         create_thread fails.
 */
error_t gen_thread_from_func(Thread_Base** func_thread_ptr, kthread_func thread,
                             char* thread_name, Task_Manager* tm, void* arg);
#endif
