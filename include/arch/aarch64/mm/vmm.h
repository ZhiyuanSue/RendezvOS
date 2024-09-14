#ifndef _SHAMPOOS_ARCH_VMM_H_
#define _SHAMPOOS_ARCH_VMM_H_
#include <common/types.h>

#define mask_9_bit     0x1ff
#define L0_INDEX(addr) ((addr >> 39) & mask_9_bit)
#define L1_INDEX(addr) ((addr >> 30) & mask_9_bit)
#define L2_INDEX(addr) ((addr >> 21) & mask_9_bit)
#define L3_INDEX(addr) ((addr >> 12) & mask_9_bit)

static inline paddr get_current_kernel_vspace_root()
{
        u64 ttbr1_tmp;
        __asm__("mrs %0, TTBR1_EL1\n" : "=r"(ttbr1_tmp) : :);
        return ttbr1_tmp;
}

// here we only consider the 4K paging
#define LOWER_BLOCK_ATTR   \
        u64 AttrIndex : 3; \
        u64 NS : 1;        \
        u64 AP : 2;        \
        u64 SH : 2;        \
        u64 AF : 1;        \
        u64 nG : 1
#define UPPER_BLOCK_ATTR    \
        u64 contiguous : 1; \
        u64 PXN : 1;        \
        u64 UXN : 1;        \
        u64 RES0 : 4;       \
        u64 RES1 : 5
union L0_entry {
        u64 entry;
        struct {
                u64 V : 1;
                u64 BOK : 1; // block or table
                u64 : 10;
                u64 paddr : 36;
                u64 : 11;
                u64 PXNTable : 1;
                u64 XNTable : 1;
                u64 APTable : 2;
                u64 NSTable : 1;
        };
};
union L1_entry_huge {
        u64 entry;
        struct {
                u64 V : 1;
                u64 BOK : 1;
                LOWER_BLOCK_ATTR;
                u64 : 18;
                u64 paddr : 18;
                u64 : 4;
                UPPER_BLOCK_ATTR;
        };
};
union L1_entry {
        u64 entry;
        struct {
                u64 V : 1;
                u64 BOK : 1; // block or table
                u64 : 10;
                u64 paddr : 36;
                u64 : 11;
                u64 PXNTable : 1;
                u64 XNTable : 1;
                u64 APTable : 2;
                u64 NSTable : 1;
        };
};
union L2_entry_huge {
        u64 entry;
        struct {
                u64 V : 1;
                u64 BOK : 1;
                LOWER_BLOCK_ATTR;
                // res0
                u64 : 9;
                u64 paddr : 27;
                // res0
                u64 : 4;
                UPPER_BLOCK_ATTR;
        };
};
union L2_entry {
        u64 entry;
        struct {
                u64 V : 1;
                u64 BOK : 1; // block or table
                u64 : 10;
                u64 paddr : 36;
                u64 : 11;
                u64 PXNTable : 1;
                u64 XNTable : 1;
                u64 APTable : 2;
                u64 NSTable : 1;
        };
};
union L3_entry {
        u64 entry;
        struct {
                u64 V : 1;
                u64 BOK : 1; // block or table
                u64 : 10;
                u64 paddr : 36;
                u64 : 4;
                UPPER_BLOCK_ATTR;
        };
};
void arch_set_L0_entry(paddr ppn, vaddr vpn, union L0_entry *pt_addr,
                       u64 flags);
void arch_set_L1_entry(paddr ppn, vaddr vpn, union L1_entry *pt_addr,
                       u64 flags);
void arch_set_L1_entry_huge(paddr ppn, vaddr vpn, union L1_entry_huge *pt_addr,
                            u64 flags);
void arch_set_L2_entry(paddr ppn, vaddr vpn, union L2_entry *pt_addr,
                       u64 flags);
void arch_set_L2_entry_huge(paddr ppn, vaddr vpn, union L2_entry_huge *pt_addr,
                            u64 flags);
void arch_set_L3_entry(paddr ppn, vaddr vpn, union L3_entry *pt_addr,
                       u64 flags);
#endif