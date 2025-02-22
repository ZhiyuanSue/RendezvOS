#ifndef _SHAMPOOS_ARCH_TIME_
#define _SHAMPOOS_ARCH_TIME_
#include <common/types.h>
#include <arch/aarch64/sys_ctrl.h>
/*CNTFRQ_EL0*/

/*CNTV_CTL_EL0*/
#define CNTV_CTL_EL0_ENABLE  (0x1)
#define CNTV_CTL_EL0_IMASK   (0x1 << 1)
#define CNTV_CTL_EL0_ISTATUS (0x1 << 2)

void arch_init_timer(void);
#endif