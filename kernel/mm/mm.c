#include <shampoos/common.h>
#include <shampoos/mm/buddy_pmm.h>
#include <shampoos/mm/map_handler.h>
#include <shampoos/mm/nexus.h>
#include <shampoos/mm/spmalloc.h>
#include <shampoos/percpu.h>
extern struct buddy buddy_pmm;
DEFINE_PER_CPU(struct map_handler, Map_Handler);
DEFINE_PER_CPU(struct nexus_node *, nexus_root);
error_t mm_init(struct setup_info *arch_setup_info)
{
        // memory part init
        buddy_pmm.pmm_init(arch_setup_info);
        init_map(&Map_Handler, 0, (struct pmm *)&buddy_pmm);
        nexus_root = init_nexus(&Map_Handler);
        sp_init(nexus_root, Map_Handler.cpu_id);
        return 0;
}