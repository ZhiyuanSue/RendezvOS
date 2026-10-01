#ifndef _RENDEZVOS_ARCH_BARRIER_H_
#define _RENDEZVOS_ARCH_BARRIER_H_

/**
 * @brief Instruction sync barrier
 */
#define isb()    __asm__ __volatile__("isb" : : : "memory")
/**
 * @brief Data memory barrier
 */
#define dmb(opt) __asm__ __volatile__("dmb " #opt : : : "memory")
/**
 * @brief Data sync barrier
 */
#define dsb(opt) __asm__ __volatile__("dsb " #opt : : : "memory")

/**
 * @brief Compiler memory barrier
 */
#define barrier()        __asm__ __volatile__("" : : : "memory")
/**
 * @brief Spin hint: YIELD.
 */
#define arch_cpu_relax() __asm__ __volatile__("yield" : : : "memory")
#endif
