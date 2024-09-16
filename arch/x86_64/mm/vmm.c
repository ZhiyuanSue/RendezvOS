#include <arch/x86_64/mm/page_table_def.h>
#include <arch/x86_64/mm/vmm.h>
#include <common/mm.h>
#include <common/bit.h>

u32 max_phy_addr_width;
void inline arch_set_L0_entry(paddr ppn, vaddr vpn, union L0_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L0_INDEX(vpn)].entry = PML4E_ADDR(ppn, max_phy_addr_width)
                                       | flags;
}
void inline arch_set_L1_entry(paddr ppn, vaddr vpn, union L1_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L1_INDEX(vpn)].entry = PDPTE_ADDR(ppn, max_phy_addr_width)
                                       | flags;
}
void inline arch_set_L1_entry_huge(paddr ppn, vaddr vpn,
                                   union L1_entry_huge *pt_addr, u64 flags)
{
        pt_addr[L1_INDEX(vpn)].entry = PDPTE_ADDR_1G(ppn, max_phy_addr_width)
                                       | flags | PDPTE_PS;
}
void inline arch_set_L2_entry(paddr ppn, vaddr vpn, union L2_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L2_INDEX(vpn)].entry = PDE_ADDR(ppn, max_phy_addr_width)
                                       | flags;
}
void inline arch_set_L2_entry_huge(paddr ppn, vaddr vpn,
                                   union L2_entry_huge *pt_addr, u64 flags)
{
        pt_addr[L2_INDEX(vpn)].entry = PDE_ADDR_2M(ppn, max_phy_addr_width)
                                       | flags | PDE_PS;
}
void inline arch_set_L3_entry(paddr ppn, vaddr vpn, union L3_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L3_INDEX(vpn)].entry = PTE_ADDR(ppn, max_phy_addr_width)
                                       | flags;
}
u64 arch_decode_flags(int entry_level, u64 ENTRY_FLAGS)
{
        u64 ARCH_PFLAGS = 0;
        if (ENTRY_FLAGS & PAGE_ENTRY_WRITE) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_READ) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_EXEC) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_USER) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_UNCACHED) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_DEVICE) {
        }
        return ARCH_PFLAGS;
}
u64 arch_encode_flags(int entry_level, u64 ARCH_PFLAGS)
{
}
u64 arch_enable_pflags(int entry_level, u64 ARCH_PFLAGS)
{
        u64 flags = ARCH_PFLAGS;
        if (entry_level == 0)
                flags = set_mask(flags, PML4E_P);
        else if (entry_level == 1)
                flags = set_mask(flags, PDPTE_P);
        else if (entry_level == 2)
                flags = set_mask(flags, PDE_P);
        else if (entry_level == 3)
                flags = set_mask(flags, PTE_P);
        return flags;
}
u64 arch_disable_pflags(int entry_level, u64 ARCH_PFLAGS)
{
        u64 flags = ARCH_PFLAGS;
        if (entry_level == 0)
                flags = clear_mask(flags, PML4E_P);
        else if (entry_level == 1)
                flags = clear_mask(flags, PDPTE_P);
        else if (entry_level == 2)
                flags = clear_mask(flags, PDE_P);
        else if (entry_level == 3)
                flags = clear_mask(flags, PTE_P);
        return flags;
}