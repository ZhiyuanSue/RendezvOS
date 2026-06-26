#ifndef _RENDEZVOS_TIME_
#define _RENDEZVOS_TIME_
#include <common/types.h>
#include <common/stdbool.h>
#include <rendezvos/error.h>

#ifdef _AARCH64_
#include <arch/aarch64/time.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/time.h>
#else

#endif

#include <common/dsa/rb_tree.h>
#include <common/dsa/list.h>
#include "rendezvos/trap/trap.h"

#include <rendezvos/ipc/port.h>

// Timer type enum
enum timer_type {
        TIMER_TYPE_PERIODIC,
        TIMER_TYPE_ONE_SHOT,
        TIMER_TYPE_X86_TSC_DDL,
};

extern volatile i64 jeffies;
extern u32 timer_irq_num;
/*in rendezvos we only use 64 bit time cnt*/
#define time_after(a, b)     ((i64)b - (i64)a < 0)
#define time_after_eq(a, b)  ((i64)b - (i64)a <= 0)
#define time_before(a, b)    time_after(b, a)
#define time_before_eq(a, b) time_after_eq(b, a)

typedef u64 tick_t;

/*
 * Per-CPU timer queue (rb-tree + same_expired_list). Call add/change/del on the
 * CPU where the event should fire; IRQ handler drains that CPU's queue.
 *
 * same_expired_list: rb-tree head uses it as list anchor; equal-expiry events
 * hang here and are not separate rb nodes.
 */
typedef struct rendezvos_timer_event {
        struct rb_node node;
        struct list_entry same_expired_list;
        tick_t expired;
        /* Non-zero: periodic re-arm in IRQ (e.g. heartbeat). Zero: one-shot. */
        u64 periodic_gap;
        /* One-shot only: delivery port (model B ref from init until fini/expire). */
        Message_Port_t* wait_port;
        /* One-shot kmsg payload; waiter validates to ignore stale delivery. */
        u64 delivery_token;
} rendezvos_timer_event;

/**
 * @brief Reset @p event and bind delivery parameters (model B port ref).
 *
 * Validates @p periodic_gap / @p wait_port, then memset + INIT_LIST_HEAD and
 * writes new fields. Does not read prior @p event contents (safe on garbage /
 * stack storage). Caller must not init an event that is still in the timer
 * queue or still holds a port ref — disarm with @p del / @p fini first.
 *
 * One-shot (@p periodic_gap == 0): @p wait_port is required; holds one port ref
 * via ref_get_not_zero until fini or IRQ expire teardown. @p delivery_token is
 * sent in KMSG_OP_CORE_TIMER_EXPIRE / KMSG_OP_CORE_TIMER_CANCEL.
 *
 * Periodic (@p periodic_gap != 0): @p wait_port must be NULL; @p delivery_token
 * is ignored. Do not call fini on periodic events (use del only).
 *
 * Typical sequence: init → add(expires_at) → … → del / cancel / fini (one-shot).
 *
 * @return REND_SUCCESS; -E_IN_PARAM if @p event is NULL or args invalid;
 *         -E_REND_IPC if one-shot port ref_get fails (@p event unchanged).
 */
error_t rendezvos_timer_event_init(rendezvos_timer_event* event, u64 periodic_gap,
                                   Message_Port_t* wait_port, u64 delivery_token);
/**
 * @brief One-shot teardown: disarm if queued, release init's port ref, clear
 *        wait_port.
 *
 * Do not call on periodic events (e.g. per-CPU heartbeat); use del alone.
 * IRQ expire path calls this after KMSG_OP_CORE_TIMER_EXPIRE delivery
 * (delivery failure still finis — one-shot lifetime ends at expiry).
 *
 * @return REND_SUCCESS; -E_IN_PARAM if @p event is periodic.
 */
error_t rendezvos_timer_event_fini(rendezvos_timer_event* event);
/**
 * @brief Disarm a one-shot if queued, then deliver KMSG_OP_CORE_TIMER_CANCEL via
 *        ipc_system_try_deliver(..., use_system_proxy=false).
 *
 * Does not fini and does not release the port ref; caller may fini after cancel
 * if the timer object is no longer needed.
 *
 * @return IPC/timer error from delivery; -E_IN_PARAM if @p event is NULL,
 *         periodic, or has no wait_port.
 */
error_t rendezvos_timer_event_cancel(rendezvos_timer_event* event);
/**
 * @brief Insert @p event into the current CPU's timer queue at @p expires_at
 *        (arch timer count, same domain as arch_timer_read()).
 *
 * @p event must be initialized and not already linked.
 */
error_t rendezvos_timer_event_add(rendezvos_timer_event *event,
                                  tick_t expires_at);
/**
 * @brief Reschedule: del then add at @p expires_at.
 */
error_t rendezvos_timer_event_change(rendezvos_timer_event *event,
                                     tick_t expires_at);
/**
 * @brief Unlink from the per-CPU timer queue only; does not release port ref
 *        or clear wait_port.
 */
error_t rendezvos_timer_event_del(rendezvos_timer_event *event);
/** @brief True if @p event is linked in the current CPU's timer queue. */
bool rendezvos_timer_event_exist(const rendezvos_timer_event *event);
/*arch interfaces*/
u64 arch_init_timer(bool is_bsp);
void arch_reset_timer(u64 next_event_gap);
tick_t arch_timer_read(void);
tick_t arch_timer_get_hz(void);

/*public interfaces*/
void rendezvos_time_init(void);
void rendezvos_do_time_irq(struct trap_frame *tf);
tick_t rendezvos_time_now(void);

/*count<-->us/ms*/
u64 rendezvos_time_count_to_us(tick_t count);
u64 rendezvos_time_count_to_ms(tick_t count);
tick_t rendezvos_time_us_to_count(u64 us);
tick_t rendezvos_time_ms_to_count(u64 ms);

/*constant*/
#define SYS_TIME_MS_PER_INT 10
#define INT_PER_SECOND      (1000 / SYS_TIME_MS_PER_INT)
#define UDELAY_MUL \
        (2147ULL * INT_PER_SECOND + 483648ULL * INT_PER_SECOND / 1000000)
#define UDELAY_SHIFT 31
#define UDELAY_MAX   2000

/*loop delay*/
void udelay(u64 us);
void mdelay(u64 ms);
#endif