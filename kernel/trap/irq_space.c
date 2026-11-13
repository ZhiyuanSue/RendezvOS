#include <modules/log/log.h>
#include <common/stddef.h>
#include <common/atomic.h>
#include <common/dsa/list.h>
#include <common/refcount.h>
#include <rendezvos/limits.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/trap/irq_space.h>
#include <rendezvos/common.h>

/*local part have no life cycle problem, for it's forever exist*/
DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

bool irq_trap_is_local(local_irq_id_t local_irq_id)
{
        return local_irq_id < (local_irq_id_t)NR_ARCH_LOCAL_IRQ;
}

struct local_irq *irq_local_find(local_irq_id_t local_irq_id, cpu_id_t cpu_id)
{
        if (!irq_trap_is_local(local_irq_id))
                return NULL;
        if (cpu_id == CPU_ID_INVALID)
                cpu_id = percpu(cpu_number);
        else if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return NULL;
        return &per_cpu(local_irq_vector[local_irq_id], cpu_id);
}

static LIST_HEAD(irq_space_list);
static spin_lock irq_space_list_lock;
DEFINE_PER_CPU(spin_lock_t, irq_space_mcs_node);

#define for_each_irq_space(space) \
        list_for_each_entry(space, &irq_space_list, node)

static inline void irq_space_list_lock_acquire(void)
{
        lock_mcs(&irq_space_list_lock, &percpu(irq_space_mcs_node));
}

static inline void irq_space_list_lock_release(void)
{
        unlock_mcs(&irq_space_list_lock, &percpu(irq_space_mcs_node));
}

error_t free_irq_space_ref(ref_count_t *ref_count_ptr)
{
        struct irq_space *space;
        struct allocator *cpu_kallocator;

        if (!ref_count_ptr)
                return -E_IN_PARAM;
        space = container_of(ref_count_ptr, struct irq_space, refcount);
        /* Only after unregister: CLOSED and off the registry list. */
        if (irq_space_life_load(space) != IRQ_SPACE_LIFE_CLOSED
            || !list_node_is_detached(&space->node)) {
                pr_error("[irq_space] free_ref not CLOSED/detached life=%ld\n",
                         irq_space_life_load(space));
                return -E_RENDEZVOS;
        }
        cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !cpu_kallocator->m_free)
                return -E_RENDEZVOS;
        if (space->destroy_priv)
                space->destroy_priv(space);
        cpu_kallocator->m_free(cpu_kallocator, space);
        return REND_SUCCESS;
}

error_t irq_space_register(struct irq_space *space)
{
        struct irq_space *s;
        error_t e = REND_SUCCESS;

        if (!space || !space->nr_global_irq_id || !space->lookup_global_irq_id
            || !space->choose_local || !space->bind_local || !space->enable
            || !space->disable)
                return -E_IN_PARAM;
        if (space->global_irq_id_base + space->nr_global_irq_id
            < space->global_irq_id_base)
                return -E_IN_PARAM;
        if (irq_space_life_load(space) != IRQ_SPACE_LIFE_ACTIVE)
                return -E_IN_PARAM;

        irq_space_list_lock_acquire();
        for_each_irq_space(s)
        {
                if (space->global_irq_id_base
                            < s->global_irq_id_base + s->nr_global_irq_id
                    && s->global_irq_id_base
                               < space->global_irq_id_base
                                         + space->nr_global_irq_id) {
                        e = -E_RENDEZVOS;
                        goto out_unlock;
                }
        }
        if (!irq_space_set_life_with_expect(
                    space, IRQ_SPACE_LIFE_ACTIVE, IRQ_SPACE_LIFE_REGISTERED)) {
                e = -E_RENDEZVOS;
                goto out_unlock;
        }
        /*
         * Transfer: creator's refcount 1 becomes the list's. No extra get.
         * Caller must not ref_put/free on success.
         */
        list_add_tail(&space->node, &irq_space_list);
out_unlock:
        irq_space_list_lock_release();
        return e;
}

