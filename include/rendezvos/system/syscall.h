#ifndef _RENDEZVOS_SYSTEM_SYSCALL_H_
#define _RENDEZVOS_SYSTEM_SYSCALL_H_

#include <rendezvos/trap/trap.h>

/**
 * @brief Syscall hook 
 * 
 * Weak default is a no-op function.
 *
 * Compat / upper layers must provide a strong definition that reads
 * @c ARCH_SYSCALL_ID / @c ARCH_SYSCALL_ARG_* and writes @c ARCH_SYSCALL_RET
 * on the frame. Return value must be stored in that frame slot, not
 * from the C ABI return register after @c syscall returns.
 *
 * @param syscall_ctx Arch trap / syscall frame pointer. 
 * May be unused by the weak stub.
 *
 * @note No NR_syscalls table in RendezvOS core. arch_start_core call arch 
 * @c init_syscall at the CPU core start
 */
void syscall(struct trap_frame *syscall_ctx);

#endif
