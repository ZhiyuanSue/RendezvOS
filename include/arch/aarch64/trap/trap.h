#ifndef _SHAMPOOS_TRAP_H_
#define _SHAMPOOS_TRAP_H_

#define NR_IRQ 1019 /*in gic(v2) we can only use 0-1019*/
struct trap_frame {};
void init_interrupt(void);
#endif