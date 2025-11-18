#ifndef _RENDEZVOS_COMMON_H_
#define _RENDEZVOS_COMMON_H_
#define _K_KERNEL_
#include <common/stdarg.h>
#include <common/stddef.h>
#include <common/types.h>
#include <common/spin.h>
#include <rendezvos/error.h>
#include <rendezvos/stdio.h>

#ifdef _AARCH64_
#include <arch/aarch64/arch_common.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/arch_common.h>
#elif defined _RISCV64_
#include <arch/riscv64/arch_common.h>
#elif defined _X86_64_
#include <arch/x86_64/arch_common.h>
#else /*for default config is x86_64*/
#include <arch/x86_64/arch_common.h>
#endif

#include <modules/modules.h>
#ifdef SMP
#include <rendezvos/smp/smp.h>
#endif
#define boot_stack_size 0x10000

void interrupt_init(void);
error_t virt_mm_init(int cpu_id, struct setup_info *arch_setup_info);
/*
        the main_init is used for the outer kernel module init,
        remember that the rendezvos is a kernel that only include some necessery
   part, and other part of the kernel should all put into the outer module like
   rendezvos_linux, so this part init is done here
*/
void main_init();

#endif
