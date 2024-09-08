#include <shampoos/mm/nexus.h>
#include <shampoos/mm/buddy_pmm.h>

struct nexus_node* nexus_root;
extern struct buddy buddy_pmm;

void init_nexus()
{
        /*get a phy page*/
        paddr nexus_init_page = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
        /*get a vir page with Identical mapping*/
}
void get_free_page(int order, enum zone_type memory_zone)
{
}
void free_pages(void* p)
{
}