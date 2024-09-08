#include <arch/x86_64/mm/page_table_def.h>
#include <arch/x86_64/mm/vmm.h>

u32 max_phy_addr_width;
void inline arch_set_L0_entry(paddr ppn, vaddr vpn, union L0_entry *pt_addr,
                              u64 flags)
{
        pt_addr[PML4(vpn)].entry = PML4E_ADDR(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L1_entry(paddr ppn, vaddr vpn, union L1_entry *pt_addr,
                              u64 flags)
{
        pt_addr[PDPT(vpn)].entry = PDPTE_ADDR(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L1_entry_huge(paddr ppn, vaddr vpn,
                                   union L1_entry_huge *pt_addr, u64 flags)
{
        pt_addr[PDPT(vpn)].entry = PDPTE_ADDR_1G(ppn, max_phy_addr_width)
                                   | flags;
}
void inline arch_set_L2_entry(paddr ppn, vaddr vpn, union L2_entry *pt_addr,
                              u64 flags)
{
        pt_addr[PDT(vpn)].entry = PDE_ADDR(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L2_entry_huge(paddr ppn, vaddr vpn,
                                   union L2_entry_huge *pt_addr, u64 flags)
{
        pt_addr[PDT(vpn)].entry = PDE_ADDR_2M(ppn, max_phy_addr_width) | flags;
}
void inline arch_set_L3_entry(paddr ppn, vaddr vpn, union L3_entry *pt_addr,
                              u64 flags)
{
        pt_addr[PT(vpn)].entry = PTE_ADDR(ppn, max_phy_addr_width) | flags;
}