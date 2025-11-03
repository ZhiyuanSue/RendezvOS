#ifndef _RENDEZVOS_MM_H_
#define _RENDEZVOS_MM_H_
#include <common/dsa/list.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/sync/cas_lock.h>

typedef struct {
        paddr vspace_root_addr;
        u64 vspace_id;
        spin_lock vspace_lock;
        cas_lock_t nexus_vspace_lock;
        void* _vspace_node;
} VSpace;
extern VSpace* current_vspace; // per cpu pointer
extern struct spin_lock_t vspace_spin_lock; // per cpu pointer
extern u64 boot_stack_bottom;

VSpace* new_vspace();
void init_vspace(VSpace* vs, u64 vspace_id, void* vspace_node);
void del_vspace(VSpace** vs);
static inline void set_vspace_root_addr(VSpace* vs, paddr root_paddr)
{
        vs->vspace_root_addr = root_paddr;
}

#endif