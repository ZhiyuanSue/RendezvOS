#ifndef _RENDEZVOS_ATOMIC_H_
#define _RENDEZVOS_ATOMIC_H_
/*
    here we try use gcc Built-in atomic functions
    see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
*/
#include <common/types.h>
typedef struct{
    volatile i32 counter;
}atomic_t;
typedef struct{
    volatile i64 counter;
}atomic64_t;
#endif