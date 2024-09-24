#ifndef _SHAMPOOS_ARCH_VMM_H_
#define _SHAMPOOS_ARCH_VMM_H_
#include "page_table_def.h"
#include <common/types.h>
#include <common/mm.h>
#include <arch/x86_64/sync/tlb.h>

/*some bit of cr3*/
#define CR3_PWT           (1 << 3)
#define CR3_PCD           (1 << 4)
#define CR3_ADDR(addr, m) (((addr >> 12) << 12) & MAXPHYADDR_mask(m))
#define CR3_PCID(pcid)    (pcid & ((1 << 12) - 1))

extern u32 max_phy_addr_width;
static inline paddr get_current_kernel_vspace_root()
{
        /* read the cr3*/
        paddr cr3_tmp;
        asm volatile("movq %%cr3,%0;" : "=&r"(cr3_tmp) :);
        return CR3_ADDR(cr3_tmp, max_phy_addr_width);
}

union L0_entry { // PML4E
        u64 entry;
        struct {
                u64 P : 1;
                u64 RW : 1;
                u64 US : 1;
                u64 PWT : 1;
                u64 PCD : 1;
                u64 A : 1;
                u64 : 1;
                u64 PS : 1;
                u64 : 4;
                u64 paddr : 40;
                u64 : 11;
                u64 XD : 1;
        };
};
union L1_entry_huge { // PDPTE
        u64 entry;
        struct {
                u64 P : 1;
                u64 RW : 1;
                u64 US : 1;
                u64 PWT : 1;
                u64 PCD : 1;
                u64 A : 1;
                u64 D : 1; // only used in 1G huge page
                u64 PS : 1;
                u64 G : 1; // only used in 1G huge page
                u64 : 3;
                u64 PAT : 1;
                u64 : 17;
                u64 vaddr : 22;
                u64 : 7;
                u64 PK : 4; // only used in 1G huge page and CR4.PKE is enabled
                u64 XD : 1;
        };
};
union L1_entry {
        u64 entry;
        struct {
                u64 P : 1;
                u64 RW : 1;
                u64 US : 1;
                u64 PWT : 1;
                u64 PCD : 1;
                u64 A : 1;
                u64 : 1;
                u64 PS : 1;
                u64 : 4;
                u64 paddr : 40;
                u64 : 11;
                u64 XD : 1;
        };
};
union L2_entry_huge { // PDE
        u64 entry;
        struct {
                u64 P : 1;
                u64 RW : 1;
                u64 US : 1;
                u64 PWT : 1;
                u64 PCD : 1;
                u64 A : 1;
                u64 D : 1; // only used in 2m huge page
                u64 PS : 1;
                u64 G : 1; // only used in 2m huge page
                u64 : 3;
                u64 PAT : 1;
                u64 : 8;
                u64 paddr : 31;
                u64 : 7;
                u64 PK : 4; // only used in 2m huge page and CR4.PKE is enabled
                u64 XD : 1;
        };
};
union L2_entry {
        u64 entry;
        struct {
                u64 P : 1;
                u64 RW : 1;
                u64 US : 1;
                u64 PWT : 1;
                u64 PCD : 1;
                u64 A : 1;
                u64 : 1;
                u64 PS : 1;
                u64 : 4;
                u64 paddr : 40;
                u64 : 11;
                u64 XD : 1;
        };
};
union L3_entry {
        u64 entry;
        struct {
                u64 P : 1;
                u64 RW : 1;
                u64 US : 1;
                u64 PWT : 1;
                u64 PCD : 1;
                u64 A : 1;
                u64 D : 1;
                u64 PAT : 1;
                u64 G : 1;
                u64 : 3;
                u64 paddr : 40;
                u64 : 7;
                u64 PK : 4; // only used in CR4.PKE is enabled
                u64 XD : 1;
        };
};
#endif