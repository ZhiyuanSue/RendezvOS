#ifndef _SHAMPOOS_TIME_
#define _SHAMPOOS_TIME_
#include <common/types.h>

#ifdef _AARCH64_
#include <arch/aarch64/time.h>
#elif defined _LOONGARCH_

#elif defined _RISCV64_

#elif defined _X86_64_
#include <arch/x86_64/time.h>
#else /*for default config is x86_64*/
#include <arch/x86_64/time.h>
#endif

#define SYS_TIME_MS_PER_INT 10
void ndelay(u64 ns);
void udelay(u64 us);
void mdelay(u64 ms);
#endif