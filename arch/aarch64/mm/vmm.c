#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/sys_ctrl_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <common/mm.h>
#include <common/bit.h>
#include <common/stdbool.h>

void inline arch_set_L0_entry(paddr ppn, vaddr vpn, union L0_entry *pt_addr,
                              ARCH_PFLAGS_T flags)
{
        pt_addr[L0_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L1_entry(paddr ppn, vaddr vpn, union L1_entry *pt_addr,
                              ARCH_PFLAGS_T flags)
{
        pt_addr[L1_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L1_entry_huge(paddr ppn, vaddr vpn,
                                   union L1_entry_huge *pt_addr,
                                   ARCH_PFLAGS_T flags)
{
        pt_addr[L1_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L2_entry(paddr ppn, vaddr vpn, union L2_entry *pt_addr,
                              ARCH_PFLAGS_T flags)
{
        pt_addr[L2_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L2_entry_huge(paddr ppn, vaddr vpn,
                                   union L2_entry_huge *pt_addr,
                                   ARCH_PFLAGS_T flags)
{
        pt_addr[L2_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void inline arch_set_L3_entry(paddr ppn, vaddr vpn, union L3_entry *pt_addr,
                              ARCH_PFLAGS_T flags)
{
        pt_addr[L3_INDEX(vpn)].entry = (ppn & PT_DESC_ADDR_MASK) | flags;
}
void mair_init()
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
static bool is_page_or_block(int entry_level, ENTRY_FLAGS_t ENTRY_FLAGS)
{
        return ((ENTRY_FLAGS & PAGE_ENTRY_HUGE)
                && (entry_level == 1 || entry_level == 2))
               || (entry_level == 3);
}
ARCH_PFLAGS_T arch_decode_flags(int entry_level, ENTRY_FLAGS_t ENTRY_FLAGS)
{
        ARCH_PFLAGS_T ARCH_PFLAGS = 0;
        if (is_page_or_block(entry_level, ENTRY_FLAGS))
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_ATTR_LOWER_AF);

        if (ENTRY_FLAGS & PAGE_ENTRY_READ) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_V);
        }
        if (!(ENTRY_FLAGS & PAGE_ENTRY_WRITE)) {
                if (is_page_or_block(entry_level, ENTRY_FLAGS))
                        ARCH_PFLAGS =
                                set_mask(ARCH_PFLAGS, PT_DESC_ATTR_LOWER_AP_RO);
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_USER) {
                if (is_page_or_block(entry_level, ENTRY_FLAGS))
                        ARCH_PFLAGS = set_mask(ARCH_PFLAGS,
                                               PT_DESC_ATTR_LOWER_AP_EL0);
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_ATTR_UPPER_PXN);
                if (!(ENTRY_FLAGS & PAGE_ENTRY_EXEC)) {
                        ARCH_PFLAGS =
                                set_mask(ARCH_PFLAGS, PT_DESC_ATTR_UPPER_XN);
                }
        } else {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, PT_DESC_ATTR_UPPER_XN);
                if (!(ENTRY_FLAGS & PAGE_ENTRY_EXEC)) {
                        ARCH_PFLAGS =
                                set_mask(ARCH_PFLAGS, PT_DESC_ATTR_UPPER_PXN);
                }
        }
        if (ENTRY_FLAGS & PAGE_ENTRY_UNCACHED) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, MEM_ATTR_UNCACHED);
        } else if (ENTRY_FLAGS & PAGE_ENTRY_DEVICE) {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, MEM_ATTR_DEVICE);
        } else {
                ARCH_PFLAGS = set_mask(ARCH_PFLAGS, MEM_ATTR_NORMAL);
        }
        if (!(ENTRY_FLAGS & PAGE_ENTRY_HUGE)) {
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
                if (is_page_or_block(entry_level, ENTRY_FLAGS))
                        ARCH_PFLAGS =
                                set_mask(ARCH_PFLAGS, PT_DESC_ATTR_LOWER_NG);
        }
        return ARCH_PFLAGS;
}
ENTRY_FLAGS_t arch_encode_flags(int entry_level, ARCH_PFLAGS_T ARCH_PFLAGS)
{
        ENTRY_FLAGS_t ENTRY_FLAGS = 0;
        if (!(ARCH_PFLAGS & PT_DESC_V))
                return ENTRY_FLAGS;
        ENTRY_FLAGS = PAGE_ENTRY_READ | PAGE_ENTRY_VALID;
        if ((ARCH_PFLAGS & PT_DESC_ATTR_LOWER_AF)
            && (entry_level == 1 || entry_level == 2))
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_HUGE);
        if (!(ARCH_PFLAGS & PT_DESC_ATTR_LOWER_AP_RO))
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_WRITE);
        if (ARCH_PFLAGS & PT_DESC_ATTR_LOWER_AP_EL0) {
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_USER);
                if (!(ARCH_PFLAGS & PT_DESC_ATTR_UPPER_XN))
                        ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_EXEC);
        } else if (!(ARCH_PFLAGS & PT_DESC_ATTR_UPPER_PXN))
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_EXEC);

        u64 mem_attr = ARCH_PFLAGS & PT_DESC_ATTR_LOWER_ATTRINDX_MASK;
        if (mem_attr == MEM_ATTR_DEVICE)
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_DEVICE);
        else if (mem_attr == MEM_ATTR_UNCACHED)
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_UNCACHED);

        if (((ENTRY_FLAGS & PAGE_ENTRY_HUGE) || entry_level == 3)
            && (ENTRY_FLAGS & PT_DESC_ATTR_LOWER_NG))
                ENTRY_FLAGS = set_mask(ENTRY_FLAGS, PAGE_ENTRY_GLOBAL);
        return ENTRY_FLAGS;
}