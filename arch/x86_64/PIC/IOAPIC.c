#include <arch/x86_64/PIC/IOAPIC.h>
#include <modules/log/log.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/mm_kernel_utils.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/trap/irq_space.h>

struct ioapic *ioapics;
size_t nr_ioapics;

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

                vaddr base = KERNEL_PHY_TO_VIRT(ioapic_entry->mmio_phy_base);
                u32 version = IOAPIC_RD_REG(base, IOAPIC_VERSION_NUM);
                u32 max_rte = (version & IOAPIC_VERSION_MAX_RTE_MASK)
                              >> IOAPIC_VERSION_MAX_RTE_OFF;
                ioapic_entry->nr_pins = (u16)(max_rte + 1u);
                print("[ IOAPIC ] id %u pins %u gsi [%u, %u)\n",
                      ioapic_entry->ioapic_id,
                      ioapic_entry->nr_pins,
                      (u32)ioapic_entry->gsi_base,
                      (u32)(ioapic_entry->gsi_base + ioapic_entry->nr_pins));
        }
        return REND_SUCCESS;
}

static size_t ioapic_nr_gsi(void)
{
        struct ioapic *last = &ioapics[nr_ioapics - 1];
        return (size_t)last->gsi_base + (size_t)last->nr_pins;
}

static error_t ioapic_setup_global_irq(void)
{
        for (size_t i = 0; i < nr_ioapics; i++) {
                struct ioapic *ioapic_entry = &ioapics[i];

                for (u16 pin = 0; pin < ioapic_entry->nr_pins; pin++) {
                        gsi_id_t gsi = (gsi_id_t)(ioapic_entry->gsi_base + pin);
                        struct arch_global_irq *entry =
                                &global_irq_vector[(global_irq_id_t)gsi];

                        entry->ioapic_id = ioapic_entry->ioapic_id;
                        entry->pin = (u8)pin;
                }
        }
        return REND_SUCCESS;
}

error_t ioapic_init(void)
{
        error_t e;
        size_t nr_gsi;

        if (!ioapics || !nr_ioapics)
                return REND_SUCCESS;

        e = ioapic_probe_all();
        if (e != REND_SUCCESS)
                return e;

        nr_gsi = ioapic_nr_gsi();
        if (!nr_gsi) {
                print("[ IOAPIC ] ERROR: GSI 0 with %lu not 0 ioapics\n",
                      (unsigned long)nr_ioapics);
                return -E_RENDEZVOS;
        }
        e = irq_global_vector_alloc(nr_gsi);
        if (e != REND_SUCCESS)
                return e;
        return ioapic_setup_global_irq();
}
