#ifndef _SHAMPOOS_TIME_
#define _SHAMPOOS_TIME_
#include <common/types.h>

#ifdef _AARCH64_
#include <arch/aarch64/time.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/time.h>
#else

#endif

#include "trap.h"

extern volatile i64 jeffies;
extern u32 timer_irq_num;
/*in shampoos we only use 64 bit time cnt*/
#define time_after(a, b)     ((i64)b - (i64)a < 0)
#define time_after_eq(a, b)  ((i64)b - (i64)a <= 0)
#define time_before(a, b)    time_after(b, a)
#define time_before_eq(a, b) time_after_eq(b, a)

typedef u64 tick_t;
void shampoos_time_init(void);
void arch_reset_timer(void);
void shampoos_do_time_irq(struct trap_frame *tf);
#define SYS_TIME_MS_PER_INT 10
#define INT_PER_SECOND      (1000 / SYS_TIME_MS_PER_INT)
#define UDELAY_MUL \
        (2147ULL * INT_PER_SECOND + 483648ULL * INT_PER_SECOND / 1000000)
#define UDELAY_SHIFT 31
#define UDELAY_MAX   2000
void udelay(u64 us);
void mdelay(u64 ms);
#endif