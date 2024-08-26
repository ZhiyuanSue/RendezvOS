#ifndef _SHAMPOOS_BIT_H_
# define _SHAMPOOS_BIT_H_
/*please check the number of bits,we do no promise of the op*/
# define set_bit(number, bit_num) (number | (1 << bit_num))
# define clear_bit(number, bit_num) (~((~number) | (1 << bit_num)))
# define set_mask(number, mask) (number | mask)
# define clear_mask(number, mask) (~((~number) | mask))
#endif