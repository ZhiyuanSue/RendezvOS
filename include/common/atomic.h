#ifndef _RENDEZVOS_ATOMIC_H_
#define _RENDEZVOS_ATOMIC_H_
/*
    here we try use gcc Built-in atomic functions
    see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
*/
#include <common/types.h>
#ifdef _AARCH64_
#include <arch/aarch64/sync/atomic.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/sync/atomic.h>
#elif defined _RISCV64_
#include <arch/riscv64/sync/atomic.h>
#elif defined _X86_64_
#include <arch/x86_64/sync/atomic.h>
#else
#include <arch/x86_64/sync/atomic.h>
#endif
typedef struct {
        volatile i32 counter;
} atomic_t;
typedef struct {
        volatile i64 counter;
} atomic64_t;
#endif