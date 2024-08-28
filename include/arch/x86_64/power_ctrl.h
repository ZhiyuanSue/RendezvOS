#ifndef _SHAMPOOS_X86_64_POWER_CTRL_H_
#define _SHAMPOOS_X86_64_POWER_CTRL_H_

#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>

static inline void arch_shutdown(void)
{
        /*have no idea,maybe it works*/
        outw(_X86_POWER_SHUTDOWN_, 0x2000);
}

static inline void arch_reset(void)
{
        outb(_X86_INIT_REGISTER_, 1);
}

#endif