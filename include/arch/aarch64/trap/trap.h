#ifndef _RENDEZVOS_AARCH64_TRAP_H_
#define _RENDEZVOS_AARCH64_TRAP_H_
#include <common/bit.h>
#include <common/types.h>
#include <arch/aarch64/gic/gic_v2.h>
#include <arch/aarch64/sys_ctrl_def.h>
#include <rendezvos/trap/trap_common.h>
#include "trap_def.h"

#define AARCH64_IRQ_TO_TRAP_ID(irq_number) (irq_number + AARCH64_IRQ_OFFSET)
#define AARCH64_TRAP_ID_TO_IRQ(trap_id)    (trap_id - AARCH64_IRQ_OFFSET)

/*
 * The core have used irq vec number
 */
#define ARCH_IRQ_INTID_IPI     GIC_V2_SGI_START /* 0 */
#define ARCH_IRQ_VEC_IPI       AARCH64_IRQ_TO_TRAP_ID(ARCH_IRQ_INTID_IPI)
#define ARCH_IRQ_VEC_ALLOC_LO  AARCH64_IRQ_TO_TRAP_ID(GIC_V2_SPI_START)
#define ARCH_IRQ_VEC_ALLOC_HI  AARCH64_IRQ_TO_TRAP_ID(GIC_V2_SPI_END)

#define TRAP_ID(trap_info)  (trap_info & AARCH64_TRAP_ID_MASK)
#define TRAP_SRC(trap_info) (trap_info & AARCH64_TRAP_SRC_MASK)
#define TRAP_GET_CPU(trap_info) \
        (((trap_info) & AARCH64_TRAP_CPU_MASK) >> AARCH64_TRAP_CPU_SHIFT)
#define TRAP_SET_CPU(trap_info, cpu_id)                                      \
        set_mask_u64(clear_mask_u64((trap_info), AARCH64_TRAP_CPU_MASK),     \
                     (((u64)(cpu_id) << AARCH64_TRAP_CPU_SHIFT)              \
                      & AARCH64_TRAP_CPU_MASK))
#define AARCH64_TRAP_GET_SRC_EL(trap_info) \
        ((trap_info & AARCH64_TRAP_SRC_EL_MASK) >> AARCH64_TRAP_SRC_EL_SHIFT)

/*
 * aarch64 specific trap information structure.
 *
 * Contains TRAP_COMMON fields and ARM-specific parsed information.
 * Does NOT duplicate trap_frame fields (ESR, FAR, etc.),
 * instead stores reference to trap_frame for accessing them.
 */
struct aarch64_trap_info {
        TRAP_COMMON

        /* ARM-specific parsed fields */
        struct {
                u32 ec : 6; /* Exception Class [5:0] */
                u32 iss : 25; /* Instruction Specific Syndrome [24:0] */
                u8 dfsc : 6; /* Data Fault Status Code */
                u8 ifsc : 6; /* Instruction Fault Status Code */
                u8 wnR : 1; /* Write not Read */
                u8 isv : 1; /* Instruction Syndrome Valid */
        } esr_fields;
};

/**
 * @brief Parser and fill aarch64_trap_info from a trap_frame 
 * including TRAP_COMMON and ESR fields.
 */
void arch_populate_trap_info(struct trap_frame *tf,
                             struct aarch64_trap_info *info);

struct trap_frame {
#define AARCH64_TRAP_SRC_EL_SHIFT (56)
#define AARCH64_TRAP_SRC_EL_MASK  (0xfUL << AARCH64_TRAP_SRC_EL_SHIFT)
#define AARCH64_TRAP_SRC_EL_0     (0UL)
#define AARCH64_TRAP_SRC_EL_1     (1UL)
        u64 trap_info;
        u64 REGS[31];

        u64 SPSR;
        u64 ELR;
        u64 SP;
        u64 ESR;
        u64 FAR;
        u64 HPFAR;
};
/* 
 * Cross arch macro for kernel using the same macro.
 * which means the syscall args and syscall id using what reg.
 */
#define ARCH_SYSCALL_ID    REGS[8]
#define ARCH_SYSCALL_RET   REGS[0]
#define ARCH_SYSCALL_ARG_1 REGS[0]
#define ARCH_SYSCALL_ARG_2 REGS[1]
#define ARCH_SYSCALL_ARG_3 REGS[2]
#define ARCH_SYSCALL_ARG_4 REGS[3]
#define ARCH_SYSCALL_ARG_5 REGS[4]
#define ARCH_SYSCALL_ARG_6 REGS[5]

#define AARCH64_ESR_EC_MASK (0xffUL << AARCH64_ESR_EC_SHIFT)
#define AARCH64_ESR_GET_EC(esr_value) \
        (((u64)(esr_value) & AARCH64_ESR_EC_MASK) >> AARCH64_ESR_EC_SHIFT)

/**
 * @brief Set VBAR_EL1 to trap_vec_table and enable IRQ.
 */
void arch_init_interrupt(void);

/**
 * @brief This function is used for init part.
 * The maintainer must understand the layout of irq_vector.
 * It's percpu.
 * Every cpu have a irq vector. But some irqs are global.
 * So there must have a broadcast——of course not here.
 * For init, cpu is boot one by one.
 * so it should also reserve the namespace of current cpu. 
 * and maintain the global namespace for global irq. 
 * The later enabled cpu should not confict with the prev cpu.
 * 
 * Per-CPU range: [0, AARCH64_IRQ_OFFSET) and SGI+PPI trap ids (intid 0–31).
 * That is global pool: [ARCH_IRQ_VEC_ALLOC_LO, ARCH_IRQ_VEC_ALLOC_HI] (SPI)
 */
void arch_init_irq_vector_state(void);

/**
 * @brief if this trap have not register a handler, using this default handler.
 * it just print the trap frame and just return.
 * a panic is recommend after this function.
 */
void arch_unknown_trap_handler(struct trap_frame *tf);

/**
 * @brief whether this trap comes from kernel, if so, return true, else false.
 */
static inline bool arch_int_from_kernel(struct trap_frame *tf)
{
        return SPSR_EL1_M_3_0(tf->SPSR) != SPSR_EL1_M_64_EL0;
}

/**
 * @brief get faulting VA after sync abort from tf->FAR(FAR_ELx); 0 if @p tf is NULL.
 */
static inline vaddr arch_get_fault_addr(struct trap_frame *tf)
{
        return tf ? (vaddr)tf->FAR : (vaddr)0;
}
#endif