#include <rendezvos/trap/trap.h>

__attribute__((weak)) void syscall(struct trap_frame* syscall_ctx)
{
        (void)syscall_ctx;
}
