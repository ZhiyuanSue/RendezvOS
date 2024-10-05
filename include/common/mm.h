#ifndef _SHAMPOOS_MM_H_
#define _SHAMPOOS_MM_H_
#include <common/stdbool.h>
#define PAGE_SIZE        0x1000
#define MIDDLE_PAGE_SIZE 0x200000
#define MIDDLE_PAGES     0x200
#define HUGE_PAGE_SIZE   0x40000000

#define KiloBytes 0x400
#define MegaBytes 0x100000
#define GigaBytes 0x40000000

#define ROUND_UP(x, align)   ((x + (align - 1)) & ~(align - 1))
#define ROUND_DOWN(x, align) (x & ~(align - 1))

#define ALIGNED(x, align) ((x & (align - 1)) == 0)

#define mask_9_bit     0x1ff
#define L0_INDEX(addr) ((addr >> 39) & mask_9_bit)
#define L1_INDEX(addr) ((addr >> 30) & mask_9_bit)
#define L2_INDEX(addr) ((addr >> 21) & mask_9_bit)
#define L3_INDEX(addr) ((addr >> 12) & mask_9_bit)

#define L0_entry_addr(entry)      ((entry.paddr) << 12)
#define L1_entry_huge_addr(entry) ((entry.paddr) << 30)
#define L1_entry_addr(entry)      ((entry.paddr) << 12)
#define L2_entry_huge_addr(entry) ((entry.paddr) << 21)
#define L2_entry_addr(entry)      ((entry.paddr) << 12)
#define L3_entry_addr(entry)      ((entry.paddr) << 12)

#define PPN(paddr) (paddr >> 12)
#define VPN(vaddr) (vaddr >> 12)
#define PADDR(ppn) (ppn << 12)
#define VADDR(vpn) (vpn << 12)

enum ENTRY_FLAGS {
        PAGE_ENTRY_NONE = 1 << 0,
        PAGE_ENTRY_VALID = 1 << 1,
        PAGE_ENTRY_WRITE = 1 << 2,
        PAGE_ENTRY_READ = 1 << 3,
        PAGE_ENTRY_EXEC = 1 << 4,
        PAGE_ENTRY_USER = 1 << 5,
        PAGE_ENTRY_DEVICE = 1 << 6,
        PAGE_ENTRY_UNCACHED = 1 << 7,
        PAGE_ENTRY_GLOBAL = 1 << 8,
        PAGE_ENTRY_HUGE = 1 << 9,

};
typedef u64 ENTRY_FLAGS_t;
typedef u64 ARCH_PFLAGS_t;
/*
    here we call the common flags entry flags
    and the flags in arch as page flags(pflags)
*/
static inline bool is_final_level_pt(int entry_level, ENTRY_FLAGS_t ENTRY_FLAGS)
{
        return ((ENTRY_FLAGS & PAGE_ENTRY_HUGE)
                && (entry_level == 1 || entry_level == 2))
               || (entry_level == 3);
}
#endif