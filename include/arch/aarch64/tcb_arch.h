#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
typedef struct {
        u64 sp_el1; /*we only need to consider the el1 in task context*/
        u64 spsr_el1;
        /*x19-x30*/
        u64 regs[12];
} Arch_Task_Context;
#endif