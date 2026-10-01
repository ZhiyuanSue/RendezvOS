#ifndef _RENDEZVOS_TRAP_DEF_H_

/*in gic(v2) we only use 0-1019, and we use 64 as the sync trap*/
#define NR_IRQ             1084
/**
 * In order to put 64 sync trap and irqs into one vector, we add this offset.
 * The first 64 number is used for sync trap, and the later part is irqs.
 */
#define AARCH64_IRQ_OFFSET 64

/* the same bits as that in irq source
 *   [9:0]   trap id = INTID + AARCH64_IRQ_OFFSET
 *   [12:10] IAR CPUID for SGI EOI
 *   higher  SRC_EL etc.
 */
#define AARCH64_TRAP_ID_MASK   0x2FF
#define AARCH64_TRAP_SRC_MASK  0x1FFF
#define AARCH64_TRAP_CPU_MASK  0x1C00
#define AARCH64_TRAP_CPU_SHIFT 10
#define TRAP_TYPE_SYNC         1
#define TRAP_TYPE_IRQ          2
#define TRAP_TYPE_FIQ          3

/*esr*/
#define AARCH64_ESR_EC_SHIFT 26
#define AARCH64_ESR_IL_SHIFT 25
#endif