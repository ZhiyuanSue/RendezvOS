#ifndef _SHAMPOOS_COMMON_H_
#define _SHAMPOOS_COMMON_H_
#define _K_KERNEL_
#include <shampoos/limits.h>
#include <shampoos/types.h>
#include <shampoos/stdio.h>
#include <shampoos/string.h>
#include <shampoos/error.h>
#include <shampoos/stdarg.h>
#include <shampoos/stddef.h>
#include <shampoos/list.h>

#ifdef _AARCH64_
#include <arch/aarch64/arch_common.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/arch_common.h>
#elif defined _RISCV64_
#include <arch/riscv64/arch_common.h>
#elif defined _X86_64_
#include <arch/x86_64/arch_common.h>
#endif

void start_arch (struct setup_info* arch_setup_info);
void parse_device(uintptr_t addr);
void interrupt_init();
void power_off(int error_code);
void cpu_idle();

#endif
