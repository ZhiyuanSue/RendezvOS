#ifndef _RENDEZVOS_THREAD_LOADER_
#define _RENDEZVOS_THREAD_LOADER_
#include <common/string.h>
#include <modules/elf/elf.h>
#include <rendezvos/error.h>
#include <modules/elf/elf_print.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/mm/page_slice.h>

/**
 * @brief ELF load metadata passed to @c thread_append_hooks.init.
 *
 * Filled by @c run_elf_program after PT_LOAD and user-stack setup. Other init
 * paths may pass an equivalent struct or NULL (see @c thread_append_init_t ).
 */
typedef struct elf_load_info {
        struct page_slice* slice; /* ELF file image slice */
        vaddr entry_addr; /* ELF entry point */
        vaddr max_load_end; /* max(PT_LOAD.vaddr + memsz), page-aligned */
        vaddr user_sp; /* Initial user SP after stack setup */
        u16 phnum; /* ELF program-header count */
        u16 phentsize; /* ELF program-header entry size */
} elf_load_info_t;

/**
 * @brief Map ELF PT_LOAD (and stub-handle PT_DYNAMIC) into the current
 *        thread's @c vs, optionally run @c append_hooks->init, then drop to
 *        userspace.
 * @param slice Populated page_slice of the ELF file image; caller retains
 *        ownership (core does not destroy).
 * @return Does not return @c REND_SUCCESS on the success path (drop should
 *         not return). Returns @c -E_IN_PARAM / @c -E_RENDEZVOS on setup
 *         failure, or @c -E_RENDEZVOS if user drop unexpectedly returns.
 *
 * @note Optional @c append_hooks->init failure is logged but **still continues
 *       to drop**. Typical entry for a user thread created by
 *       @c gen_thread_from_elf.
 */
error_t run_elf_program(struct page_slice* slice);

/**
 * @brief Create a user thread from an ELF @p slice (no filesystem image).
 *
 * Creates a VSpace, a thread that runs @c run_elf_program, maps a user stack,
 * sets @c THREAD_FLAG_USER (assigns the whole flags word; clears other
 * bits), and enqueues on this CPU's @c core_tm only.
 *
 * @param elf_thread_ptr Optional out pointer for the new thread.
 * @param thread_append_hooks Optional lifecycle hooks (NULL ok). @c init is
 *        invoked later from @c run_elf_program with @c elf_load_info_t.
 * @param slice Populated page_slice of the ELF file image.
 * @return REND_SUCCESS on success; negative error on failure (rolls back the
 *         thread and/or an unowned vs).
 * @note @p slice is passed through to the new thread; caller / hooks decide
 *       when to release it (core does not destroy).
 */
error_t gen_thread_from_elf(Thread_Base** elf_thread_ptr,
                            const thread_append_hooks_t* thread_append_hooks,
                            struct page_slice* slice);

/**
 * @brief Map ELF64 PT_LOAD and handle PT_DYNAMIC into @p vs.
 * @param slice Populated page_slice of the ELF file image.
 * @param vs Target address space.
 * @param max_load_end_out Optional out: page-aligned max PT_LOAD end (may be
 * NULL).
 * @return @c REND_SUCCESS, or a negative @c error_t on bad image / map / copy
 * failure.
 */
error_t load_elf_to_vs(struct page_slice* slice, VSpace* vs,
                       vaddr* max_load_end_out);

/**
 * @brief Map the standard user stack at USER_SPACE_TOP downward.
 * @param vs Address space receiving the stack mapping.
 * @return Initial user stack pointer (top minus 8), or 0 on failure
 *         (@p vs NULL, range check, lock, or map fail).
 */
vaddr generate_user_stack(VSpace* vs);

/** @brief Kernel thread entry: void* (*)(void*). */
typedef void* (*kthread_func)(void*);

/**
 * @brief Create a kernel thread on @c root_vspace and add it to @p tm.
 * @param func_thread_ptr Optional out pointer for the new thread (set only on
 *        success).
 * @param thread Entry function.
 * @param thread_name Name string (copied via @c thread_set_name_with_copy).
 * @param tm Task manager hosting the thread.
 * @param arg Single integer argument passed to @p thread.
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if name or tm is NULL; @c -E_RENDEZVOS
 *         if root get or create_thread fails; otherwise the enqueue error from
 *         @c add_thread_to_manager.
 *
 * Gets @c root_vspace then passes that live ref to @c create_thread
 * (ownership transfer). On create failure the get is put back.
 */
error_t gen_thread_from_func(Thread_Base** func_thread_ptr, kthread_func thread,
                             char* thread_name, Task_Manager* tm, void* arg);
#endif
