#ifndef _SHAMPOOS_ARCH_TIME_
#define _SHAMPOOS_ARCH_TIME_
#include <common/types.h>
void arch_init_timer(void);
void arch_udelay(u64 us);
#endif