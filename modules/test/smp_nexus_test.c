
// #define DEBUG
#include <modules/test/test.h>
#include <rendezvos/mm/nexus.h>
#include <modules/log/log.h>
#include <rendezvos/percpu.h>
#define NR_MAX_TEST NEXUS_PER_PAGE * 80
extern struct nexus_node* nexus_root;
DEFINE_PER_CPU(void*, smp_test_ptrs[NR_MAX_TEST]);
int smp_nexus_test(void)
{
        debug("sizeof struct nexus_node is 0x%x\n", sizeof(struct nexus_node));
        /*after the nexus init, we try to print it first*/
        for (int i = 0; i < NR_MAX_TEST; i++) {
                int page_num = 2;
                percpu(smp_test_ptrs)[i] = get_free_page(page_num,
                                                         ZONE_NORMAL,
                                                         KERNEL_VIRT_OFFSET,
                                                         0,
                                                         percpu(nexus_root));
                if (percpu(smp_test_ptrs)[i]) {
                        *((u64*)(percpu(smp_test_ptrs)[i])) = 0;
                        *((u64*)(percpu(smp_test_ptrs)[i] + PAGE_SIZE)) = 0;
                }
        }
        for (int i = 0; i < NR_MAX_TEST; i++) {
                if (percpu(smp_test_ptrs)[i])
                        free_pages(percpu(smp_test_ptrs)[i],
                                   2,
                                   0,
                                   percpu(nexus_root));
        }
        return 0;
}