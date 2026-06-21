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
 * same_expired_list: rb-tree head uses it as list anchor; equal-expiry events
 * hang here and are not separate rb nodes.
 */
typedef struct rendezvos_timer_event {
        struct rb_node node;
        struct list_entry same_expired_list;
        tick_t expired;
} rendezvos_timer_event;

void rendezvos_timer_event_init(rendezvos_timer_event *event);
error_t rendezvos_timer_event_add(rendezvos_timer_event *event,
                                  tick_t expires_at);
error_t rendezvos_timer_event_change(rendezvos_timer_event *event,
                                     tick_t expires_at);
error_t rendezvos_timer_event_del(rendezvos_timer_event *event);
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