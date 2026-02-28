#ifndef _RENDEZVOS_VMM_H_
#define _RENDEZVOS_VMM_H_
#include "pmm.h"

#ifdef _AARCH64_
#include <arch/aarch64/mm/vmm.h>
#elif defined _LOONGARCH_
#include <arch/loongarch/mm/vmm.h>
#elif defined _RISCV64_
#include <arch/riscv64/mm/vmm.h>
#elif defined _X86_64_
#include <arch/x86_64/mm/vmm.h>
#else
#include <arch/x86_64/mm/vmm.h>
#endif
#include <common/dsa/rb_tree.h>

typedef struct {
        paddr vspace_root_addr;
        u64 vspace_id;
        spin_lock vspace_lock;
        cas_lock_t nexus_vspace_lock;
        void* _vspace_node;
} VSpace;
extern VSpace* current_vspace; // per cpu pointer
extern struct spin_lock_t handler_spin_lock; // per cpu pointer
#define boot_stack_size 0x10000
extern u64 boot_stack_bottom;

VSpace* new_vspace();
static inline void init_vspace(VSpace* vs, u64 vspace_id, void* vspace_node)
{
        vs->vspace_lock = NULL;
        vs->vspace_id = vspace_id;
        vs->_vspace_node = vspace_node;
}
void del_vspace(VSpace** vs);
static inline void set_vspace_root_addr(VSpace* vs, paddr root_paddr)
{
        vs->vspace_root_addr = root_paddr;
}
static inline void unset_vspace_root_addr(VSpace* vs)
{
        vs->vspace_root_addr = 0;
}

void arch_set_L0_entry(paddr p, vaddr v, union L0_entry* pt_addr,
                       ARCH_PFLAGS_t flags);
void arch_set_L1_entry(paddr p, vaddr v, union L1_entry* pt_addr,
                       ARCH_PFLAGS_t flags);
void arch_set_L2_entry(paddr p, vaddr v, union L2_entry* pt_addr,
                       ARCH_PFLAGS_t flags);
void arch_set_L3_entry(paddr p, vaddr v, union L3_entry* pt_addr,
                       ARCH_PFLAGS_t flags);
/*use those functions to set page entry flags for every page entry*/
ARCH_PFLAGS_t arch_decode_flags(int entry_level, ENTRY_FLAGS_t ENTRY_FLAGS);
ENTRY_FLAGS_t arch_encode_flags(int entry_level, ARCH_PFLAGS_t ARCH_PFLAGS);

error_t virt_mm_init(u32 cpu_id, struct setup_info* arch_setup_info);
#endif