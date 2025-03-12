#ifndef _SHAMPOOS_TRAP_H_
#define _SHAMPOOS_TRAP_H_
#include <common/types.h>

#define NR_IRQ                                                                 \
        1084 /*in gic(v2) we can only use 0-1019, and we use 64 as the sync ec \
                trap*/
struct trap_frame {
        u64 trap_id;
        u64 REGS[31];

        u64 SPSR;
        u64 ELR;
        u64 SP;
        u64 ESR;
        u64 FAR;
        u64 HPFAR;
        u64 TPIDR_EL0;
};
void arch_init_interrupt(void);
void arch_unknown_trap_handler(struct trap_frame *tf);
#endif