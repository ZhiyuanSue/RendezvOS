#include <arch/x86_64/PIC/IOAPIC.h>
#include <arch/x86_64/trap/irq_space.h>
#include <arch/x86_64/trap/trap.h>
#include <modules/log/log.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/trap/irq_space.h>

/*
 * IOAPIC irq_space — GSI span, topology fill, line enable/disable/eoi.
 * Shared IDT delivery (choose/bind entry*) comes from arch_irq_space_*.
 * Hardware MMIO stays in PIC/IOAPIC.c.
 */

/* global_irq_id == GSI (space base 0): entry index in priv[]. */
static gsi_id_t ioapic_space_entry_gsi(const struct arch_global_irq *entry)
{
        const struct arch_global_irq *entries =
                (const struct arch_global_irq *)entry->space->priv;

        return (gsi_id_t)((size_t)(entry - entries)
                          + entry->space->global_irq_id_base);
}

static void ioapic_space_entry_init(struct arch_global_irq *entry,
                                    global_irq_id_t global_irq_id)
{
        struct ioapic *ioapic_entry;
        u16 pin;

        if (ioapic_get_entry_from_gsi(
                    (gsi_id_t)global_irq_id, &ioapic_entry, &pin)
            != REND_SUCCESS) {
                entry->ioapic = NULL;
                return;
        }
        entry->ioapic = ioapic_entry;
}

static error_t ioapic_space_enable(struct arch_global_irq *entry,
                                   enum irq_trigger trigger,
                                   enum irq_polarity polarity)
{
        u32 trigger_mode;
        u32 pin_polarity;
        gsi_id_t gsi;
        error_t e;

        if (!entry || !entry->ioapic || !entry->space || !entry->space->priv)
                return -E_RENDEZVOS;
        if (entry->local_irq_id > (local_irq_id_t)0xFFu
            || entry->cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        gsi = ioapic_space_entry_gsi(entry);
        trigger_mode = (trigger == IRQ_TRIGGER_LEVEL) ?
                               IOAPIC_TRIGGER_MODE_LEVEL :
                               IOAPIC_TRIGGER_MODE_EDGE;
        pin_polarity = (polarity == IRQ_POLARITY_ACTIVE_LOW) ?
                               IOAPIC_PIN_POLARITY_LOW :
                               IOAPIC_PIN_POLARITY_HIGH;
        e = ioapic_configure_rte(gsi,
                                 (u8)entry->local_irq_id,
                                 (u8)entry->cpu_id,
                                 IOAPIC_DELIVERY_MODE_FIXED,
                                 IOAPIC_DEST_PHYSICAL,
                                 trigger_mode,
                                 pin_polarity);
        if (e != REND_SUCCESS)
                return e;
        return ioapic_change_mask(gsi, false);
}

static error_t ioapic_space_disable(struct arch_global_irq *entry)
{
        if (!entry || !entry->ioapic || !entry->space || !entry->space->priv)
                return -E_RENDEZVOS;
        return ioapic_change_mask(ioapic_space_entry_gsi(entry), true);
}

static void ioapic_space_eoi(struct arch_global_irq *entry, u64 trap_info)
{
        local_irq_id_t irq_id = (local_irq_id_t)TRAP_ID(trap_info);

        if (entry && entry->ioapic)
                ioapic_eoi(entry->ioapic, (u8)irq_id);
}

static struct arch_global_irq *
ioapic_space_lookup_global_irq_id(struct irq_space *space,
                                  global_irq_id_t global_irq_id)
{
        struct arch_global_irq *entries = (struct arch_global_irq *)space->priv;

        if (!entries || !irq_space_covers(space, global_irq_id))
                return NULL;
        return &entries[global_irq_id - space->global_irq_id_base];
}

static void ioapic_space_destroy_priv(struct irq_space *space)
{
        struct allocator *cpu_kallocator;

        if (!space || !space->priv)
                return;
        cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator && cpu_kallocator->m_free)
                cpu_kallocator->m_free(cpu_kallocator, space->priv);
        space->priv = NULL;
}

error_t irq_space_ioapic_register(void)
{
        if (!ioapics || !nr_ioapics)
                return REND_SUCCESS;

        size_t nr_gsi = ioapic_nr_gsi();
        if (!nr_gsi) {
                print("[ IOAPIC ] ERROR: GSI 0 with %lu not 0 ioapics\n",
                      (unsigned long)nr_ioapics);
                return -E_RENDEZVOS;
        }

        struct allocator *cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !cpu_kallocator->m_alloc
            || !cpu_kallocator->m_free)
                return -E_RENDEZVOS;

        struct irq_space *space =
                cpu_kallocator->m_alloc(cpu_kallocator, sizeof(*space));
        if (!space)
                return -E_RENDEZVOS;
        struct arch_global_irq *entries = cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(*entries) * nr_gsi);
        if (!entries) {
                cpu_kallocator->m_free(cpu_kallocator, space);
                return -E_RENDEZVOS;
        }

        irq_space_common_init(space);
        space->priv = entries;
        space->global_irq_id_base = 0;
        space->nr_global_irq_id = nr_gsi;
        space->lookup_global_irq_id = ioapic_space_lookup_global_irq_id;
        space->choose_local = arch_irq_space_choose_local;
        space->bind_local = arch_irq_space_bind_local;
        space->enable = ioapic_space_enable;
        space->disable = ioapic_space_disable;
        space->eoi = ioapic_space_eoi;
        space->destroy_priv = ioapic_space_destroy_priv;

        for (size_t i = 0; i < nr_gsi; i++) {
                global_irq_id_t global_irq_id = (global_irq_id_t)i;

                irq_global_entry_common_init(&entries[i], space);
                ioapic_space_entry_init(&entries[i], global_irq_id);
        }

        error_t e = irq_space_register(space);
        if (e != REND_SUCCESS) {
                cpu_kallocator->m_free(cpu_kallocator, entries);
                cpu_kallocator->m_free(cpu_kallocator, space);
        }
        return e;
}
