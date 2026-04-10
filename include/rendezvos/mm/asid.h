#ifndef _RENDEZVOS_MM_ASID_H_
#define _RENDEZVOS_MM_ASID_H_

#include <common/types.h>
#include <common/stdbool.h>

/*
 * ASID allocator (recyclable); asid.c uses BITMAP_DEFINE_TYPE + BITMAP_OPS.
 * - ASID 0 is reserved (used as "no ASID" / boot).
 * - Allocated ASIDs are in [1, asid_max], with asid_max either U8_MAX or
 *   U16_MAX (see common/limits.h) depending on arch_asid_supports_16bit().
 */

#ifdef _AARCH64_
#include <arch/aarch64/mm/asid.h>
#elif defined _X86_64_
#include <arch/x86_64/mm/asid.h>
#else
static inline bool arch_asid_supports_16bit(void)
{
        return true;
}
#endif

void asid_init(void);
asid_t asid_alloc(void);
void asid_free(asid_t asid);
asid_t asid_get_max(void);

#endif
