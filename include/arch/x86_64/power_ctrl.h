#ifndef _RENDEZVOS_X86_64_POWER_CTRL_H_
#define _RENDEZVOS_X86_64_POWER_CTRL_H_

#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>

/**
 * @brief x86 shutdown: outw(0x604, 0x2000), which is just QEMU/SeaBIOS.
 * but actually, we should using ACPI table to do it on a real machine.
 */
static inline void arch_shutdown(void)
{
        /*have no idea,maybe it works*/
        outw(_X86_POWER_SHUTDOWN_, 0x2000);
}

/**
 * @brief Soft reset via port 0x92 bit0. unused now
 */
static inline void arch_reset(void)
{
        outb(_X86_INIT_REGISTER_, 1);
}

#endif