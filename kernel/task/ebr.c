#include <rendezvos/task/ebr.h>
#include <rendezvos/smp/percpu.h>
#include <common/atomic.h>
#include <modules/log/log.h>

extern int NR_CPU;

typedef struct {
        ref_count_t* ref;
        void (*free_func)(ref_count_t*);
        u64 retire_epoch;
        bool used;
} ebr_retired_rec_t;

DEFINE_PER_CPU(atomic64_t, ebr_active_flag);
DEFINE_PER_CPU(atomic64_t, ebr_local_epoch);
DEFINE_PER_CPU(atomic64_t, ebr_nesting_depth);
DEFINE_PER_CPU(ebr_retired_rec_t[EBR_RETIRE_SLOTS], ebr_retired_recs);
DEFINE_PER_CPU(atomic64_t, ebr_retired_count);
DEFINE_PER_CPU(atomic64_t, ebr_retired_peak);
DEFINE_PER_CPU(atomic64_t, ebr_retire_ops);
DEFINE_PER_CPU(atomic64_t, ebr_reclaim_ops);
DEFINE_PER_CPU(atomic64_t, ebr_overflow_ops);
DEFINE_PER_CPU(atomic64_t, ebr_wm_cross_mask);

static atomic64_t ebr_global_epoch;
static atomic64_t ebr_inited;
static atomic64_t ebr_overflow_log_cnt;
static atomic64_t ebr_bad_free_func_log_cnt;

#if EBR_ENABLE_WATERMARK
static const u32 ebr_wm_levels[] = {
        1,    2,    4,    8,    12,   16,   24,   32,   48,   64,
        96,   128,  192,  256,  384,  512,  1024, 2048, 4096, 8192,
};
static const u32 ebr_wm_level_count =
        sizeof(ebr_wm_levels) / sizeof(ebr_wm_levels[0]);
#endif

static inline void ebr_ensure_init(void)
{
        /*
         * ebr_inited state machine:
         * 0 = uninitialized, 1 = initializing, 2 = ready.
         * Use 3-state init to avoid readers observing "inited=1" before global
         * fields are actually initialized.
         */
        u64 st = atomic64_load((volatile const u64*)&ebr_inited.counter);
        if (st == 2)
                return;

        if (st == 0
            && atomic64_cas((volatile u64*)&ebr_inited.counter, 0, 1) == 0) {
                atomic64_init(&ebr_global_epoch, 1);
                atomic64_init(&ebr_overflow_log_cnt, 0);
                atomic64_store((volatile u64*)&ebr_inited.counter, 2);
                return;
        }

        while (atomic64_load((volatile const u64*)&ebr_inited.counter) != 2) {
                /* spin until initializer publishes ready */
        }
}

static inline void ebr_update_watermark(u32 live)
{
#if EBR_ENABLE_WATERMARK
        i64 old_peak = atomic64_load(
                (volatile const u64*)&percpu(ebr_retired_peak).counter);
        while ((u64)old_peak < (u64)live) {
                if (atomic64_cas((volatile u64*)&percpu(ebr_retired_peak).counter,
                                 (u64)old_peak,
                                 (u64)live)
                    == (u64)old_peak) {
                        break;
                }
                old_peak = atomic64_load(
                        (volatile const u64*)&percpu(ebr_retired_peak).counter);
        }

#if EBR_ENABLE_WATERMARK_LOG
        u64 mask = atomic64_load(
                (volatile const u64*)&percpu(ebr_wm_cross_mask).counter);
        for (u32 i = 0; i < ebr_wm_level_count; i++) {
                if (ebr_wm_levels[i] > EBR_RETIRE_SLOTS)
                        continue;
                u64 bit = 1ULL << i;
                if ((mask & bit) || live < ebr_wm_levels[i])
                        continue;
                if (atomic64_cas(
                            (volatile u64*)&percpu(ebr_wm_cross_mask).counter,
                            mask,
                            mask | bit)
                    == mask) {
                        pr_info("[ebr] cpu=%u retire watermark crossed: %u (live=%u)\n",
                                (u32)percpu(cpu_number),
                                ebr_wm_levels[i],
                                live);
                        mask |= bit;
                } else {
                        mask = atomic64_load(
                                (volatile const u64*)&percpu(ebr_wm_cross_mask)
                                         .counter);
                }
        }
#endif
#else
        (void)live;
#endif
}

