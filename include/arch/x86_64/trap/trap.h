#ifndef _RENDEZVOS_X86_64_TRAP_H_
#define _RENDEZVOS_X86_64_TRAP_H_
#include <common/types.h>
#include <common/stdbool.h>
#include <arch/x86_64/desc.h>
#define NR_IRQ             256
#define TRAP_ID(trap_info) (trap_info)
enum TRAP_NUM {
        TRAP_DE,
        TRAP_DB,
        TRAP_NMI,
        TRAP_BP,
        TRAP_OF,
        TRAP_BR,
        TRAP_UD,
        TRAP_NM,
        TRAP_DF,
        TRAP_CSO, /*reserved,Coprocessor Segment Overrun*/
        TRAP_TS,
        TRAP_NP,
        TRAP_SS,
        TRAP_GP,
        TRAP_PF,
        TRAP_RESERVED,
        TRAP_MF,
        TRAP_AC,
        TRAP_MC,
        TRAP_XM,
        TRAP_VE,
        TRAP_ARCH_USED,
};
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
#define ARCH_SYSCALL_ID    rax
#define ARCH_SYSCALL_ARG_1 rdi
#define ARCH_SYSCALL_ARG_2 rsi
#define ARCH_SYSCALL_ARG_3 rdx
#define ARCH_SYSCALL_ARG_4 r10
#define ARCH_SYSCALL_ARG_5 r8
#define ARCH_SYSCALL_ARG_6 r9

void arch_init_interrupt(void);
void arch_unknown_trap_handler(struct trap_frame *tf);
static inline bool arch_int_from_kernel(struct trap_frame *tf)
{
        return tf->cs == sizeof(union desc) * GDT_KERNEL_CS_INDEX;
}
#endif