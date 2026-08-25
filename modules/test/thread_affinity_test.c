#include <modules/test/test.h>
#include <rendezvos/task/thread.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/limits.h>
#include <common/types.h>

extern int NR_CPU;

/*
 * Ring create-time bind: CPU i creates a thread on CPU (i+1)%NR_CPU.
 * Probe records creator id into affinity_seen[ran_on].
 * check_result: affinity_seen[j] == (j + NR_CPU - 1) % NR_CPU.
 *
 * With check_result set, multi_cpu_test ignores per-CPU return values —
 * failures go through affinity_fail / affinity_seen.
 */
static volatile int affinity_seen[RENDEZVOS_MAX_CPU_NUMBER];
static volatile int affinity_fail;
static volatile bool affinity_cleared;

static void* affinity_probe_thread(void* arg)
{
        cpu_id_t creator = (cpu_id_t)(u64)arg;
        cpu_id_t here = percpu(cpu_number);

        if (here < (cpu_id_t)RENDEZVOS_MAX_CPU_NUMBER)
                affinity_seen[here] = (int)creator;
        return NULL;
}

int smp_thread_affinity_test(void)
{
        cpu_id_t me = percpu(cpu_number);
        cpu_id_t target;
        Task_Manager* tm;
        Thread_Base* probe = NULL;

        if (NR_CPU < 1 || me >= (cpu_id_t)NR_CPU)
                return REND_SUCCESS;

        if (percpu(cpu_number) == BSP_ID) {
                for (int i = 0; i < NR_CPU; i++)
                        affinity_seen[i] = -1;
                affinity_fail = 0;
                affinity_cleared = true;
        } else {
                while (!affinity_cleared)
                        arch_cpu_relax();
        }

        target = (me + 1) % (cpu_id_t)NR_CPU;
        tm = task_manager_for_cpu(target);
        if (!tm || tm->owner_cpu != target) {
                affinity_fail = 1;
                return REND_SUCCESS;
        }

        if (gen_thread_from_func(&probe,
                                 affinity_probe_thread,
                                 "affinity_probe",
                                 tm,
                                 (void*)(u64)me)
            != REND_SUCCESS) {
                affinity_fail = 1;
                return REND_SUCCESS;
        }
        if (!probe || thread_owner_cpu(probe) != target) {
                affinity_fail = 1;
                if (probe)
                        (void)delete_thread(probe);
                return REND_SUCCESS;
        }

        /*
         * Target (and every other CPU) must schedule so ring probes run.
         * Wait until the thread we created has run on @p target.
         */
        while (!affinity_fail && affinity_seen[target] != (int)me)
                schedule(percpu(core_tm));

        if (affinity_fail)
                return REND_SUCCESS;

        while (thread_get_status(probe) != thread_status_zombie)
                schedule(percpu(core_tm));

        if (delete_thread(probe) != REND_SUCCESS)
                affinity_fail = 1;

        return REND_SUCCESS;
}

bool smp_thread_affinity_check(void)
{
        int n = NR_CPU;

        affinity_cleared = false;

        if (affinity_fail)
                return false;
        if (n < 1)
                return true;

        for (int j = 0; j < n; j++) {
                int expect = (j + n - 1) % n;

                if (affinity_seen[j] != expect) {
                        pr_error(
                                "[affinity] cpu %d seen=%d expect creator %d\n",
                                j,
                                affinity_seen[j],
                                expect);
                        return false;
                }
        }
        return true;
}
