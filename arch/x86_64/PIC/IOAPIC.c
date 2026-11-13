#include <arch/x86_64/PIC/IOAPIC.h>
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <common/limits.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/mm_kernel_utils.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>
#include <arch/x86_64/trap/irq_space.h>

struct ioapic *ioapics;
size_t nr_ioapics;

static inline vaddr ioapic_get_base_va(const struct ioapic *ioapic_entry)
{
        return KERNEL_PHY_TO_VIRT(ioapic_entry->mmio_phy_base);
}

static inline u32 ioapic_rte_low_index(u16 pin)
{
        return IOAPIC_RTE_START_NUM + (u32)pin * 2;
}

static inline u32 ioapic_rte_high_index(u16 pin)
{
        return ioapic_rte_low_index(pin) + 1;
}

error_t ioapic_get_entry_from_gsi(gsi_id_t gsi,
                                  struct ioapic **ioapic_entry_out,
                                  u16 *pin_out)
{
        if (!ioapics || !nr_ioapics)
                return -E_REND_NOFOUND;

        for (size_t i = 0; i < nr_ioapics; i++) {
                struct ioapic *ioapic_entry = &ioapics[i];
                gsi_id_t end = ioapic_entry->gsi_base
                               + (gsi_id_t)ioapic_entry->nr_pins;

                if (gsi >= ioapic_entry->gsi_base && gsi < end) {
                        *ioapic_entry_out = ioapic_entry;
                        *pin_out = (u16)(gsi - ioapic_entry->gsi_base);
                        return REND_SUCCESS;
                }
        }
        return -E_REND_NOFOUND;
}

static void ioapic_write_rte(struct ioapic *ioapic_entry, u16 pin,
                             struct IOAPIC_RTE *rte)
{
        vaddr base = ioapic_get_base_va(ioapic_entry);

        IOAPIC_WR_REG(base, ioapic_rte_high_index(pin), rte->reg_2);
        IOAPIC_WR_REG(base, ioapic_rte_low_index(pin), rte->reg_1);
}

static struct IOAPIC_RTE ioapic_read_rte(struct ioapic *ioapic_entry, u16 pin)
{
        struct IOAPIC_RTE rte;
        vaddr base = ioapic_get_base_va(ioapic_entry);

        rte.reg_1 = IOAPIC_RD_REG(base, ioapic_rte_low_index(pin));
        rte.reg_2 = IOAPIC_RD_REG(base, ioapic_rte_high_index(pin));
        return rte;
}

static void ioapic_mask_all_pins(struct ioapic *ioapic_entry)
{
        for (u16 pin = 0; pin < ioapic_entry->nr_pins; pin++) {
                struct IOAPIC_RTE rte = ioapic_read_rte(ioapic_entry, pin);

                rte.Mask = 1;
                ioapic_write_rte(ioapic_entry, pin, &rte);
        }
}

