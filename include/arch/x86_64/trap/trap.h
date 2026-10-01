#ifndef _RENDEZVOS_X86_64_TRAP_H_
#define _RENDEZVOS_X86_64_TRAP_H_
#include <common/types.h>
#include <common/stdbool.h>
#include <arch/x86_64/desc.h>
#include <rendezvos/trap/trap_common.h> /* For TRAP_COMMON and enum trap_class */
#include "trap_def.h" /* For PF error code bits */

#define NR_IRQ             256
#define TRAP_ID(trap_info) (trap_info)

/*
 * The core have used irq vec number
 */
#define ARCH_IRQ_VEC_TIMER    0x20u
#define ARCH_IRQ_VEC_SPURIOUS 0x27u
#define ARCH_IRQ_VEC_IPI      0x30u
#define ARCH_IRQ_VEC_ALLOC_LO 0x40u
#define ARCH_IRQ_VEC_ALLOC_HI 0xEFu

enum TRAP_NUM {
        TRAP_DE, /* 0: Divide Error */
        TRAP_DB, /* 1: Debug Exception */
        TRAP_NMI, /* 2: Non-Maskable Interrupt */
        TRAP_BP, /* 3: Breakpoint */
        TRAP_OF, /* 4: Overflow */
        TRAP_BR, /* 5: BOUND Range Exceeded */
        TRAP_UD, /* 6: Invalid Opcode */
        TRAP_NM, /* 7: Device Not Available (No Math) */
        TRAP_DF, /* 8: Double Fault */
        TRAP_CSO, /* 9: Coprocessor Segment Overrun (obsolete) */
        TRAP_TS, /* 10: Invalid TSS */
        TRAP_NP, /* 11: Segment Not Present */
        TRAP_SS, /* 12: Stack-Segment Fault */
        TRAP_GP, /* 13: General Protection */
        TRAP_PF, /* 14: Page Fault */
        TRAP_RESERVED, /* 15: Reserved */
        TRAP_MF, /* 16: x87 FPU Floating-Point Error */
        TRAP_AC, /* 17: Alignment Check */
        TRAP_MC, /* 18: Machine Check */
        TRAP_XM, /* 19: SIMD Floating-Point Exception */
        TRAP_VE, /* 20: Virtualization Exception */
        TRAP_21 = 21, /* Reserved (Intel-defined) */
        TRAP_22 = 22, /* Reserved */
        TRAP_23 = 23, /* Reserved */
        TRAP_24 = 24, /* Reserved */
        TRAP_25 = 25, /* Reserved */
        TRAP_26 = 26, /* Reserved */
        TRAP_27 = 27, /* Reserved */
        TRAP_28 = 28, /* Reserved */
        TRAP_29 = 29, /* Reserved */
        TRAP_SE, /* 30: Security Exception */
        TRAP_31 = 31, /* Reserved */
        TRAP_ARCH_USED, /* Upper boundary (32): defines array size for
                           x86_trap_class_map[] */
};

/*
 * IMPORTANT: TRAP_ARCH_USED semantics
 *
 * TRAP_ARCH_USED defines the UPPER BOUND of the exception vector range (0-31).
 * It is used as:
 *   - Array size: static enum trap_class x86_trap_class_map[TRAP_ARCH_USED]
 *   - Loop bound: for (i = 0; i < TRAP_ARCH_USED; i++)
 *
 * TRAP_ARCH_USED is NOT:
 *   - The count of "actually used" exception vectors
 *   - A counter of how many exceptions are defined
 *   - To be used in logic like "if (trap_id < TRAP_ARCH_USED - 1)"
 *
 * If Intel adds new exception vectors in the future (e.g., vector 29, 30),
 * TRAP_ARCH_USED will automatically expand to include them.
 */

/*
 * x86_64 specific trap information structure.
 *
 * Contains TRAP_COMMON fields and x86-specific parsed information.
 * Does NOT duplicate trap_frame fields (error_code, etc.),
 * instead stores reference to trap_frame for accessing them.
 */
struct x86_64_trap_info {
        TRAP_COMMON

        /* x86-specific parsed fields */
        u64 cr2; /* Page fault address (from CR2) */

        /* Detailed PF error code parsing */
        struct {
                u8 page_present : 1;
                u8 write_access : 1;
                u8 user_mode : 1;
                u8 reserved_bit : 1;
                u8 instruction_fetch : 1;
                u8 protection_key : 1;
                u8 shadow_stack : 1;
                u8 hv_mmio : 1;
                u32 reserved_bits : 24;
        } pf_ec;
};

/**
 * @brief Parser and fill x86_64_trap_info from a trap_frame 
 * including TRAP_COMMON and pf_ec fields.
 *
 * @param tf Trap frame need to parser
 * @param info output infos
 */
void arch_populate_trap_info(struct trap_frame *tf,
                             struct x86_64_trap_info *info);

struct trap_frame {
        u64 trap_info;
        u64 r15;
        u64 r14;
        u64 r13;
        u64 r12;
        u64 rbp;
        u64 rbx;

        u64 r11;
        u64 r10;
        u64 r9;
        u64 r8;
        u64 rax;
        u64 rcx;
        u64 rdx;
        u64 rsi;
        u64 rdi;

        u64 error_code;
        u64 rip;
        u64 cs;
        u64 eflags;
        u64 rsp;
        u64 ss;
};
/* 
 * Cross arch macro for kernel using the same macro.
 * which means the syscall args and syscall id using what reg.
 */
#define ARCH_SYSCALL_ID    rax
#define ARCH_SYSCALL_RET   rax
#define ARCH_SYSCALL_ARG_1 rdi
#define ARCH_SYSCALL_ARG_2 rsi
#define ARCH_SYSCALL_ARG_3 rdx
#define ARCH_SYSCALL_ARG_4 r10
#define ARCH_SYSCALL_ARG_5 r8
#define ARCH_SYSCALL_ARG_6 r9

/**
 * @brief Load this CPU's IDT (lidt).
 */
void arch_init_interrupt(void);

/**
 * @brief Reserve x86 fixed vectors on this CPU; publish device alloc pool.
 * 
 * The maintainer must understand the layout of irq_vector.
 * It's percpu.
 * Every cpu have a irq vector. But some irqs are global.
 * So there must have a broadcast——of course not here.
 * For init, cpu is boot one by one.
 * so it should also reserve the namespace of current cpu. 
 * and maintain the global namespace for global irq. 
 * The later enabled cpu should not confict with the prev cpu.
 * 
 * Per-CPU USED: [0, TRAP_ARCH_USED), timer 0x20, spurious 0x27, IPI 0x30.
 * Global pool: [ARCH_IRQ_VEC_ALLOC_LO, ARCH_IRQ_VEC_ALLOC_HI].
 * Handler install is later (except spurious often stays empty).
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
 *
 * @note Compares @c tf->cs to the kernel CS descriptor selector.
 */
static inline bool arch_int_from_kernel(struct trap_frame *tf)
{
        return tf->cs == sizeof(union desc) * GDT_KERNEL_CS_INDEX;
}

/**
 * @brief get faulting VA for #PF from CR2; @p tf unused on x86_64.
 */
static inline vaddr arch_get_fault_addr(struct trap_frame *tf)
{
        (void)tf;
        vaddr addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
}
#endif