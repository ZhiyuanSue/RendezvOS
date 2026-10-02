#ifndef _RENDEZVOS_INIT_H_
#define _RENDEZVOS_INIT_H_
#include <common/types.h>

/**
 * @brief One initcall table entry is just a void(void) function pointer.
 *
 * Linker script gathers `.init.call.0` … `.init.call.9` then `.init.call`
 * between `_s_init` and `_e_init` (see `script/link/<isa>_linker.ld`).
 */
typedef struct {
        void (*init_func_ptr)(void);
} Init_Info;
extern Init_Info _s_init, _e_init;

#define __INIT_SECTION(level) ".init.call." #level

/**
 * @brief Place @p func_ptr into linker section `.init.call.<level>`.
 *
 * @param func_ptr Function of type `void (*)(void)` (identifier, not string).
 * @param level    Integer 0–9; smaller runs first. Same level: link order only.
 *
 * @note No parameters, no return code to the caller of do_init_call().
 *       Failures must log/panic inside the function.
 * @note Every CPU that calls do_init_call() invokes every entry once.
 *       Gate BSP-only / per-CPU work with @c percpu(cpu_number) inside the
 *       function.
 */
#define DEFINE_INIT_LEVEL(func_ptr, level)                              \
        __attribute__((used, section(__INIT_SECTION(level)))) Init_Info \
                __init_##func_ptr = {.init_func_ptr = (func_ptr)}

/**
 * @brief Register @p func_ptr at default init level 3.
 * @note if using this macro, the order might not certain with other func that
 * using DEFINE_INIT
 */
#define DEFINE_INIT(func_ptr) DEFINE_INIT_LEVEL(func_ptr, 3)

/**
 * @brief Call every initcall between `_s_init` and `_e_init` in link order.
 *
 * Runs synchronously on the calling CPU/thread. If you creating a thread from an
 * initcall does not run that thread until a later @c schedule.
 */
static inline void do_init_call(void)
{
        for (Init_Info *i_ptr = &_s_init; i_ptr < &_e_init; i_ptr++) {
                i_ptr->init_func_ptr();
        }
}
#endif