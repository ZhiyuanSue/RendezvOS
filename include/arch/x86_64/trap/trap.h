#ifndef _RENDEZVOS_X86_64_TRAP_H_
#define _RENDEZVOS_X86_64_TRAP_H_
#include <common/types.h>
#include <common/stdbool.h>
#include <arch/x86_64/desc.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/trap/trap_common.h> /* For TRAP_COMMON / IRQ_COMMON */
#include "trap_def.h" /* For PF error code bits */

/*
 * LOCAL: [0, ALLOC_LO) — exceptions + APIC LVT/IPI (structural; no reserve).
 * GLOBAL: one entry per global_irq_id (GSI). local_irq_id → global_irq_id
 * via the per-CPU map below. IDT hardware table is always @c NR_IDT_VECTORS
 * (256).
 */
#define ARCH_IRQ_ID_TIMER    0x20u
#define ARCH_IRQ_ID_SPURIOUS 0x27u
#define ARCH_IRQ_ID_IPI      0x30u
#define ARCH_IRQ_ID_ALLOC_LO 0x40u
#define ARCH_IRQ_ID_ALLOC_HI 0xEFu

#define NR_ARCH_LOCAL_IRQ ARCH_IRQ_ID_ALLOC_LO
/*
 * Temporary global_irq_id count until MADT is parsed (GSI count).
 * @c arch_start_platform passes this to @c irq_global_vector_alloc.
 */
#define NR_ARCH_GLOBAL_IRQ 256
#define NR_IDT_VECTORS     256
#define NR_IRQ             NR_IDT_VECTORS

#define TRAP_ID(trap_info) (trap_info)

/**
 * Global external irq. Index is @c global_irq_id (GSI).
 * Dispatch: local_irq_id → per-CPU map → @c global_irq_vector[global_irq_id].
 * Arch-extra: IOAPIC pin, filled when the route is programmed.
 */
struct global_irq {
        IRQ_COMMON
        atomic64_t irq_attr; /* CAS USED at alloc; @c IRQ_NEED_EOI, … */
        size_t local_irq_id; /* per-CPU id mapped on cpu_id */
        cpu_id_t cpu_id;
        u8 ioapic_id;
        u8 pin;
};

/*
 * Per-CPU map @c local_irq_map[] (u64, CAS): local_irq_id → global_irq_id+1
 * (0 = not mapped). Alloc: CAS irq_attr on the chosen global_irq_id, then
 * CAS this map on cpu_id only. @c arch_map_fixed_local_irq_id returns
 * INVALID so alloc scans [ALLOC_LO, ALLOC_HI] for a free local_irq_id.
 */

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