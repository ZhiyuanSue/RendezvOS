#include <rendezvos/task/tcb.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/cas_lock.h>
#include <modules/log/log.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/task/ebr.h>
extern Thread_Base* init_thread_ptr;
extern Thread_Base* idle_thread_ptr;
DEFINE_PER_CPU(Task_Manager*, core_tm);
volatile bool is_print_sche_info;
Thread_Base* round_robin_schedule(Task_Manager* tm)
{
        if (!tm || !tm->current_thread)
                return NULL;
        struct list_entry* next = tm->current_thread->sched_thread_list.next;
        while (next == &(tm->sched_thread_list)
               || container_of(next, Thread_Base, sched_thread_list)->status
                          != thread_status_ready) {
                next = next->next;
        }
        return container_of(next, Thread_Base, sched_thread_list);
}
Task_Manager* new_task_manager(void)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        Task_Manager* tm = (Task_Manager*)(cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(Task_Manager)));
        choose_schedule(tm);
        lock_init_cas(&tm->sched_lock);
        INIT_LIST_HEAD(&(tm->sched_task_list));
        INIT_LIST_HEAD(&(tm->sched_thread_list));
        tm->owner_cpu = percpu(cpu_number);
        return tm;
}
void del_task_manager_structure(Task_Manager* tm)
{
        /*only delete the structure, the upper code should make sure the tm have
         * no resources*/
        if (!tm)
                return;
        percpu(kallocator)->m_free(percpu(kallocator), tm);
}
void choose_schedule(Task_Manager* tm)
{
        if (!tm)
                return;
        tm->scheduler = round_robin_schedule;
        is_print_sche_info = false;
}
void print_sche_info(Thread_Base* old, Thread_Base* new)
{
        if (!old || !new)
                return;
        if (!is_print_sche_info)
                return;
        if (old->name) {
                if (new->name) {
                        pr_info("[CPU %d SCHE INFO] old %s new %s\n",
                                percpu(cpu_number),
                                old->name,
                                new->name);
                } else {
                        pr_info("[CPU %d SCHED INFO] old %s new %lx\n",
                                percpu(cpu_number),
                                old->name,
                                new);
                }

        } else {
                if (new->name) {
                        pr_info("[CPU %d SCHED INFO] old %lx new %s\n",
                                percpu(cpu_number),
                                old,
                                new->name);
                } else {
                        pr_info("[CPU %d SCHED INFO] old %lx new %lx\n",
                                percpu(cpu_number),
                                old,
                                new);
                }
        }
}
void schedule(Task_Manager* tm)
{
        if (!tm)
                return;
        /*
         * Cross-CPU kfree queues are per-CPU: remote frees land on the owner
         * allocator’s MSQs. kalloc/kfree entry also drains, but a CPU that
         * rarely malloc/free (e.g. idle-heavy) would not process inbound work
         * without another hook—schedule runs on every context switch.
         */
        kalloc_process_cross_cpu_frees();
        ebr_try_reclaim();
        lock_cas(&tm->sched_lock);
        Thread_Base* curr = tm->current_thread;
        if (tm->scheduler)
                tm->current_thread = tm->scheduler(tm);
        if (!tm->current_thread || curr == tm->current_thread) {
                unlock_cas(&tm->sched_lock);
                return;
        }
        print_sche_info(curr, tm->current_thread);

        if ((tm->current_thread->flags) & THREAD_FLAG_USER) {
                /*
                if target thread is not a kernel thread,
                try to change the vspace
                */
                Tcb_Base* prev_tcb = curr->belong_tcb;
                Tcb_Base* next_tcb = tm->current_thread->belong_tcb;
                if (!prev_tcb || !next_tcb || !next_tcb->vs
                    || !vs_common_is_table_vspace(next_tcb->vs)) {
                        pr_error("[ Error ] unexpect thread config\n");
                        goto use_old_thread;
                }
                if (prev_tcb != next_tcb) {
                        /*
                         * we think every task have a vspace
                         */
                        VS_Common* old_vs = percpu(current_vspace);
                        VS_Common* new_vs = next_tcb->vs;
                        if (old_vs != new_vs) {
                                if (!ref_get_not_zero(&new_vs->refcount)) {
                                        pr_error(
                                                "[ Error ] ref_get_not_zero failed: new_vs=%p\n",
                                                (void*)new_vs);
                                        goto use_old_thread;
                                }

                                /* Mask the new vs's cpu mask*/
                                lock_cas(&new_vs->tlb_cpu_mask_lock);
                                vs_tlb_cpu_mask_set(new_vs, percpu(cpu_number));
                                unlock_cas(&new_vs->tlb_cpu_mask_lock);

                                /* Switch to new vspace first. */
                                arch_set_current_user_vspace_root_asid(
                                        new_vs->vspace_root_addr, new_vs->asid);
                                percpu(current_vspace) = new_vs;
                                /*If necessary, clean the old vs*/
                                if (vs_common_is_table_vspace(old_vs)
                                    && old_vs != &root_vspace) {
                                        arch_tlb_invalidate_vspace_page(
                                                old_vs->asid, 0);

                                        lock_cas(&old_vs->tlb_cpu_mask_lock);
                                        vs_tlb_cpu_mask_clear(
                                                old_vs, percpu(cpu_number));
                                        unlock_cas(&old_vs->tlb_cpu_mask_lock);
                                        ref_put(&old_vs->refcount,
                                                free_vspace_ref);
                                }
                        }
                }
        }
        /*
         * if before the schedule no status is set
         * set it to ready, otherwise using the set status
         */
        if ((curr->flags & THREAD_FLAG_EXIT_REQUESTED)
            && thread_get_status(curr) == thread_status_running) {
                /* Owner CPU proves switch-away; safe to reap. */
                thread_set_status_with_expect(
                        curr, thread_status_running, thread_status_zombie);
        } else {
                thread_set_status_with_expect(
                        curr, thread_status_running, thread_status_ready);
        }
        thread_set_status(tm->current_thread, thread_status_running);
        unlock_cas(&tm->sched_lock);
        switch_to(&(curr->ctx), &(tm->current_thread->ctx));
        return;
use_old_thread:
        tm->current_thread = curr;
        unlock_cas(&tm->sched_lock);
        return;
}
