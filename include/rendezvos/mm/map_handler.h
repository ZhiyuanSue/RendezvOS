#ifndef _RENDEZVOS_MAP_HANDLER_
#define _RENDEZVOS_MAP_HANDLER_
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
void sys_init_map();
void init_map(struct map_handler* handler, int cpu_id, struct pmm* pmm);
/*
        kernel might try to mapping one page to a different vspace
        and if the vspace is not exist, it should try to alloc a new one
*/

#include <common/dsa/list.h>

#include <rendezvos/sync/spin_lock.h>
struct vspace {
        paddr vspace_root;
        uint64_t vspace_id;
        spin_lock vspace_lock;
        struct list_entry vspace_node;
        // TODO: we just use list entry to orginize the vspaces now
};
extern spin_lock kspace_spin_lock_ptr;
extern struct vspace* current_vspace; // per cpu pointer
extern struct spin_lock_t vspace_spin_lock; // per cpu pointer
void init_vspace(struct vspace* vs, paddr vspace_root_addr, uint64_t vspace_id);

error_t map(struct vspace* vs, u64 ppn, u64 vpn, int level,
            ENTRY_FLAGS_t eflags, struct map_handler* handler);
/*
        here we think the vspace root paddr must exist.
        and we expect the vpn and the page number we need to unmap
*/
error_t unmap(struct vspace* vs, u64 vpn, struct map_handler* handler);

/*
        check whether the vpn have mapped in this vspace
*/
paddr have_mapped(struct vspace* vs, u64 vpn, struct map_handler* handler);
#endif