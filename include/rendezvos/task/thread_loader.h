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
 * @brief ELF load metadata (personality may use when building user image).
 *
 * Passed to @c thread_append_hooks.init from @c run_elf_program (Path B /
 * incbin harness). Personality exec paths may fill an equivalent struct
 * themselves.
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
 * @brief Map ELF PT_LOAD/PT_DYNAMIC into the current thread’s vs, run
 *        @c thread_append_hooks.init when set, then drop to userspace.
 * @param slice Populated page_slice of the ELF file image; caller retains
 *        ownership (core does not destroy).
 * @return REND_SUCCESS if control returns; -E_IN_PARAM or -E_RENDEZVOS on
 *         failure. Does not modify slice lifetime.
 *
 * Intended as the body of a user thread created by @c gen_thread_from_elf
 * (incbin / bare-core harness). Linux PID1 / execve use personality load
 * instead.
 */
error_t run_elf_program(struct page_slice* slice);

/**
 * @brief Create a user thread from an ELF @p slice (no FS / no personality).
 *
 * create/register @c VSpace → @c create_thread(@c run_elf_program) (takes
 * ownership of vs) → @c generate_user_stack → @c THREAD_FLAG_USER →
 * @c add_thread_to_manager.
 *
 * @param elf_thread_ptr Optional out pointer for the new thread.
 * @param thread_append_hooks Optional lifecycle hooks (NULL ok). @c init is
 *        invoked later from @c run_elf_program with @c elf_load_info_t.
 * @param slice Populated page_slice of the ELF file image.
 * @return REND_SUCCESS on success; negative error on failure (rolls back).
 * @p slice is passed through to the new thread; caller / hooks decide when
 * to release it (core does not destroy).
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
 * @brief Create a kernel thread on @c root_vspace and add it to @p tm.
 * @param func_thread_ptr Optional out pointer for the new thread.
 * @param thread Entry function.
 * @param thread_name Name string (not copied).
 * @param tm Task manager hosting the thread.
 * @param arg Single integer argument passed to @p thread.
 * @return REND_SUCCESS; -E_IN_PARAM if name or tm is NULL; -E_RENDEZVOS if
 *         root get or create_thread fails.
 *
 * Gets @c root_vspace then passes that live ref to @c create_thread
 * (ownership transfer). On create failure the get is put back.
 */
error_t gen_thread_from_func(Thread_Base** func_thread_ptr, kthread_func thread,
                             char* thread_name, Task_Manager* tm, void* arg);
#endif
