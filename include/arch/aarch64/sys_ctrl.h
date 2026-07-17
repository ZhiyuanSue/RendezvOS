#ifndef _RENDEZVOS_SYS_CTRL_H_
#define _RENDEZVOS_SYS_CTRL_H_
#include "sys_ctrl_def.h"
#include <common/types.h>

/*write reg*/
#define msr(sys_reg, value) \
        __asm__ __volatile__("msr " sys_reg ", %0;" : : "r"(value))
/*read reg*/
#define mrs(sys_reg, value) \
        __asm__ __volatile__("mrs %0, " sys_reg "\n" : "=r"(value) : :)

static inline void set_vbar_el1(vaddr trap_vec)
{
        msr("VBAR_EL1", trap_vec);
}

static inline void arch_disable_irq(void)
{
        __asm__ __volatile__("msr daifset, 0x3" ::: "memory");
}
static inline void arch_enable_irq(void)
{
        __asm__ __volatile__("msr daifclr, 0x3" ::: "memory");
}
static inline u64 arch_save_and_disable_irq(void)
{
        u64 flags;
        __asm__ __volatile__("mrs %0, daif\n\t"
                             "msr daifset, #3"
                             : "=r"(flags)
                             :
                             : "memory");
        return flags;
}

static inline void arch_irq_restore(u64 flags)
{
        __asm__ __volatile__("msr daif, %0" : : "r"(flags) : "memory");
}
#endif