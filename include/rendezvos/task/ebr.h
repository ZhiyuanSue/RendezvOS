#ifndef _RENDEZVOS_TASK_EBR_H_
#define _RENDEZVOS_TASK_EBR_H_

#include <common/refcount.h>

/*
 * Toggle watermark accounting (peak/cross-level updates).
 * Comment this line out (or set to 0) to disable watermark logic.
 */
/* #define EBR_ENABLE_WATERMARK 1 */
#ifndef EBR_ENABLE_WATERMARK
#define EBR_ENABLE_WATERMARK 0
#endif

/*
 * Toggle watermark logging.
 * Set to 0 (or comment out and redefine in build flags) to disable.
 */
#ifndef EBR_ENABLE_WATERMARK_LOG
#define EBR_ENABLE_WATERMARK_LOG 1
#endif

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
void ebr_exit(void);
void ebr_try_reclaim(void);
void ebr_retire_ref(ref_count_t* ref,
                    void (*free_func)(ref_count_t*));
void ebr_dump_stats(void);

#endif
