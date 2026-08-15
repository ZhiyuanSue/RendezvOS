/*
 * x86 user-TLB SMP flush via IPI (no PCID).
 * only CPUs in vs->tlb_cpu_mask (set/cleared in schedule).
 */
#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/sync/tlb.h>
#include <common/atomic.h>
#include <common/dsa/bitmap.h>
#include <common/stdbool.h>
#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/tlb_cpu_mask.h>
#include <rendezvos/smp/ipi.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/smp/smp.h>

extern enum IRQ_type arch_irq_type;

struct smp_tlb_flush_msg {
        atomic64_t request_gen;
        atomic64_t done_gen;
        atomic64_t busy; /* whether an IPI is handling */
        vaddr flush_va; /* target vaddr */
        bool flush_all; /* true: flush the vspace, false: flush flush_va */
};

DEFINE_PER_CPU(struct smp_tlb_flush_msg, smp_tlb_flush_message);

static ipi_id_t smp_tlb_ipi_id;
static bool smp_tlb_ipi_ready;

static void smp_tlb_flush_ipi_fn(void)
{
        struct smp_tlb_flush_msg *msg = &percpu(smp_tlb_flush_message);
        i64 cur_gen;

        cur_gen = (i64)atomic64_load(
                (volatile const u64 *)&msg->request_gen.counter);

        if (msg->flush_all)
                arch_tlb_invalidate_all();
        else
                invlpg(msg->flush_va);

        atomic64_store((volatile u64 *)&msg->done_gen.counter, (u64)cur_gen);
        atomic64_store((volatile u64 *)&msg->busy.counter, 0);
}

static void smp_tlb_flush_send(cpu_id_t cpu_id, vaddr addr, bool flush_all)
{
        struct smp_tlb_flush_msg *msg = &per_cpu(smp_tlb_flush_message, cpu_id);
        i64 expect_gen;
        error_t e;

        while (atomic64_exchange((volatile u64 *)&msg->busy.counter, 1) != 0) {
                arch_cpu_relax();
        }

        msg->flush_all = flush_all;
        msg->flush_va = addr;
        expect_gen = atomic64_fetch_inc(&msg->request_gen) + 1;

        e = smp_ipi_send(cpu_id, smp_tlb_ipi_id);
        if (e != REND_SUCCESS) {
                atomic64_store((volatile u64 *)&msg->busy.counter, 0);
                return;
        }

        while (atomic64_load((volatile const u64 *)&msg->done_gen.counter)
               < (u64)expect_gen) {
                arch_cpu_relax();
        }
}

static void smp_tlb_flush_remote(vaddr addr, bool single_page,
                                 const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        cpu_id_t local_cpu = percpu(cpu_number);

        if (single_page)
                invlpg(addr);
        else
                arch_tlb_invalidate_all();

        if (!cpu_mask || !smp_tlb_ipi_ready)
                return;
        if (arch_irq_type != xAPIC_IRQ && arch_irq_type != x2APIC_IRQ)
                return;

        for (cpu_id_t i = 0; i < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER; i++) {
                if (i == local_cpu)
                        continue;
                if (!BITMAP_OPS(vs_tlb_cpu_bitmap, test)(cpu_mask, (u32)i))
                        continue;
                if (!cpu_is_online(i))
                        continue;
                smp_tlb_flush_send(i, addr, !single_page);
        }
}

void arch_smp_flush_tlb_init(void)
{
        struct smp_tlb_flush_msg *msg = &percpu(smp_tlb_flush_message);

        if (arch_irq_type != xAPIC_IRQ && arch_irq_type != x2APIC_IRQ)
                return;
        atomic64_init(&msg->request_gen, 0);
        atomic64_init(&msg->done_gen, 0);
        atomic64_init(&msg->busy, 0);
        msg->flush_all = false;
        msg->flush_va = 0;

        if (!smp_tlb_ipi_ready) {
                if (smp_ipi_register(&smp_tlb_ipi_id, smp_tlb_flush_ipi_fn)
                    != REND_SUCCESS)
                        return;
                smp_tlb_ipi_ready = true;
        }
}

void arch_smp_flush_page_tlb(vaddr addr, const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        smp_tlb_flush_remote(addr, true, cpu_mask);
}

void arch_smp_flush_all_tlb(const vs_tlb_cpu_bitmap_t *cpu_mask)
{
        smp_tlb_flush_remote(0, false, cpu_mask);
}
