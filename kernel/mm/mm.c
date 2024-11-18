#include <shampoos/common.h>
#include <shampoos/mm/buddy_pmm.h>
#include <shampoos/mm/map_handler.h>
#include <shampoos/mm/nexus.h>
#include <shampoos/mm/spmalloc.h>
extern struct buddy buddy_pmm;
struct map_handler Map_Handler;
struct nexus_node *nexus_root;
error_t mm_init(struct setup_info *arch_setup_info)
{
        // memory part init
        buddy_pmm.pmm_init(arch_setup_info);
        init_map(&Map_Handler, 0, (struct pmm *)&buddy_pmm);
        nexus_root = init_nexus(&Map_Handler);
        nexus_root->nexus_id = 0;
        sp_init(nexus_root, 0);
        return 0;
}