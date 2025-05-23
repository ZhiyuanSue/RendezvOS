#ifndef _RENDEZVOS_TCB_ARCH_
#define _RENDEZVOS_TCB_ARCH_

#include <common/types.h>
typedef struct {
        u64 sp;
        u64 next_pc;
} Arch_Context;
#endif