#ifndef _RENDEZVOS_MM_ASID_H_
#define _RENDEZVOS_MM_ASID_H_

#include <common/types.h>

/**
 * @file asid.h
 * @brief Software Address Space ID allocator (recyclable bitmap).
 *
 * ASID 0 is reserved (boot / root_vspace / "no ASID"). Allocated ids are in
 * [1, asid_get_max()], where the max is @c (1u << width) - 1 from
 * @ref arch_asid_supported_width (aarch64: 8 or 16; x86: 12 for PCID).
 * 
 * There is no generation/epoch: reuse is safe only after tlb_cpu_mask is
 * empty and related CPUs have flushed (see TLB docs).
 *
 * Hardware use: aarch64 packs the id into TTBR0_EL1; x86 currently ignores it
 * when loading CR3 (no PCID). @c asid_t is @c u16 (@c common/types.h).
 */

#ifdef _AARCH64_
#include <arch/aarch64/mm/asid.h>
#elif defined _X86_64_
#include <arch/x86_64/mm/asid.h>
#else
/*default set 16 for other arch*/
static inline u32 arch_asid_supported_width(void)
{
        return 16;
}
#endif

/**
 * @brief Initialize the global ASID bitmap to all 0 and set asid_max).It's BSP-only.
 */
void asid_init(void);

/**
 * @brief Allocate a free ASID in [1, asid_max].
 * @return New id, or 0 if fail (0 should not be alloced).
 * @note it's MCS-locked and uses current CPU's asid_mcs_node.
 */
asid_t asid_alloc(void);

/**
 * @brief Return @p asid to the [1, asid_max] pool.
 * @note No-op if @p asid is illegal.It's MCS-locked
 */
void asid_free(asid_t asid);

/**
 * @brief get upper bound asid_max.
 *
 * Equal to @c (1u << arch_asid_supported_width()) - 1 
 */
asid_t asid_get_max(void);

#endif