error_t irq_space_unregister(struct irq_space *space)
{
        if (!space)
                return -E_IN_PARAM;

        irq_space_list_lock_acquire();
        if (irq_space_life_load(space) != IRQ_SPACE_LIFE_REGISTERED
            || list_node_is_detached(&space->node)) {
                irq_space_list_lock_release();
                return -E_IN_PARAM;
        }
        /*
         * CLOSING first (still linked): let new ops_begin/alloc fails.
         * Busy → restore REGISTERED, stay linked, AGAIN.
         * Success: unlink + CLOSED transfers that 1 to us, then ref_put frees.
         */
        if (!irq_space_set_life_with_expect(
                    space, IRQ_SPACE_LIFE_REGISTERED, IRQ_SPACE_LIFE_CLOSING)) {
                irq_space_list_lock_release();
                return -E_RENDEZVOS;
        }
        if (ref_count(&space->refcount) != 1
            || atomic64_load((volatile const u64 *)&space->nr_used.counter)
                       != 0) {
                (void)irq_space_set_life_with_expect(space,
                                                     IRQ_SPACE_LIFE_CLOSING,
                                                     IRQ_SPACE_LIFE_REGISTERED);
                irq_space_list_lock_release();
                return -E_REND_AGAIN;
        }
        list_del(&space->node);
        irq_space_list_lock_release();
        atomic64_store((volatile u64 *)&space->life.counter,
                       IRQ_SPACE_LIFE_CLOSED);
        return ref_put(&space->refcount, free_irq_space_ref);
}

static struct arch_global_irq *
irq_spaces_find_global_entry_locked(global_irq_id_t global_irq_id,
                                    struct irq_space **space_out)
{
        struct irq_space *space;

        if (space_out)
                *space_out = NULL;
        if (global_irq_id == IRQ_GLOBAL_ID_INVALID)
                return NULL;

        for_each_irq_space(space)
        {
                if (irq_space_life_load(space) != IRQ_SPACE_LIFE_REGISTERED
                    || !space->lookup_global_irq_id
                    || !irq_space_covers(space, global_irq_id))
                        continue;
                struct arch_global_irq *entry =
                        space->lookup_global_irq_id(space, global_irq_id);
                if (!entry)
                        continue;
                if (space_out)
                        *space_out = space;
                return entry;
        }
        return NULL;
}

