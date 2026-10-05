#ifndef _RENDEZVOS_TRAP_DEF_H_

/*in gic(v2) we only use 0-1019, and we use 64 as the sync trap*/
#define NR_IRQ             1084
/**
 * In order to put 64 sync trap and irqs into one vector, we add this offset.
 * The first 64 number is used for sync trap, and the later part is irqs.
 */
#define AARCH64_IRQ_OFFSET 64

/* the same bits as that in irq source
 *   [10:0]  trap id = INTID + AARCH64_IRQ_OFFSET (11 bits, max 2047; fits SPI 1019 + 64 = 1083)
 *   [13:11] IAR CPUID for SGI EOI (3 bits, max 7)
 *   higher  SRC_EL etc.
 *
 * Note: union irq_source in gic_v2.h mirrors the GICC_IAR/EOIR hardware
 * layout (INTID[9:0] + CPUID[12:10]) and is NOT affected by this software
 * packing — the two are decoupled; conversion goes through source.irq_id
 * (10 bits, holds INTID <= 1019) and source.cpu_id (3 bits, holds CPUID <= 7).
 */
#define AARCH64_TRAP_ID_MASK   0x7FF
#define AARCH64_TRAP_SRC_MASK  0x3FFF
#define AARCH64_TRAP_CPU_MASK  0x3800
#define AARCH64_TRAP_CPU_SHIFT 11
#define TRAP_TYPE_SYNC         1
#define TRAP_TYPE_IRQ          2
#define TRAP_TYPE_FIQ          3

/*esr*/
#define AARCH64_ESR_EC_SHIFT 26
#define AARCH64_ESR_IL_SHIFT 25
#endif