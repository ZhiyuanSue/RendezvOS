#ifndef _RENDEZVOS_ARCH_BARRIER_H_
#define _RENDEZVOS_ARCH_BARRIER_H_
/**
 * @brief Store fence (x86 SFENCE). Rarely used on lock paths.
 */
#define sfence() __asm__ __volatile__("sfence" : : : "memory")
/**
 * @brief Load fence (x86 LFENCE).
 */
#define lfence() __asm__ __volatile__("lfence" : : : "memory")
/**
 * @brief Full fence (x86 MFENCE).
 */
#define mfence() __asm__ __volatile__("mfence" : : : "memory")

/**
 * @brief Compiler memory barrier only.
 */
#define barrier()        __asm__ __volatile__("" : : : "memory")
/**
 * @brief Spin hint: PAUSE.
 */
#define arch_cpu_relax() __asm__ __volatile__("pause\n" : : : "memory")
#endif
