#ifndef _SHAMPOOS_8254_H_
#define _SHAMPOOS_8254_H_

#include <common/types.h>
#define _8254_CTL_BCD_OFF_ (0)
#define _8254_CTL_BCD_     (1 << _8254_CTL_BCD_OFF_)

#define _8254_CTL_M_OFF_  (1)
#define _8254_CTL_M_MASK_ (0b111 << _8254_CTL_M_OFF_)

#define _8254_CTL_RW_OFF_  (4)
#define _8254_CTL_RW_MASK_ (0b11 << _8254_CTL_RW_OFF_)

#define _8254_CTL_SC_OFF_  (6)
#define _8254_CTL_SC_MASK_ (0b11 << _8254_CTL_SC_OFF_)

void init_8254_cyclical(int freq);
void init_8254_one_shot(u16 t);
void init_8254_read();
u16 read_8254_val();
void PIT_mdelay(int ms);
#define PIT_TICK_RATE 1193181
#endif