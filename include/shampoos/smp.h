#ifndef _SHAMPOOS_SMP_H_
#define _SHAMPOOS_SMP_H_

#ifdef _AARCH64_
#include <arch/aarch64/smp.h>
#elif defined _X86_64_
#include <arch/x86_64/smp.h>
#else
#include <arch/x86_64/smp.h>
#endif
void start_smp(void);

#endif