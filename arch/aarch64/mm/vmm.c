#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/vmm.h>
void inline arch_set_L0_entry(paddr ppn, vaddr vpn, struct L0_entry *pt_addr,
							  u64 flags) {
	pt_addr[(vpn << 16) >> 55].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L1_entry(paddr ppn, vaddr vpn, struct L1_entry *pt_addr,
							  u64 flags) {
	pt_addr[(vpn << 25) >> 55].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L1_entry_huge(paddr ppn, vaddr vpn,
								   struct L1_entry *pt_addr, u64 flags) {
	pt_addr[(vpn << 25) >> 55].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L2_entry(paddr ppn, vaddr vpn, struct L2_entry *pt_addr,
							  u64 flags) {
	pt_addr[(vpn << 34) >> 55].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L2_entry_huge(paddr ppn, vaddr vpn,
								   struct L2_entry *pt_addr, u64 flags) {
	pt_addr[(vpn << 34) >> 55].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L3_entry(paddr ppn, vaddr vpn, struct L3_entry *pt_addr,
							  u64 flags) {
	pt_addr[(vpn << 43) >> 55].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}