void ebr_enter(void)
{
        ebr_ensure_init();
        i64 old_depth = atomic64_fetch_inc(&percpu(ebr_nesting_depth));
        if (old_depth == 0) {
                u64 ge = atomic64_load(
                        (volatile const u64*)&ebr_global_epoch.counter);
                atomic64_store((volatile u64*)&percpu(ebr_local_epoch).counter,
                               ge);
                atomic64_store((volatile u64*)&percpu(ebr_active_flag).counter,
                               1);
        }
}

void ebr_exit(void)
{
        i64 old_depth = atomic64_fetch_dec(&percpu(ebr_nesting_depth));
        if (old_depth == 1) {
                atomic64_store((volatile u64*)&percpu(ebr_active_flag).counter,
                               0);
                /* Advance epoch on every quiescent transition. */
                atomic64_inc(&ebr_global_epoch);
                /* Opportunistic reclaim on quiescent transition. */
                ebr_try_reclaim();
        }
}

static u64 ebr_compute_safe_epoch(void)
{
        u64 safe = atomic64_load((volatile const u64*)&ebr_global_epoch.counter);
        int ncpu = NR_CPU;
        if (ncpu <= 0)
                ncpu = 1;
        for (int i = 0; i < ncpu; i++) {
                if (atomic64_load((volatile const u64*)&per_cpu(ebr_active_flag, i)
                                                           .counter)
                    == 0)
                        continue;
                u64 le = atomic64_load(
                        (volatile const u64*)&per_cpu(ebr_local_epoch, i)
                                .counter);
                if (le < safe)
                        safe = le;
        }
        return safe;
}

void ebr_try_reclaim(void)
{
        u64 safe_epoch = ebr_compute_safe_epoch();
        ebr_retired_rec_t* recs = percpu(ebr_retired_recs);
        i64 live = atomic64_load(
                (volatile const u64*)&percpu(ebr_retired_count).counter);
        if (live <= 0)
                return;
        for (u32 i = 0; i < EBR_RETIRE_SLOTS; i++) {
                if (!recs[i].used)
                        continue;
                if (recs[i].retire_epoch >= safe_epoch)
                        continue;
                ref_count_t* ref = recs[i].ref;
                void (*free_func)(ref_count_t*) = recs[i].free_func;

                if (ref && free_func
                    && ((u64)(uintptr_t)free_func) < 0x1000) {
                        if (atomic64_fetch_inc(&ebr_bad_free_func_log_cnt) < 5) {
                                pr_error("[ebr] bad free_func ptr slot=%x cpu=%x ref_hi=%x ref_lo=%x fn_hi=%x fn_lo=%x\n",
                                         i,
                                         (u32)percpu(cpu_number),
                                         (u32)(((u64)(uintptr_t)ref >> 32)
                                               & 0xffffffff),
                                         (u32)((u64)(uintptr_t)ref & 0xffffffff),
                                         (u32)(((u64)(uintptr_t)free_func >> 32)
                                               & 0xffffffff),
                                         (u32)((u64)(uintptr_t)free_func
                                               & 0xffffffff));
                        }
                }
                recs[i].used = false;
                recs[i].ref = NULL;
                recs[i].free_func = NULL;
                recs[i].retire_epoch = 0;
                atomic64_dec(&percpu(ebr_retired_count));
                if (ref && free_func)
                        free_func(ref);
                atomic64_inc(&percpu(ebr_reclaim_ops));
        }
}

