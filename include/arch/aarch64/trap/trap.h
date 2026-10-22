#ifndef _RENDEZVOS_AARCH64_TRAP_H_
#define _RENDEZVOS_AARCH64_TRAP_H_
#include <common/bit.h>
#include <common/types.h>
#include <arch/aarch64/gic/gic_v2.h>
#include <arch/aarch64/sys_ctrl_def.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/trap_common.h>
#include "trap_def.h"

#define AARCH64_IRQ_TO_TRAP_ID(irq_number) (irq_number + AARCH64_IRQ_OFFSET)
#define AARCH64_TRAP_ID_TO_IRQ(trap_id)    (trap_id - AARCH64_IRQ_OFFSET)

/*
 * Local / global id split (GIC INTID ↔ trap id via the macros above).
 */
#define ARCH_IRQ_INTID_IPI   GIC_V2_SGI_START /* 0 */
#define ARCH_IRQ_ID_IPI      AARCH64_IRQ_TO_TRAP_ID(ARCH_IRQ_INTID_IPI)
#define ARCH_IRQ_ID_ALLOC_LO AARCH64_IRQ_TO_TRAP_ID(GIC_V2_SPI_START)
#define ARCH_IRQ_ID_ALLOC_HI AARCH64_IRQ_TO_TRAP_ID(GIC_V2_SPI_END)

/*
 * LOCAL: [0, ALLOC_LO) = EC + SGI/PPI (structural; no reserve).
 * GLOBAL: one entry per global_irq_id; length from @c gic_v2_nr_spi.
 * global_irq_id is SPI index = INTID - SPI_START.
 * local_irq_id = INTID + AARCH64_IRQ_OFFSET (offset 64).
 *
 * Each CPU has the same local_irq_id number line. Low ids live in
 * local_irq_vector[]; high ids are virtual (not in that vector) and map 1:1
 * into the shared global_irq_vector[] (formula, no per-CPU map).
 *
 *   CPU0 local_irq_vector[] (real, private)     virtual ids (not in local)
 *   +-----+-----+-----+-----+                   +-----+-------+-----+
 *   |  0  |  1  | ... |LO-1 |                   | LO  | LO+1  | ... |
 *   +-----+-----+-----+-----+                   +--+--+--+----+-----+
 *                                                  |     |
 *                                                  |     |
 *                                                  v     v
 *   CPU1 local_irq_vector[] (real, private)     +-----+-------+-----+
 *   +-----+-----+-----+-----+                   | LO  | LO+1  | ... |
 *   |  0  |  1  | ... |LO-1 |                   +--+--+--+----+-----+
 *   +-----+-----+-----+-----+                      |     |
 *                                                  |     |
 *                                                  v     v
 *                    shared global_irq_vector[] (real, machine-wide)
 *                    +------+------+------+-----+
 *                    | g[0] | g[1] | g[2] | ... |
 *                    +--+---+--+---+------+-----+
 *                       ^      ^
 *                       |      |
 *              all CPUs LO     all CPUs LO+1     (1:1 formula)
 *
 * LO = ARCH_IRQ_ID_ALLOC_LO; g[i] = global_irq_vector[i] (SPI index i).
 */
#define NR_ARCH_LOCAL_IRQ ARCH_IRQ_ID_ALLOC_LO
#define NR_IRQ            NR_ARCH_LOCAL_IRQ /* compat */

/**
 * Global external irq. Index is @c global_irq_id (SPI index).
 * Fixed pair: global_irq_id = SPI index, local_irq_id = INTID + offset.
 */
struct global_irq {
        IRQ_COMMON
        atomic64_t irq_attr; /* CAS USED at alloc; @c IRQ_NEED_EOI, … */
        size_t local_irq_id; /* per-CPU id mapped on cpu_id */
        cpu_id_t cpu_id;
};

/*
 * local_irq_id → global_irq_id by formula (no per-CPU map table).
 * @c cpu_id is recorded at alloc for later GIC routing.
 * @c arch_map_local_irq_id is a no-op (always succeeds if cpu is valid).
 * @c arch_map_fixed_local_irq_id always returns the fixed local_irq_id.
 * Common APIs: @c rendezvos/trap/trap.h.
 */

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