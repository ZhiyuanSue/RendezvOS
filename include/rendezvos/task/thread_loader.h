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
 * @brief ELF load metadata passed to @c thread_append_hooks.init.
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
 * @brief Map ELF PT_LOAD/PT_DYNAMIC into @p vs on the current thread, run
 * @p thread append @c init hook when set, then drop to userspace.
 * @param slice Populated page_slice of the ELF file image; caller retains
 *        ownership (core does not destroy).
 * @param vs Address space to map into.
 * @return REND_SUCCESS if control returns; -E_IN_PARAM or -E_RENDEZVOS on
 *         failure. Does not modify slice lifetime.
 */
error_t run_elf_program(struct page_slice* slice, VSpace* vs);

/**
 * @brief Create task and user thread, map ELF, set user stack, thread_join.
 * @param elf_thread_ptr Optional out pointer for the new thread.
 * @param task_append_hooks Optional task append lifecycle hooks (NULL ok).
 * @param thread_append_hooks Optional thread append lifecycle hooks (NULL ok).
 * @param slice Populated page_slice of the ELF file image.
 * @return REND_SUCCESS on success; negative error on failure (rolls back task).
 * @p slice is passed through to the new thread; upper @c init hook decides
 * when to release it (core does not destroy).
 */
error_t gen_task_from_elf(Thread_Base** elf_thread_ptr,
                          const task_append_hooks_t* task_append_hooks,
                          const thread_append_hooks_t* thread_append_hooks,
                          struct page_slice* slice);

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
