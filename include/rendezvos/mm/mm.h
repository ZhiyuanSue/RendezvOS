#ifndef _RENDEZVOS_MM_H_
#define _RENDEZVOS_MM_H_
#include <common/dsa/list.h>
#include <rendezvos/sync/spin_lock.h>

typedef struct {
        paddr vspace_root_addr;
        uint64_t vspace_id;
        spin_lock vspace_lock;
        struct list_entry vspace_node;
        // TODO: we just use list entry to orginize the vspaces now
} VSpace;
extern VSpace* current_vspace; // per cpu pointer
extern struct spin_lock_t vspace_spin_lock; // per cpu pointer
extern u64 boot_stack_bottom;

VSpace* new_vspace();
void init_vspace(VSpace* vs, paddr vspace_root_addr, uint64_t vspace_id);

#endif