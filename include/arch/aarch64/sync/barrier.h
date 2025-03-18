#ifndef _RENDEZVOS_BARRIER_H_
#define _RENDEZVOS_BARRIER_H_

#define isb()    __asm__ __volatile__("isb" : : : "memory")
#define dmb(opt) __asm__ __volatile__("dmb " #opt : : : "memory")
#define dsb(opt) __asm__ __volatile__("dsb " #opt : : : "memory")
#endif