void ebr_retire_ref(ref_count_t* ref, void (*free_func)(ref_count_t*))
{
        if (!ref || !free_func)
                return;
        ebr_ensure_init();

        /*
         * Retire into current epoch. Global epoch is advanced by quiescent
         * transitions in ebr_exit(), which better matches EBR semantics.
         */
        u64 retire_epoch =
                atomic64_load((volatile const u64*)&ebr_global_epoch.counter);

        ebr_retired_rec_t* recs = percpu(ebr_retired_recs);
        /*
         * First reclaim pass before occupying a slot. Under heavy churn this
         * reduces pressure and prevents avoidable overflow.
         */
        ebr_try_reclaim();
        for (u32 i = 0; i < EBR_RETIRE_SLOTS; i++) {
                if (recs[i].used)
                        continue;
                recs[i].used = true;
                recs[i].ref = ref;
                recs[i].free_func = free_func;
                recs[i].retire_epoch = retire_epoch;
                atomic64_inc(&percpu(ebr_retired_count));
                atomic64_inc(&percpu(ebr_retire_ops));
                ebr_update_watermark((u32)atomic64_load(
                        (volatile const u64*)&percpu(ebr_retired_count).counter));
                ebr_try_reclaim();
                return;
        }

        /* Second chance after a reclaim attempt. */
        ebr_try_reclaim();
        for (u32 i = 0; i < EBR_RETIRE_SLOTS; i++) {
                if (recs[i].used)
                        continue;
                recs[i].used = true;
                recs[i].ref = ref;
                recs[i].free_func = free_func;
                recs[i].retire_epoch = retire_epoch;
                atomic64_inc(&percpu(ebr_retired_count));
                atomic64_inc(&percpu(ebr_retire_ops));
                ebr_update_watermark((u32)atomic64_load(
                        (volatile const u64*)&percpu(ebr_retired_count).counter));
                ebr_try_reclaim();
                return;
        }

        /*
         * Retire buffer overflow: keep system safe by leaking this node instead
         * of immediate free (which can reintroduce UAF races). Log once-ish.
         */
        atomic64_inc(&percpu(ebr_overflow_ops));
        if (atomic64_fetch_inc(&ebr_overflow_log_cnt) < 8) {
                u64 live = (u64)atomic64_load(
                        (volatile const u64*)&percpu(ebr_retired_count).counter);
                pr_error("[ebr] retire overflow cpu=%u retired_live=%x leaking one node "
                         "(raise EBR_RETIRE_SLOTS or fix churn)\n",
                         (u32)percpu(cpu_number),
                         (u32)(live & 0xffffffff));
        }
        ebr_try_reclaim();
}

void ebr_dump_stats(void)
{
        ebr_ensure_init();
        int ncpu = NR_CPU;
        if (ncpu <= 0)
                ncpu = 1;
        pr_info("[ebr] ===== stats begin ===== slots=%u =====\n",
                (u32)EBR_RETIRE_SLOTS);
        for (int i = 0; i < ncpu; i++) {
                u64 live = atomic64_load(
                        (volatile const u64*)&per_cpu(ebr_retired_count, i)
                                .counter);
                u64 peak = atomic64_load(
                        (volatile const u64*)&per_cpu(ebr_retired_peak, i)
                                .counter);
                u64 retire_ops = atomic64_load(
                        (volatile const u64*)&per_cpu(ebr_retire_ops, i)
                                .counter);
                u64 reclaim_ops = atomic64_load(
                        (volatile const u64*)&per_cpu(ebr_reclaim_ops, i)
                                .counter);
                u64 overflow_ops = atomic64_load(
                        (volatile const u64*)&per_cpu(ebr_overflow_ops, i)
                                .counter);
                pr_info("[ebr] cpu=%u live=%u peak=%u retire=%u reclaim=%u overflow=%u\n",
                        i,
                        live,
                        peak,
                        retire_ops,
                        reclaim_ops,
                        overflow_ops);
        }
        pr_info("[ebr] ===== stats end =====\n");
}
