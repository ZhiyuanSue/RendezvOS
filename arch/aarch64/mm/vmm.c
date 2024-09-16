#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <common/mm.h>

void inline arch_set_L0_entry(paddr ppn, vaddr vpn, union L0_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L0_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L1_entry(paddr ppn, vaddr vpn, union L1_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L1_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L1_entry_huge(paddr ppn, vaddr vpn,
                                   union L1_entry_huge *pt_addr, u64 flags)
{
        pt_addr[L1_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L2_entry(paddr ppn, vaddr vpn, union L2_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L2_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L2_entry_huge(paddr ppn, vaddr vpn,
                                   union L2_entry_huge *pt_addr, u64 flags)
{
        pt_addr[L2_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L3_entry(paddr ppn, vaddr vpn, union L3_entry *pt_addr,
                              u64 flags)
{
        pt_addr[L3_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
u64 arch_decode_flags(int entry_level, u64 ENTRY_FLAGS)
{
        u64 page_entry_flags;
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
        return page_entry_flags;
}
u64 arch_encode_flags(int entry_level, u64 ARCH_PFLAGS)
{
}
u64 arch_enable_pflags(int entry_level, u64 flags)
{
}
u64 arch_disable_pflags(int entry_level, u64 flags)
{
}