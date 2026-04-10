#include <rendezvos/mm/asid.h>

#include <common/dsa/bitmap.h>
#include <common/limits.h>
#include <common/stdbool.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>

/*
 * ASID ids are asid_t (u16); index 0 reserved. Bitmap covers every possible
 * asid_t index
 * so the backing store size follows U16_MAX without magic numbers.
 */
#define ASID_BITMAP_BITS ((u32)U16_MAX + 1u)

BITMAP_DEFINE_TYPE(asid_bitmap_t, ASID_BITMAP_BITS)

static spin_lock asid_lock;
DEFINE_PER_CPU(spin_lock_t, asid_mcs_node);
static asid_t asid_max = U8_MAX;

static asid_bitmap_t asid_bitmap;

void asid_init(void)
{
        asid_lock = NULL;
        asid_max = arch_asid_supports_16bit() ? U16_MAX : U8_MAX;

        BITMAP_OPS(asid_bitmap, zero)(&asid_bitmap);
        /* Reserve ASID 0. */
        BITMAP_OPS(asid_bitmap, set)(&asid_bitmap, 0);
}

asid_t asid_get_max(void)
{
        return asid_max;
}

asid_t asid_alloc(void)
{
        lock_mcs(&asid_lock, &percpu(asid_mcs_node));
        for (u32 asid = 1; asid <= (u32)asid_max; asid++) {
                if (!BITMAP_OPS(asid_bitmap, test)(&asid_bitmap, asid)) {
                        BITMAP_OPS(asid_bitmap, set)(&asid_bitmap, asid);
                        unlock_mcs(&asid_lock, &percpu(asid_mcs_node));
                        return (asid_t)asid;
                }
        }
        unlock_mcs(&asid_lock, &percpu(asid_mcs_node));
        return 0;
}

void asid_free(asid_t asid)
{
        if (asid == 0 || asid > asid_max)
                return;

        lock_mcs(&asid_lock, &percpu(asid_mcs_node));
        BITMAP_OPS(asid_bitmap, clear)(&asid_bitmap, asid);
        unlock_mcs(&asid_lock, &percpu(asid_mcs_node));
}
