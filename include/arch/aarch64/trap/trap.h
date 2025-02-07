#ifndef _SHAMPOOS_TRAP_H_
#define _SHAMPOOS_TRAP_H_
#include <common/types.h>

#define NR_IRQ 1019 /*in gic(v2) we can only use 0-1019*/
struct trap_frame {
        u64 trap_id;
};
void arch_init_interrupt(void);
void arch_unknown_trap_handler(struct trap_frame *tf);
#endif