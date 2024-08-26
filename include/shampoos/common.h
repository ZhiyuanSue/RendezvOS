#ifndef _SHAMPOOS_COMMON_H_
# define _SHAMPOOS_COMMON_H_
# define _K_KERNEL_
# include <common/stdarg.h>
# include <common/stddef.h>
# include <common/types.h>
# include <shampoos/error.h>
# include <shampoos/limits.h>
# include <shampoos/list.h>
# include <shampoos/mm/buddy_pmm.h>
# include <shampoos/mm/pmm.h>
# include <shampoos/stdio.h>

# ifdef _AARCH64_
#  include <arch/aarch64/arch_common.h>
# elif defined _LOONGARCH_
#  include <arch/loongarch/arch_common.h>
# elif defined _RISCV64_
#  include <arch/riscv64/arch_common.h>
# elif defined _X86_64_
#  include <arch/x86_64/arch_common.h>
# else /*for default config is x86_64*/
#  include <arch/x86_64/arch_common.h>
# endif
# include <modules/modules.h>
# ifdef SMP
#  include <shampoos/smp.h>
# endif

void	parse_device(uintptr_t addr);
void	interrupt_init(void);
void	cpu_idle(void);

#endif
