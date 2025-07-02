#ifndef _RENDEZVOS_COMMON_MM_H_
#define _RENDEZVOS_COMMON_MM_H_
#include <common/stdbool.h>
#include <common/types.h>
#include <common/align.h>
#define PAGE_SIZE        0x1000ULL
#define MIDDLE_PAGE_SIZE 0x200000ULL
#define MIDDLE_PAGES     0x200ULL
#define HUGE_PAGE_SIZE   0x40000000ULL

#define KiloBytes 0x400ULL
#define MegaBytes 0x100000ULL
#define GigaBytes 0x40000000ULL

#define mask_9_bit     0x1ffULL
#define L0_INDEX(addr) (((u64)addr >> 39) & mask_9_bit)
#define L1_INDEX(addr) (((u64)addr >> 30) & mask_9_bit)
#define L2_INDEX(addr) (((u64)addr >> 21) & mask_9_bit)
#define L3_INDEX(addr) (((u64)addr >> 12) & mask_9_bit)

#define L0_entry_addr(entry)      (((u64)(entry.paddr)) << 12)
#define L1_entry_huge_addr(entry) (((u64)(entry.paddr)) << 30)
#define L1_entry_addr(entry)      (((u64)(entry.paddr)) << 12)
#define L2_entry_huge_addr(entry) (((u64)(entry.paddr)) << 21)
#define L2_entry_addr(entry)      (((u64)(entry.paddr)) << 12)
#define L3_entry_addr(entry)      (((u64)(entry.paddr)) << 12)

#define PPN(p_addr) ((paddr)p_addr >> 12)
#define VPN(v_addr) ((vaddr)v_addr >> 12)
#define PADDR(ppn)  ((u64)ppn << 12)
#define VADDR(vpn)  ((u64)vpn << 12)

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