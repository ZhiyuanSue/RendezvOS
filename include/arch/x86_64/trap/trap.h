#ifndef _SHAMPOOS_TRAP_H_
#define _SHAMPOOS_TRAP_H_
#include <common/types.h>
#define IDT_LIMIT 256
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
void init_interrupt(void);
void trap_handler(struct trap_frame* tf);
void arch_eor_irq(void);
#endif