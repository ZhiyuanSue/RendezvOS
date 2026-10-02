#ifndef _RENDEZVOS_TASK_EBR_H_
#define _RENDEZVOS_TASK_EBR_H_

#include <common/refcount.h>

/**
 * @file ebr.h
 * @brief Minimal epoch-based reclamation for lock-free readers.
 *
 * Readers that may observe retired objects wrap the critical section with
 * @c ebr_enter / @c ebr_exit. Last-ref free paths that may race with those
 * readers call @c ebr_retire_ref instead of freeing immediately.
 *
 * Reclaim runs against **this CPU's** retire table only, and only once no
 * active reader can still hold a pointer from the retire epoch.
 * @c ebr_try_reclaim may also be invoked opportunistically.
 */

/**
 * @brief Build toggle: watermark accounting (peak / cross-level updates).
 *
 * Default off. Define to 1 (or uncomment below) to enable.
 */
/* #define EBR_ENABLE_WATERMARK 1 */
#ifndef EBR_ENABLE_WATERMARK
#define EBR_ENABLE_WATERMARK 0
#endif

/**
 * @brief Build toggle: watermark logging.
 *
 * Default off. Set to 0 (or redefine in build flags) to disable when watermark
 * accounting is on.
 */
#ifndef EBR_ENABLE_WATERMARK_LOG
#define EBR_ENABLE_WATERMARK_LOG 0
#endif

/**
 * @brief Per-CPU retire-table capacity (slots).
 *
 * Overflow leaks the node and still returns success (never free immediately).
 */
#ifndef EBR_RETIRE_SLOTS
#define EBR_RETIRE_SLOTS 512
#endif

/*
 * Minimal epoch-based reclamation (EBR) for lock-free queue users.
 *
 * Mapping to this codebase:
 * - ms_queue operations call ebr_enter/ebr_exit around pointer traversal.
 * - Free callbacks that may race with lock-free readers do not free directly.
 *   They call ebr_retire_ref(), which defers actual free_func() until all CPUs
 *   have passed a safe epoch.
 *
 * Safety intuition:
 * - A CPU in ebr_enter() publishes "I may dereference queue pointers from
 *   epoch E".
 * - retire records are tagged with retire_epoch R.
 * - reclaim is allowed only when every active CPU is in epoch > R.
 */
void ebr_enter(void);

/**
 * @brief Leave a nested read critical section on this CPU.
 */
void ebr_exit(void);

/**
 * @brief Reclaim this CPU's retire slots that are past the safe epoch.
 *
 * Does not scan other CPUs' tables. Safe to call anytime.
 */
void ebr_try_reclaim(void);

/**
 * @brief Defer @p free_func(@p ref) until no active reader can hold a pointer
 *        from the current epoch.
 *
 * Records the retire on this CPU's table (capacity @c EBR_RETIRE_SLOTS).
 * May try reclaim before/after insert.
 *
 * @return @c REND_SUCCESS on slot insert or on overflow leak; @c -E_IN_PARAM
 *         if @p ref or @p free_func is NULL.
 * @note Overflow: if the table stays full after reclaim attempts, the node
 *       is leaked (logged a few times) and success is still returned —
 *       never free immediately (UAF race). Raise @c EBR_RETIRE_SLOTS or
 *       reduce churn.
 */
error_t ebr_retire_ref(ref_count_t* ref, error_t (*free_func)(ref_count_t*));

/**
 * @brief Print per-CPU retire/reclaim/overflow counters (debug).
 */
void ebr_dump_stats(void);

#endif
