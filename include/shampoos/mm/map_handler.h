#ifndef _SHAMPOOS_MAP_HANDLER_
#define _SHAMPOOS_MAP_HANDLER_
#include <common/types.h>
#include <common/mm.h>

#define map_pages 0xFFFFFFFFFFE00000
/*
        we use last 2M page as the set of the map used pages virtual addr
        we use one 4K page during the mapping stage
        but consider the multi-core, we use last 2M as this per cpu map page set
        and we think we should not have more than 512 cores
*/
struct map_handler {
        u64 cpu_id;
        vaddr map_vaddr[4];
        struct pmm* pmm;
} __attribute__((packed));
extern struct map_handler Map_Handler;
void init_map(struct map_handler* handler, int cpu_id, struct pmm* pmm);
/*
        kernel might try to mapping one page to a different vspace
        and if the vspace is not exist, it should try to alloc a new one
*/
error_t map(paddr* vspace_root_paddr, u64 ppn, u64 vpn, int level,
            ENTRY_FLAGS_t eflags, struct map_handler* handler);
/*
        here we think the vspace root paddr must exist.
        and we expect the vpn and the page number we need to unmap
*/
error_t unmap(paddr vspace_root_paddr, u64 vpn, struct map_handler* handler);

/*
        check whether the vpn have mapped in this vspace
*/
paddr have_mapped(paddr vspace_root_paddr, u64 vpn,
                  struct map_handler* handler);
#endif