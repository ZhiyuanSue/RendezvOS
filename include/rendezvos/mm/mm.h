#ifndef _RENDEZVOS_MM_H_
#define _RENDEZVOS_MM_H_
#include <common/dsa/list.h>
#include <rendezvos/sync/spin_lock.h>

struct vspace {
        paddr vspace_root;
        uint64_t vspace_id;
        spin_lock vspace_lock;
        struct list_entry vspace_node;
        // TODO: we just use list entry to orginize the vspaces now
};
extern struct vspace* current_vspace; // per cpu pointer
extern struct spin_lock_t vspace_spin_lock; // per cpu pointer
void init_vspace(struct vspace* vs, paddr vspace_root_addr, uint64_t vspace_id);

#endif