error_t irq_global_find(local_irq_id_t local_irq_id, cpu_id_t cpu_id,
                        struct arch_global_irq **entry_out)
{
        if (!entry_out)
                return -E_IN_PARAM;
        *entry_out = NULL;
        if (local_irq_id == IRQ_LOCAL_ID_INVALID)
                return -E_IN_PARAM;
        if (cpu_id != CPU_ID_INVALID
            && cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        cpu_id_t target_cpu = (cpu_id == CPU_ID_INVALID) ? percpu(cpu_number) :
                                                           cpu_id;
        struct arch_global_irq *entry =
                arch_irq_entry_from_local(target_cpu, local_irq_id);
        if (!entry)
                return -E_REND_NOFOUND;
        if (!irq_atomic_attr_is_used(&entry->irq_attr)
            || entry->local_irq_id != local_irq_id
            || entry->cpu_id != target_cpu)
                return -E_REND_NOFOUND;

        *entry_out = entry;
        return REND_SUCCESS;
}

error_t irq_global_alloc(global_irq_id_t global_irq_id, cpu_id_t cpu_id,
                         local_irq_id_t *local_irq_id_out)
{
        struct irq_space *space = NULL;
        error_t e = REND_SUCCESS;

        if (!local_irq_id_out)
                return -E_IN_PARAM;
        if (cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                return -E_IN_PARAM;

        irq_space_list_lock_acquire();
        struct arch_global_irq *global_entry =
                irq_spaces_find_global_entry_locked(global_irq_id, &space);
        if (!global_entry || !space || !space->choose_local
            || !space->bind_local) {
                irq_space_list_lock_release();
                e = -E_IN_PARAM;
                goto out;
        }
        if (!irq_space_ops_begin(space)) {
                irq_space_list_lock_release();
                e = -E_REND_AGAIN;
                goto out;
        }
        irq_space_list_lock_release();

        if (!irq_space_covers(space, global_irq_id)
            || space->lookup_global_irq_id(space, global_irq_id)
                       != global_entry) {
                e = -E_REND_AGAIN;
                goto out_ops_end;
        }

        local_irq_id_t local_irq_id =
                space->choose_local(space, global_irq_id, cpu_id);
        if (local_irq_id == IRQ_LOCAL_ID_INVALID) {
                e = -E_REND_OVERFLOW;
                goto out_ops_end;
        }

        /* USED CAS: only one claimant; then this cpu_id owns the entry. */
        if (!irq_atomic_attr_try_set_used(&global_entry->irq_attr)) {
                e = -E_REND_AGAIN;
                goto out_ops_end;
        }
        atomic64_inc(&space->nr_used);

        if (irq_space_life_load(space) != IRQ_SPACE_LIFE_REGISTERED) {
                e = -E_REND_AGAIN;
                goto out_unclaim;
        }

        /* Publish ids before bind so find never sees a mapped half-init entry.
         */
        global_entry->local_irq_id = local_irq_id;
        global_entry->cpu_id = cpu_id;
        if (!space->bind_local(space, cpu_id, local_irq_id, global_entry)) {
                e = -E_REND_AGAIN;
                goto out_unbind;
        }

        *local_irq_id_out = local_irq_id;
        goto out_ops_end;
out_unbind:
        global_entry->local_irq_id = IRQ_LOCAL_ID_INVALID;
        global_entry->cpu_id = CPU_ID_INVALID;
out_unclaim:
        atomic64_dec(&space->nr_used);
        (void)irq_atomic_attr_try_set_unused(&global_entry->irq_attr);
out_ops_end:
        irq_space_ops_end(space);
out:
        return e;
}

error_t irq_global_free(global_irq_id_t global_irq_id)
{
        struct irq_space *space = NULL;
        error_t e = REND_SUCCESS;

        irq_space_list_lock_acquire();
        struct arch_global_irq *global_entry =
                irq_spaces_find_global_entry_locked(global_irq_id, &space);
        if (!global_entry || !space || !space->bind_local) {
                irq_space_list_lock_release();
                e = -E_IN_PARAM;
                goto out;
        }
        if (!irq_space_ops_begin(space)) {
                irq_space_list_lock_release();
                e = -E_REND_AGAIN;
                goto out;
        }
        irq_space_list_lock_release();

        if (global_entry->local_irq_id == IRQ_LOCAL_ID_INVALID
            || global_entry->cpu_id == CPU_ID_INVALID
            || global_entry->cpu_id >= (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER) {
                e = -E_REND_NOFOUND;
                goto out_ops_end;
        }
        /* Owner-CPU exclusive: no concurrent mutator, no entry-refcount drain.
         */
        if (percpu(cpu_number) != global_entry->cpu_id) {
                e = -E_IN_PARAM;
                goto out_ops_end;
        }
        if (!irq_atomic_attr_is_used(&global_entry->irq_attr)) {
                e = -E_REND_NOFOUND;
                goto out_ops_end;
        }

        cpu_id_t owner = global_entry->cpu_id;
        local_irq_id_t local_irq_id = global_entry->local_irq_id;
        (void)space->bind_local(space, owner, local_irq_id, NULL);
        global_entry->local_irq_id = IRQ_LOCAL_ID_INVALID;
        global_entry->cpu_id = CPU_ID_INVALID;
        /* Keep irq_handler across unclaim (migrate: free → alloc → enable). */
        if (!irq_atomic_attr_try_set_unused(&global_entry->irq_attr)) {
                /* Owner-only: should not fail; leave unbound and report. */
                e = -E_RENDEZVOS;
                goto out_ops_end;
        }
        atomic64_dec(&space->nr_used);
out_ops_end:
        irq_space_ops_end(space);
out:
        return e;
}
