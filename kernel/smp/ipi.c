#include <common/atomic.h>
#include <common/stdbool.h>
#include <rendezvos/smp/ipi.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/trap/trap.h>

struct smp_ipi_slot {
        smp_ipi_fn_t fn;
        bool used;
};

static struct smp_ipi_slot smp_ipi_slots[RENDEZVOS_SMP_IPI_MAX];
static ipi_id_t smp_ipi_next;

DEFINE_PER_CPU(atomic64_t, smp_ipi_pending);

static void smp_ipi_dispatch(struct trap_frame *tf)
{
        volatile u64 *pending =
                (volatile u64 *)&percpu(smp_ipi_pending).counter;

        (void)tf;

        for (;;) {
                u64 pending_bits = atomic64_exchange(pending, 0);
                ipi_id_t ipi_id;

                if (!pending_bits)
                        return;
                for (ipi_id = 0; ipi_id < RENDEZVOS_SMP_IPI_MAX; ipi_id++) {
                        if (!(pending_bits & (1ull << ipi_id)))
                                continue;
                        if (smp_ipi_slots[ipi_id].used
                            && smp_ipi_slots[ipi_id].fn)
                                smp_ipi_slots[ipi_id].fn();
                }
        }
}

void smp_ipi_init(void)
{
        arch_smp_ipi_init(smp_ipi_dispatch);
}

error_t smp_ipi_register(ipi_id_t *ipi_id_out, smp_ipi_fn_t fn)
{
        ipi_id_t ipi_id;

        if (!ipi_id_out || !fn)
                return -E_IN_PARAM;
        if (smp_ipi_next >= RENDEZVOS_SMP_IPI_MAX)
                return -E_REND_OVERFLOW;

        ipi_id = smp_ipi_next++;
        smp_ipi_slots[ipi_id].fn = fn;
        smp_ipi_slots[ipi_id].used = true;
        *ipi_id_out = ipi_id;
        return REND_SUCCESS;
}

error_t smp_ipi_send(cpu_id_t cpu, ipi_id_t ipi_id)
{
        volatile u64 *pending;
        u64 old_pending, new_pending;
        error_t e;

        if (ipi_id >= RENDEZVOS_SMP_IPI_MAX || !smp_ipi_slots[ipi_id].used)
                return -E_IN_PARAM;
        if (!cpu_is_online(cpu))
                return -E_IN_PARAM;

        pending = (volatile u64 *)&per_cpu(smp_ipi_pending, cpu).counter;
        do {
                old_pending = atomic64_load(pending);
                new_pending = old_pending | (1ull << ipi_id);
                if (new_pending == old_pending)
                        break;
        } while (atomic64_cas(pending, old_pending, new_pending) != old_pending);

        e = arch_smp_ipi_send(cpu);
        if (e != REND_SUCCESS) {
                do {
                        old_pending = atomic64_load(pending);
                        new_pending = old_pending & ~(1ull << ipi_id);
                        if (new_pending == old_pending)
                                break;
                } while (atomic64_cas(pending, old_pending, new_pending)
                         != old_pending);
        }
        return e;
}
