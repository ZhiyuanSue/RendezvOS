#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/sys_ctrl_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <common/mm.h>
#include <common/bit.h>

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
u64 mair_init()
{
        /*we use the mair register to indicate the memory attr,so we need init
         * it*/
        u64 mair_reg_val = 0;
        u8 attr[8] = {MAIR_EL1_ATTR_0,
#define MEM_ATTR_DEVICE (0 << PT_DESC_ATTR_LOWER_ATTRINDX_OFF)
                      MAIR_EL1_ATTR_1,
#define MEM_ATTR_UNCACHED (1 << PT_DESC_ATTR_LOWER_ATTRINDX_OFF)
                      MAIR_EL1_ATTR_2,
#define MEM_ATTR_NORMAL (2 << PT_DESC_ATTR_LOWER_ATTRINDX_OFF)
                      MAIR_EL1_ATTR_3,
                      MAIR_EL1_ATTR_4,
                      MAIR_EL1_ATTR_5,
                      MAIR_EL1_ATTR_6,
                      MAIR_EL1_ATTR_7};
        for (int index = 0; index < MAIR_EL1_NR; index++) {
                mair_reg_val = set_mask(mair_reg_val,
                                        ((u64)attr[index]) << (index * 8));
        }
        asm volatile("msr mair_el1, %0;" : : "r"(mair_reg_val));
}
u64 arch_decode_flags(int entry_level, u64 ENTRY_FLAGS)
{
        u64 ARCH_PFLAGS;
        ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_ATTR_LOWER_AF);
        if (ENTRY_FLAGS & PAGE_ENTRY_WRITE) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_READ) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_V);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_EXEC) {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_USER) {
        } else {
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_UNCACHED) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, MEM_ATTR_UNCACHED);
        } else if (ENTRY_FLAGS & PAGE_ENTRY_DEVICE) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, MEM_ATTR_DEVICE);
        } else {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, MEM_ATTR_NORMAL);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_HUGE) {
                switch (entry_level) {
                case 1:
                        ARCH_PFLAGS =
                                set_mask(ARCH_PFLAGS, PT_DESC_BLOCK_OR_TABLE);
                        break;
                case 2:
                        ARCH_PFLAGS =
                                set_mask(ARCH_PFLAGS, PT_DESC_BLOCK_OR_TABLE);
                        break;
                default:
                        break;
                }
        }
        if (!(ENTRY_FLAGS & PAGE_ENTRY_GLOBAL)) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_ATTR_LOWER_NG);
        }
        return ARCH_PFLAGS;
}
u64 arch_encode_flags(int entry_level, u64 ARCH_PFLAGS)
{
}