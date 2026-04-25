#ifndef _RENDEZVOS_SYSTEM_PANIC_H_
#define _RENDEZVOS_SYSTEM_PANIC_H_

#include <common/types.h>
#include <rendezvos/trap/trap.h>

/*
 * Kernel panic - halt the system due to fatal error
 *
 * @param msg: Panic message describing the error
 *
 * This function will:
 * - Print the panic message
 * - Call architecture-specific shutdown sequence
 * - Halt the system
 *
 * Note: This function never returns
 */
void kernel_panic(const char* msg) __attribute__((noreturn));

/*
 * Kernel halt - stop the system
 *
 * This function will halt the system using architecture-specific method.
 * Should be used when the system cannot continue (e.g., unrecoverable error).
 *
 * Note: This function never returns
 */
void kernel_halt(void) __attribute__((noreturn));

#endif
