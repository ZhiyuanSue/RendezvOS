#ifndef _SHAMPOOS_ARCH_TIME_
# define _SHAMPOOS_ARCH_TIME_
# ifdef _X86_64_
#  include <modules/driver/timer/8254.h>
void	init_timer(void);
# endif
#endif