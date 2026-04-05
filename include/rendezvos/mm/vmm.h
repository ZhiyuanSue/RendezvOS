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
#include <common/refcount.h>
#include <rendezvos/smp/cpu_id.h>

#ifndef PTE_SIZE
#define PTE_SIZE 8
#endif

/*
 * One type for nexus vs_common + page-table identity (anonymous union: members
 * are as if on VS_Common; interpret only per `type`):
 * - KERNEL_HEAP_REF: `vs` -> shared root VS_Common (table branch); `cpu_id` =
 *   allocating logical CPU for kmem routing.
 * - USER_VSPACE: vspace_root_addr, locks, _vspace_node (table branch).
 * Per-CPU kernel ref object: nexus_kernel_heap_vs_common.
 */
enum vs_common_kind {
        VS_COMMON_NONE = 0,
        VS_COMMON_KERNEL_HEAP_REF,
        VS_COMMON_USER_VSPACE,
};

typedef struct VS_Common {
        u64 type;
        /*
         * Reference count for vspace lifetime.
         * - Owner (task) holds one reference.
         * - CPUs hold active references while CR3/TTBR points to this vspace.
         * - Kernel vspace(root) is always exist during the system running time.
         * Last ref_put runs del_vspace(): if _vspace_node is already NULL but
         * vspace_root_addr is still set, delete_task did nexus + descendant
         * reclaim and only the top-level user root frame remains; otherwise
         * del_vspace does full nexus + PT teardown.
         */
        ref_count_t refcount;
        /*
        The nexus_vspace_lock is the lock that protect the nexus nodes under
        this nexus vspace root. for user space , all the nexus node in this
        vspace is under the vspace node. but for kernel space, the nexus node's
        are split to percpu. So we have to use the heap ref to get percpu's
        nexus_vspace_lock to protect percpu nexus nodes change
        */
        cas_lock_t nexus_vspace_lock;
        union {
                struct {
                        struct VS_Common* vs;
                        cpu_id_t cpu_id;
                };
                struct {
                        paddr vspace_root_addr;
                        u64 vspace_id;
                        /*
                        The vspace lock is the lock that protect the real page
                        table. Which will be transfer to the map/unmap to
                        protect the page table change.
                        */
                        spin_lock vspace_lock;
                        void* _vspace_node;
                };
        };
} VS_Common;

extern VS_Common nexus_kernel_heap_vs_common;

extern VS_Common* current_vspace; // per cpu pointer
extern VS_Common root_vspace;
extern struct spin_lock_t handler_spin_lock; // per cpu pointer
#define boot_stack_size 0x10000
extern u64 boot_stack_bottom;

VS_Common* new_vspace(void);
error_t vspace_free_last_ref(ref_count_t* refcount);
static inline void init_vspace(VS_Common* vs, u64 vspace_id, void* vspace_node)
{
        vs->vspace_lock = NULL;
        vs->vspace_id = vspace_id;
        vs->_vspace_node = vspace_node;
}
error_t del_vspace(VS_Common** vs);
static inline void set_vspace_root_addr(VS_Common* vs, paddr root_paddr)
{
        vs->vspace_root_addr = root_paddr;
}
static inline void unset_vspace_root_addr(VS_Common* vs)
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

error_t virt_mm_init(cpu_id_t cpu_id, struct setup_info* arch_setup_info);
#endif
