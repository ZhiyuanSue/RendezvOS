#ifndef _RENDEZVOS_TLB_CPU_MASK_H_
#define _RENDEZVOS_TLB_CPU_MASK_H_

#include <common/dsa/bitmap.h>
#include <rendezvos/limits.h>

/* One bit per logical CPU in [0, RENDEZVOS_MAX_CPU_NUMBER). */
#define VS_TLB_CPU_MASK_BITS (RENDEZVOS_MAX_CPU_NUMBER)
BITMAP_DEFINE_TYPE(vs_tlb_cpu_bitmap_t, VS_TLB_CPU_MASK_BITS)

#endif
