#ifndef _SHAMPOOS_ARCH_PAGE_TABLE_H_
# define _SHAMPOOS_ARCH_PAGE_TABLE_H_
# include "page_table_def.h"
# include <common/types.h>
# define mask_9_bit 0x1ff
# define PML4(addr) ((addr >> 39) & mask_9_bit)
# define PDPT(addr) ((addr >> 30) & mask_9_bit)
# define PDT(addr) ((addr >> 21) & mask_9_bit)
# define PT(addr) ((addr >> 12) & mask_9_bit)

/*some bit of cr3*/
# define CR3_PWT (1 << 3)
# define CR3_PCD (1 << 4)
# define CR3_ADDR(addr, m) (((addr >> 12) << 12) & MAXPHYADDR_mask(m))
# define CR3_PCID(pcid) (pcid & ((1 << 12) - 1))

#endif
