#ifndef _SHAMPOOS_ARCH_VMM_H_
#define _SHAMPOOS_ARCH_VMM_H_
#include <common/types.h>

union L0_entry {  // PML4E
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
union L1_entry_huge {  // PDPTE
	u64 entry;
	struct {
		u64 P : 1;
		u64 RW : 1;
		u64 US : 1;
		u64 PWT : 1;
		u64 PCD : 1;
		u64 A : 1;
		u64 D : 1;	// only used in 1G huge page
		u64 PS : 1;
		u64 G : 1;	// only used in 1G huge page
		u64 : 3;
		u64 PAT : 1;
		u64 : 17;
		u64 vaddr : 22;
		u64 : 7;
		u64 PK : 4;	 // only used in 1G huge page and CR4.PKE is enabled
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
union L2_entry_huge {  // PDE
	u64 entry;
	struct {
		u64 P : 1;
		u64 RW : 1;
		u64 US : 1;
		u64 PWT : 1;
		u64 PCD : 1;
		u64 A : 1;
		u64 D : 1;	// only used in 2m huge page
		u64 PS : 1;
		u64 G : 1;	// only used in 2m huge page
		u64 : 3;
		u64 PAT : 1;
		u64 : 8;
		u64 vaddr : 31;
		u64 : 7;
		u64 PK : 4;	 // only used in 2m huge page and CR4.PKE is enabled
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
		u64 vaddr : 40;
		u64 : 7;
		u64 PK : 4;	 // only used in CR4.PKE is enabled
		u64 XD : 1;
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