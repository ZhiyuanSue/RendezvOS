#ifndef _SHAMPOOS_BARRIER_H_
#define _SHAMPOOS_BARRIER_H_

#define isb()    asm volatile("isb" : : : "memory")
#define dmb(opt) asm volatile("dmb" #opt : : : "memory")
#define dsb(opt) asm volatile("dsb" #opt : : : "memory")
#endif