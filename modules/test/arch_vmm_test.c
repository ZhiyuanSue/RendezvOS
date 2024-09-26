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

#define vp_1 0xffffff7fbfdfe000
/*this vpn address means four level index are all 1111 1111 0 */
#define vp_2 0xffffff7fbfa00000
/*this vpn address means the level 0 and 1 are all 1111 1111 0, and level 2 is 1111 1110 1,level 3 is 0*/

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
        /*=== === === ===*/

        /*TEST:memset the map l3 table to 0*/
        u32 ppn = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        paddr page_paddr = ppn << 12;
        vaddr page_vaddr = map_pages;
        ENTRY_FLAGS_t flags = arch_decode_flags(
                3, PAGE_ENTRY_READ | PAGE_ENTRY_VALID | PAGE_ENTRY_WRITE);

        arch_set_L3_entry(
                page_paddr, page_vaddr, (union L3_entry *)&MAP_L3_table, flags);
        u64 tmp_test = *((u64 *)page_vaddr);
        *((u64 *)page_vaddr) = tmp_test + 1;
        if (*((u64 *)page_vaddr) != tmp_test + 1)
                pr_error("[ TEST ] error write in during init map test\n");
        memset((char *)page_vaddr, 0, PAGE_SIZE);
        if (*((u64 *)page_vaddr))
                pr_error("[ TEST ] error memset in during init map test\n");
        buddy_pmm.pmm_free(ppn, 1);
        arch_tlb_invalidate(page_vaddr);
        pr_info("[ TEST ] vmm:init map system pass!\n");
        /*=== === === ===*/
        /*
                first alloc one page frame, alloc a new 4K virtual region, and
           try map ,expect success
        */
        paddr old_vspace_root = get_current_kernel_vspace_root();
        u32 old_ppn_1 = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        if (map(&old_vspace_root,
                old_ppn_1,
                VPN(vp_1),
                3,
                (struct pmm *)&buddy_pmm)) {
                pr_error("[ TEST ] ERROR:map 4K virtual error!\n");
                goto arch_vmm_test_error;
        }
        memset((void *)vp_1, 0, PAGE_SIZE);

        /*
                then we alloc another page frame and try to map to same virtual
           region ,expect fail
        */
        u32 old_ppn_2 = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
		if (!map(&old_vspace_root,old_ppn_1,VPN(vp_1),3,(struct pmm *)&buddy_pmm)){
			pr_error("[ TEST ] ERROR:try to map to same virtual page but no error return\n");
			goto arch_vmm_test_error;
		}

        /*free the second page frame to buddy and do not unmap*/
		if(!buddy_pmm.pmm_free(old_ppn_2,ZONE_NORMAL)){
			pr_error("[ TEST ] ERROR:try to free a physical page fail\n");
			goto arch_vmm_test_error;
		}
        /*
                this time alloc a new 2M virtual region
        */

        /*
                another 2M page map to same virtual region
				except same fail as 4K remap
        */

        /*free them and unmap*/

        /*
                alloc a 4K but map to a level3 2M page (the same 2M virtual
           region,but unmapped) , although unnormal, expect success
        */

        /*
                let the vspace root page be empty, and try to map the same 4K
           region(for it's another vspace), expect success
        */
        /*alloc a 4K and try to map at the new vspace with a level 1, expect
         * fail*/

        /*alloc a 4K and try to map at the new vspace with a level 0, expect
         * fail*/

        /*unmap the old_ppn_1 and free*/

		/* try to map a 2M page to the same vp of old_ppn_1 mapped*/

		/*unmap the 2M page and free*/
		
        pr_info("[ TEST ] vmm:vmm map test pass!\n");

        return;
arch_vmm_test_error:
        pr_error("[ ERROR ] arch vmm test failed\n");
}