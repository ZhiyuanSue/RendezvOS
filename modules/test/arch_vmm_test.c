#include <modules/test/test.h>
#include <shampoos/mm/buddy_pmm.h>

#ifdef _AARCH64_
#include <arch/aarch64/mm/vmm.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/mm/vmm.h>
#elif defined _RISCV64_
#include <arch/riscv64/mm/vmm.h>
#elif defined _X86_64_
#include <arch/x86_64/mm/vmm.h>
#else /*for default config is x86_64*/
#include <arch/x86_64/mm/vmm.h>
#endif
#include <shampoos/mm/vmm.h>
#include <common/string.h>

union L0_entry l0;
union L1_entry_huge l1_h;
union L1_entry l1;
union L2_entry_huge l2_h;
union L2_entry l2;
union L3_entry l3;
extern char MAP_L3_table;
extern struct buddy buddy_pmm;

void arch_vmm_test(void)
{
        if (sizeof(l0) != sizeof(u64) || sizeof(l1_h) != sizeof(u64)
            || sizeof(l1) != sizeof(u64) || sizeof(l2_h) != sizeof(u64)
            || sizeof(l2) != sizeof(u64) || sizeof(l3) != sizeof(u64)) {
                pr_error("vmm entry align error\n");
                goto arch_vmm_test_error;
        }
        pr_info("[ TEST ] vmm:arch vmm test pass!\n");

        /*TEST:memset the map l3 table to 0*/
        u32 ppn = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        paddr page_paddr = ppn << 12;
        vaddr page_vaddr = map_pages;
        ENTRY_FLAGS_t flags = arch_decode_flags(
                3, PAGE_ENTRY_READ | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE);

        arch_set_L3_entry(
                page_paddr, map_pages, (union L3_entry *)&MAP_L3_table, flags);
		u64 tmp_test=*((u64 *)map_pages);
		*((u64 *)map_pages) = tmp_test+1;
		if(*((u64 *)map_pages)!=tmp_test+1)
			pr_error("[ TEST ] error write in during init map test\n");
        memset((char *)map_pages, 0, PAGE_SIZE);
		if(*((u64 *)map_pages))
			pr_error("[ TEST ] error memset in during init map test\n");
        pr_info("[ TEST ] vmm:init map system success\n");

        return;
arch_vmm_test_error:
        pr_error("arch vmm test failed\n");
}