static error_t ioapic_probe_all(void)
{
        const ENTRY_FLAGS_t ioapic_flags =
                PAGE_ENTRY_UNCACHED | PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ
                | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE;
        struct VSpace *vs = per_cpu(current_vspace, BSP_ID);
        struct map_handler *handler = &per_cpu(Map_Handler, BSP_ID);
        error_t error;

        for (size_t i = 0; i < nr_ioapics; i++) {
                struct ioapic *ioapic_entry = &ioapics[i];

                error = ensure_kernel_identity_range(
                        vs,
                        ioapic_entry->mmio_phy_base,
                        PAGE_SIZE,
                        ioapic_flags,
                        handler);
                if (error != REND_SUCCESS) {
                        print("[ IOAPIC ] ERROR: ensure map id %u failed\n",
                              ioapic_entry->ioapic_id);
                        return error;
                }

                vaddr base = ioapic_get_base_va(ioapic_entry);
                u32 version = IOAPIC_RD_REG(base, IOAPIC_VERSION_NUM);
                u32 max_rte = (version & IOAPIC_VERSION_MAX_RTE_MASK)
                              >> IOAPIC_VERSION_MAX_RTE_OFF;
                ioapic_entry->version = (u8)(version & IOAPIC_VERSION_MASK);
                ioapic_entry->nr_pins = (u16)(max_rte + 1u);
                if (ioapic_entry->version < IOAPIC_VERSION_VALUE_LEGAL) {
                        print("[ IOAPIC ] ERROR: id %u ver 0x%x < 0x%x (need EOI reg)\n",
                              ioapic_entry->ioapic_id,
                              ioapic_entry->version,
                              IOAPIC_VERSION_VALUE_LEGAL);
                        return -E_RENDEZVOS;
                }

                ioapic_mask_all_pins(ioapic_entry);
                print("[ IOAPIC ] id %u ver 0x%x pins %u gsi [%u, %u)\n",
                      ioapic_entry->ioapic_id,
                      ioapic_entry->version,
                      ioapic_entry->nr_pins,
                      (u32)ioapic_entry->gsi_base,
                      (u32)(ioapic_entry->gsi_base + ioapic_entry->nr_pins));
        }
        return REND_SUCCESS;
}

size_t ioapic_nr_gsi(void)
{
        if (!ioapics || !nr_ioapics)
                return 0;
        struct ioapic *last_ioapic_entry = &ioapics[nr_ioapics - 1];

        return (size_t)last_ioapic_entry->gsi_base
               + (size_t)last_ioapic_entry->nr_pins;
}

error_t ioapic_init(void)
{
        error_t e;

        if (!ioapics || !nr_ioapics)
                return REND_SUCCESS;

        e = ioapic_probe_all();
        if (e != REND_SUCCESS)
                return e;
        return irq_space_ioapic_register();
}

error_t ioapic_configure_rte(gsi_id_t gsi, u8 vector, u8 dest_apic_id,
                             u32 delivery_mode, u32 dest_mode, u32 trigger_mode,
                             u32 pin_polarity)
{
        struct ioapic *ioapic_entry;
        u16 pin;
        struct IOAPIC_RTE rte = {.reg_1 = 0, .reg_2 = 0};

        /* APIC-delivered vectors 0–15 are reserved for exceptions. */
        if (vector < 16u)
                return -E_IN_PARAM;
        if (delivery_mode > 0x7u || dest_mode > 1u || trigger_mode > 1u
            || pin_polarity > 1u)
                return -E_IN_PARAM;

        error_t e = ioapic_get_entry_from_gsi(gsi, &ioapic_entry, &pin);
        if (e != REND_SUCCESS)
                return e;

        rte.vector = vector;
        rte.Delivery_Mode = delivery_mode;
        rte.Dest_Mode = dest_mode;
        rte.Pin_Polarity = pin_polarity;
        rte.Trigger_Mode = trigger_mode;
        rte.Mask = 1; /* stay masked until ioapic_change_mask(..., false) */
        rte.Destination = dest_apic_id;
        ioapic_write_rte(ioapic_entry, pin, &rte);
        return REND_SUCCESS;
}

error_t ioapic_change_mask(gsi_id_t gsi, bool mask)
{
        struct ioapic *ioapic_entry;
        u16 pin;

        error_t e = ioapic_get_entry_from_gsi(gsi, &ioapic_entry, &pin);
        if (e != REND_SUCCESS)
                return e;

        struct IOAPIC_RTE rte = ioapic_read_rte(ioapic_entry, pin);
        rte.Mask = mask ? 1 : 0;
        ioapic_write_rte(ioapic_entry, pin, &rte);
        return REND_SUCCESS;
}

void ioapic_eoi(struct ioapic *ioapic_entry, u8 vector)
{
        if (!ioapic_entry)
                return;

        *((volatile u32 *)(ioapic_get_base_va(ioapic_entry) + IOAPIC_EOI)) =
                (u32)vector;
}
