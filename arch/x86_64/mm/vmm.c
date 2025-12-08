#include <arch/x86_64/mm/page_table_def.h>
#include <arch/x86_64/mm/vmm.h>
#include <common/mm.h>
#include <common/bit.h>
#include <rendezvos/mm/vmm.h>

u32 max_phy_addr_width = 0;
inline void arch_set_L0_entry(paddr p, vaddr v, union L0_entry *pt_addr,
                              ARCH_PFLAGS_t flags)
{
        pt_addr[L0_INDEX(v)].entry = PML4E_ADDR(p, max_phy_addr_width) | flags;
}
inline void arch_set_L1_entry(paddr p, vaddr v, union L1_entry *pt_addr,
                              ARCH_PFLAGS_t flags)
{
        pt_addr[L1_INDEX(v)].entry = PDPTE_ADDR(p, max_phy_addr_width) | flags;
}
inline void arch_set_L2_entry(paddr p, vaddr v, union L2_entry *pt_addr,
                              ARCH_PFLAGS_t flags)
{
        pt_addr[L2_INDEX(v)].entry = PDE_ADDR(p, max_phy_addr_width) | flags;
}
inline void arch_set_L3_entry(paddr p, vaddr v, union L3_entry *pt_addr,
                              ARCH_PFLAGS_t flags)
{
        pt_addr[L3_INDEX(v)].entry = PTE_ADDR(p, max_phy_addr_width) | flags;
}
ARCH_PFLAGS_t arch_decode_flags(int entry_level, ENTRY_FLAGS_t ENTRY_FLAGS)
{
        ENTRY_FLAGS_t ARCH_PFLAGS = 0;
        static ENTRY_FLAGS_t arch_entry_flags_valid_bit[4] = {
                PML4E_P, PDPTE_P, PDE_P, PTE_P};
        static ENTRY_FLAGS_t arch_entry_flags_write_bit[4] = {
                PML4E_RW, PDPTE_RW, PDE_RW, PTE_RW};
        static ENTRY_FLAGS_t arch_entry_flags_no_exec_bit[4] = {
                PML4E_XD, PDPTE_XD, PDE_XD, PTE_XD};
        static ENTRY_FLAGS_t arch_entry_flags_user_bit[4] = {
                PML4E_US, PDPTE_US, PDE_US, PTE_US};
        static ENTRY_FLAGS_t arch_entry_flags_uncache_device_bit[4] = {
                PML4E_PCD | PML4E_PWT,
                PDPTE_PCD | PDPTE_PWT,
                PDE_PCD | PDE_PWT,
                PTE_PCD | PTE_PWT};
        static ENTRY_FLAGS_t arch_entry_flags_huge_bit[4] = {
                0, PDPTE_PS, PDE_PS, 0};
        if (entry_level < 0 || entry_level > 3)
                return 0;
        if (ENTRY_FLAGS & PAGE_ENTRY_VALID) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS,
                                       arch_entry_flags_valid_bit[entry_level]);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_WRITE) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS,
                                       arch_entry_flags_write_bit[entry_level]);
        }
        if (!(ENTRY_FLAGS & PAGE_ENTRY_EXEC)) {
                /*this must ensure that the IA32_EFER.NXE==1, we enabled at boot
                 * stage*/
                ARCH_PFLAGS = set_mask(
                        ARCH_PFLAGS, arch_entry_flags_no_exec_bit[entry_level]);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_USER) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS,
                                       arch_entry_flags_user_bit[entry_level]);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_UNCACHED
            || ENTRY_FLAGS & PAGE_ENTRY_DEVICE) {
                ARCH_PFLAGS = set_mask(
                        ARCH_PFLAGS,
                        arch_entry_flags_uncache_device_bit[entry_level]);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_HUGE) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS,
                                       arch_entry_flags_huge_bit[entry_level]);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_GLOBAL) {
                switch (entry_level) {
                        /*no global in pml4*/
                case 1:
                        if (ENTRY_FLAGS & PAGE_ENTRY_HUGE)
                                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PDPTE_G);
                        break;
                case 2:
                        if (ENTRY_FLAGS & PAGE_ENTRY_HUGE)
                                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PDE_G);
                        break;
                case 3:
                        ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PTE_G);
                        break;
                default:
                        break;
                }
        }
        return ARCH_PFLAGS;
}
ENTRY_FLAGS_t arch_encode_flags(int entry_level, ARCH_PFLAGS_t ARCH_PFLAGS)
{
        ENTRY_FLAGS_t ENTRY_FLAGS = 0;
        if (ARCH_PFLAGS & PML4E_P || ARCH_PFLAGS & PDPTE_P
            || ARCH_PFLAGS & PDE_P || ARCH_PFLAGS & PTE_P) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_VALID);
        }
        ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_READ);
        if (ARCH_PFLAGS & PML4E_RW || ARCH_PFLAGS & PDPTE_RW
            || ARCH_PFLAGS & PDE_RW || ARCH_PFLAGS & PTE_RW) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_WRITE);
        }
        if (ARCH_PFLAGS & PML4E_XD || ARCH_PFLAGS & PDPTE_XD
            || ARCH_PFLAGS & PDE_XD || ARCH_PFLAGS & PTE_XD) {
                ENTRY_FLAGS = clear_mask(ENTRY_FLAGS, PAGE_ENTRY_EXEC);
        } else {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_EXEC);
        }
        if (ARCH_PFLAGS & PML4E_US || ARCH_PFLAGS & PDPTE_US
            || ARCH_PFLAGS & PDE_US || ARCH_PFLAGS & PDE_US) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_USER);
        }
        if (ARCH_PFLAGS & (PML4E_PCD | PML4E_PWT)
            || ARCH_PFLAGS & (PDPTE_PCD | PDPTE_PWT)
            || ARCH_PFLAGS & (PDE_PCD | PDE_PWT)
            || ARCH_PFLAGS & (PTE_PCD | PTE_PWT)) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS,
                                       PAGE_ENTRY_DEVICE | PAGE_ENTRY_UNCACHED);
        }
        if ((entry_level == 1 || entry_level == 2)
            && (ARCH_PFLAGS & PDPTE_PS || ARCH_PFLAGS & PDE_PS)) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_HUGE);
        }
        if (((entry_level == 1 || entry_level == 2)
             && (ARCH_PFLAGS & (PDPTE_PS | PDPTE_G)
                 || ARCH_PFLAGS & (PDE_PS | PDE_G)))
            || ((entry_level == 3) && (ARCH_PFLAGS & PTE_G))) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_GLOBAL);
        }

        return ENTRY_FLAGS;
}