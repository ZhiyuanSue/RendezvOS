#include <arch/aarch64/gic/gic.h>
#include <arch/aarch64/trap/irq_space.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/trap/irq_space.h>

/* Sole SPI space for formula delivery / arch_irq_entry_from_local. */
static struct irq_space *gic_spi_space;

/* global_irq_id == SPI index (base 0): INTID from dense priv[] index. */
static u32 gic_spi_space_entry_intid(const struct arch_global_irq *entry)
{
        const struct arch_global_irq *entries =
                (const struct arch_global_irq *)entry->space->priv;

        return gic_spi_intid((u32)((size_t)(entry - entries)
                                   + entry->space->global_irq_id_base));
}

static error_t gic_spi_space_enable(struct arch_global_irq *entry,
                                    enum irq_trigger trigger,
                                    enum irq_polarity polarity)
{
        u32 intid;

        (void)polarity; /* GIC has no pin polarity like IOAPIC */

        if (!entry || !entry->space || !entry->space->priv || !gic.enable_spi)
                return -E_RENDEZVOS;
        intid = gic_spi_space_entry_intid(entry);
        if (!gic_is_spi(intid))
                return -E_IN_PARAM;
        return gic.enable_spi(intid, entry->cpu_id, trigger);
}

static error_t gic_spi_space_disable(struct arch_global_irq *entry)
{
        u32 intid;

        if (!entry || !entry->space || !entry->space->priv || !gic.disable_spi)
                return -E_RENDEZVOS;
        intid = gic_spi_space_entry_intid(entry);
        if (!gic_is_spi(intid))
                return -E_IN_PARAM;
        return gic.disable_spi(intid);
}

static struct arch_global_irq *
gic_spi_space_lookup_global_irq_id(struct irq_space *space,
                                   global_irq_id_t global_irq_id)
{
        struct arch_global_irq *entries =
                (struct arch_global_irq *)space->priv;

        if (!entries || !irq_space_covers(space, global_irq_id))
                return NULL;
        return &entries[global_irq_id - space->global_irq_id_base];
}

struct arch_global_irq *arch_irq_entry_from_local(cpu_id_t cpu_id,
                                                  local_irq_id_t local_irq_id)
{
        size_t hw_irq;
        global_irq_id_t global_irq_id;

        (void)cpu_id;
        if (!gic_spi_space)
                return NULL;
        hw_irq = AARCH64_TRAP_ID_TO_IRQ(local_irq_id);
        if (hw_irq < (size_t)GIC_SPI_START || hw_irq > (size_t)GIC_SPI_END)
                return NULL;
        global_irq_id = (global_irq_id_t)(hw_irq - (size_t)GIC_SPI_START);
        return gic_spi_space_lookup_global_irq_id(gic_spi_space, global_irq_id);
}

static local_irq_id_t gic_spi_space_choose_local(struct irq_space *space,
                                                 global_irq_id_t global_irq_id,
                                                 cpu_id_t cpu_id)
{
        size_t hw_irq;

        (void)space;
        (void)cpu_id; /* alloc already validated */
        hw_irq = (size_t)global_irq_id + (size_t)GIC_SPI_START;
        if (hw_irq > (size_t)GIC_SPI_END)
                return IRQ_LOCAL_ID_INVALID;
        return (local_irq_id_t)AARCH64_IRQ_TO_TRAP_ID(hw_irq);
}

static bool gic_spi_space_bind_local(struct irq_space *space, cpu_id_t cpu_id,
                                     local_irq_id_t local_irq_id,
                                     struct arch_global_irq *entry)
{
        (void)space;
        (void)cpu_id;
        (void)local_irq_id;
        (void)entry;
        /* Formula delivery; no per-CPU map to update. */
        return true;
}

static void gic_spi_space_destroy_priv(struct irq_space *space)
{
        struct allocator *cpu_kallocator;

        if (!space)
                return;
        if (gic_spi_space == space)
                gic_spi_space = NULL;
        if (!space->priv)
                return;
        cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator && cpu_kallocator->m_free)
                cpu_kallocator->m_free(cpu_kallocator, space->priv);
        space->priv = NULL;
}

error_t irq_space_gic_spi_register(size_t nr_spi)
{
        if (!nr_spi)
                return -E_IN_PARAM;

        struct allocator *cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !cpu_kallocator->m_alloc || !cpu_kallocator->m_free)
                return -E_RENDEZVOS;

        struct irq_space *space = cpu_kallocator->m_alloc(cpu_kallocator, sizeof(*space));
        if (!space)
                return -E_RENDEZVOS;
        struct arch_global_irq *entries = cpu_kallocator->m_alloc(cpu_kallocator, sizeof(*entries) * nr_spi);
        if (!entries) {
                cpu_kallocator->m_free(cpu_kallocator, space);
                return -E_RENDEZVOS;
        }

        irq_space_common_init(space);
        space->priv = entries;
        space->global_irq_id_base = 0;
        space->nr_global_irq_id = nr_spi;
        space->lookup_global_irq_id = gic_spi_space_lookup_global_irq_id;
        space->choose_local = gic_spi_space_choose_local;
        space->bind_local = gic_spi_space_bind_local;
        space->enable = gic_spi_space_enable;
        space->disable = gic_spi_space_disable;
        space->eoi = NULL;
        space->destroy_priv = gic_spi_space_destroy_priv;

        for (size_t i = 0; i < nr_spi; i++)
                irq_global_entry_common_init(&entries[i], space);

        error_t e = irq_space_register(space);
        if (e != REND_SUCCESS) {
                cpu_kallocator->m_free(cpu_kallocator, entries);
                cpu_kallocator->m_free(cpu_kallocator, space);
                return e;
        }
        gic_spi_space = space;
        return REND_SUCCESS;
}
