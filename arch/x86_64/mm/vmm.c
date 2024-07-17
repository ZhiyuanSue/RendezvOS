#include <arch/x86_64/mm/page_table.h>
#include <arch/x86_64/mm/page_table_def.h>
#include <arch/x86_64/mm/vmm.h>
u32 max_phy_addr_width;
void inline arch_set_L0_entry(paddr ppn, vaddr vpn, u64 *pt_addr, u64 flags) {
	pt_addr[PML4(vpn)] = PML4E_ADDR(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L1_entry(paddr ppn, vaddr vpn, u64 *pt_addr, u64 flags) {
	pt_addr[PDPT(vpn)] = PDPTE_ADDR(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L1_entry_huge(paddr ppn, vaddr vpn, u64 *pt_addr, u64 flags) {
	pt_addr[PDPT(vpn)] = PDPTE_ADDR_1G(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L2_entry(paddr ppn, vaddr vpn, u64 *pt_addr, u64 flags) {
	pt_addr[PDT(vpn)] = PDE_ADDR(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L2_entry_huge(paddr ppn, vaddr vpn, u64 *pt_addr, u64 flags) {
	pt_addr[PDT(vpn)] = PDE_ADDR_2M(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L3_entry(paddr ppn, vaddr vpn, u64 *pt_addr, u64 flags) {
	pt_addr[PT(vpn)] = PTE_ADDR(ppn, max_phy_addr_width) | flags;
}