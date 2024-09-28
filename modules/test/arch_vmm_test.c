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

#define vp_1    0xffffff7fbfdfe000
#define vp_1_2M 0xffffff7fbfc00000
/*this vpn address means four level index are all 1111 1111 0 */
#define vp_2 0xffffff7fbfa00000
/*this vpn address means the level 0 and 1 are all 1111 1111 0, and level 2 is
 * 1111 1110 1,level 3 is 0*/

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
        pr_info("[ TEST ] PASS: vmm:arch vmm test ok!\n");
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
        arch_tlb_invalidate_page(0, page_vaddr);
        pr_info("[ TEST ] PASS: vmm:init map system ok!\n");
        /*=== === === ===*/
        /*
                first alloc one page frame, alloc a new 4K virtual region, and
           try map ,expect success
        */
        paddr old_vspace_root = get_current_kernel_vspace_root();
        /* actually we should lock this pmm_alloc, but it's a test, we think
         * there's no preemt*/
        int ppn_1 = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        if (ppn_1 <= 0) {
                pr_error("[ ERROR ] ERROR:try get a ppn fail\n");
                goto arch_vmm_test_error;
        }
        if (map(&old_vspace_root,
                ppn_1,
                VPN(vp_1),
                3,
                (struct pmm *)&buddy_pmm)) {
                pr_error("[ TEST ] ERROR:map 4K virtual error!\n");
                goto arch_vmm_test_error;
        }
        memset((void *)vp_1, 0, PAGE_SIZE);
        pr_info("[ TEST ] PASS: map 4K ok!\n");
        /*
                then we alloc another page frame and try to map to same virtual
           region ,expect fail
        */
        int ppn_2 = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        if (ppn_2 <= 0) {
                pr_error("[ ERROR ] ERROR:try get a ppn fail\n");
                goto arch_vmm_test_error;
        }
        if (!map(&old_vspace_root,
                 ppn_2,
                 VPN(vp_1),
                 3,
                 (struct pmm *)&buddy_pmm)) {
                pr_error(
                        "[ TEST ] ERROR:try to map to same virtual page but no error return\n");
                goto arch_vmm_test_error;
        }

        /*free the second page frame to buddy and do not unmap*/
        if (buddy_pmm.pmm_free(ppn_2, ZONE_NORMAL)) {
                pr_error("[ TEST ] ERROR:try to free a physical page fail\n");
                goto arch_vmm_test_error;
        }
        pr_info("[ TEST ] PASS: remap 4K ok!\n");
        /*
                this time alloc a new 2M virtual region
        */
        int ppn_3 =
                buddy_pmm.pmm_alloc(MIDDLE_PAGE_SIZE / PAGE_SIZE, ZONE_NORMAL);
        if (ppn_3 <= 0) { /*we expect the ppn aligned*/
                pr_error("[ ERROR ] ERROR:try get a ppn fail\n");
                goto arch_vmm_test_error;
        } else if (ppn_3 & mask_9_bit) {
                pr_error(
                        "[ ERROR ] ERROR:get a unaligned 2M page with ppn 0x%x\n",
                        ppn_3);
                goto arch_vmm_test_error;
        }
        if (map(&old_vspace_root,
                ppn_3,
                VPN(vp_2),
                2,
                (struct pmm *)&buddy_pmm)) {
                pr_error("[ TEST ] ERROR:try to map to a 2M page and fail\n");
                goto arch_vmm_test_error;
        }
        memset((void *)vp_2, 0, MIDDLE_PAGE_SIZE);
        pr_info("[ TEST ] PASS: map 2M ok!\n");

        /*
                another 2M page map to same virtual region
                                except same fail as 4K remap
        */
        int ppn_4 =
                buddy_pmm.pmm_alloc(MIDDLE_PAGE_SIZE / PAGE_SIZE, ZONE_NORMAL);
        if (ppn_4 <= 0) { /*we expect the ppn aligned*/
                pr_error("[ ERROR ] ERROR:try get a ppn fail\n");
                goto arch_vmm_test_error;
        } else if (ppn_4 & mask_9_bit) {
                pr_error(
                        "[ ERROR ] ERROR:get a unaligned 2M page with ppn 0x%x\n",
                        ppn_4);
                goto arch_vmm_test_error;
        }
        if (!map(&old_vspace_root,
                 ppn_4,
                 VPN(vp_2),
                 2,
                 (struct pmm *)&buddy_pmm)) {
                pr_error(
                        "[ TEST ] ERROR:try to map to same virtual page but no error return\n");
                goto arch_vmm_test_error;
        }

        /*free them and unmap*/
        if (buddy_pmm.pmm_free(ppn_4, ZONE_NORMAL)) {
                pr_error("[ TEST ] ERROR:try to free a physical page fail\n");
                goto arch_vmm_test_error;
        }
        if (unmap(old_vspace_root, VPN(vp_2))) {
                pr_error("[ TEST ] ERROR: try to unmap a 2M page fail\n");
                goto arch_vmm_test_error;
        }
        if (buddy_pmm.pmm_free(ppn_3, ZONE_NORMAL)) {
                pr_error("[ TEST ] ERROR:try to free a physical page fail\n");
                goto arch_vmm_test_error;
        }
        pr_info("[ TEST ] PASS: remap 2M ok!\n");

        /*unmap the ppn_1 and free*/
        if (unmap(old_vspace_root, VPN(vp_1))) {
                pr_error("[ TEST ] ERROR: try to unmap a 4K page fail\n");
                goto arch_vmm_test_error;
        }
        if (buddy_pmm.pmm_free(ppn_1, 1)) {
                pr_error("[ TEST ] ERROR:try to free a physical page fail\n");
                goto arch_vmm_test_error;
        }

        /* try to map a 2M page to the same vp of ppn_1 mapped*/
        ppn_1 = buddy_pmm.pmm_alloc(MIDDLE_PAGE_SIZE / PAGE_SIZE, ZONE_NORMAL);
        if (ppn_1 <= 0) {
                pr_error("[ ERROR ] ERROR:try get a ppn fail\n");
                goto arch_vmm_test_error;
        }
        if (map(&old_vspace_root,
                ppn_1,
                VPN(vp_1_2M),
                2,
                (struct pmm *)&buddy_pmm)) {
                pr_error(
                        "[ TEST ] ERROR:try to map a 2M after a 4K map and unmap\n");
                goto arch_vmm_test_error;
        }
        /*unmap the 2M page and free*/
        if (unmap(old_vspace_root, VPN(vp_1_2M))) {
                pr_error("[ TEST ] ERROR: try to unmap a 4K page fail\n");
                goto arch_vmm_test_error;
        }
        if (buddy_pmm.pmm_free(ppn_1, 1)) {
                pr_error("[ TEST ] ERROR:try to free a physical page fail\n");
                goto arch_vmm_test_error;
        }
        pr_info("[ TEST ] PASS: vmm:map a 4K and unmap and map 2M to same place ok!\n");

        pr_info("[ TEST ] PASS: vmm:vmm map test ok!\n");

        return;
arch_vmm_test_error:
        pr_error("[ ERROR ] arch vmm test failed\n");
}