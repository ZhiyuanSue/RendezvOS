#ifndef _SHAMPOOS_TRAP_H_
#define _SHAMPOOS_TRAP_H_
#include <common/types.h>
#include <arch/aarch64/gic/gic_v2.h>
#include "trap_def.h"

/*
        we let the highest 8 bits(bit 56-63) represent the source el
        and the last 32bits(bit 0-31) represent the trap number
        for the trap number, 0-63 is the sync error
        and the 64-1083 used for gic irq number + 64
*/
#define TRAP_ID(trap_frame) (trap_frame->trap_info & AARCH64_TRAP_ID_MASK)
#define AARCH64_TRAP_GET_SRC_EL(trap_frame)                 \
        ((trap_frame->trap_info & AARCH64_TRAP_SRC_EL_MASK) \
         >> AARCH64_TRAP_SRC_EL_SHIFT)

struct trap_frame {
#define AARCH64_TRAP_ID_MASK (0xffff)

#define AARCH64_TRAP_SRC_EL_SHIFT (56)
#define AARCH64_TRAP_SRC_EL_MASK  (0xf << AARCH64_TRAP_SRC_EL_SHIFT)
#define AARCH64_TRAP_SRC_EL_0     (0)
#define AARCH64_TRAP_SRC_EL_1     (1)
        u64 trap_info;
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
void arch_eoi_irq(struct irq_source source);
#endif