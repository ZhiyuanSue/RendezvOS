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

void start_arch (uintptr_t magic, uintptr_t addr);
void parse_device(uintptr_t addr);
void interrupt_init();
void power_off(int error_code);
void cpu_idle();
#endif