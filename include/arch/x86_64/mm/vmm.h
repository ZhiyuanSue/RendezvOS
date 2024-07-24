#ifndef _SHAMPOOS_ARCH_VMM_H_
#define _SHAMPOOS_ARCH_VMM_H_
#include <common/types.h>

struct L0_entry {
	union {
		u64 entry;
		struct {};
	};
};
struct L1_entry {
	u64 entry;
};
struct L2_entry {
	u64 entry;
};
struct L3_entry {
	u64 entry;
};
void arch_set_L0_entry(paddr ppn, vaddr vpn, struct L0_entry *pt_addr,
					   u64 flags);
void arch_set_L1_entry(paddr ppn, vaddr vpn, struct L1_entry *pt_addr,
					   u64 flags);
void arch_set_L1_entry_huge(paddr ppn, vaddr vpn, struct L1_entry *pt_addr,
							u64 flags);
void arch_set_L2_entry(paddr ppn, vaddr vpn, struct L2_entry *pt_addr,
					   u64 flags);
void arch_set_L2_entry_huge(paddr ppn, vaddr vpn, struct L2_entry *pt_addr,
							u64 flags);
void arch_set_L3_entry(paddr ppn, vaddr vpn, struct L3_entry *pt_addr,
					   u64 flags);
#endif