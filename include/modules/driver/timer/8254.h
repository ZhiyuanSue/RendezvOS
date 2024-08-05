#ifndef _SHAMPOOS_8254_H_
#define _SHAMPOOS_8254_H_

#define _8254_CTL_BCD_ (1 << 0)
#define _8254_CTL_M_MASK_ (0b111 << 1)
#define _8254_CTL_RW_MASK_ (0b11 << 4)
#define _8254_CTL_SC_MASK_ (0b11 << 6)

void init_8254();
#endif