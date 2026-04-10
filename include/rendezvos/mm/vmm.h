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
#include <common/dsa/bitmap.h>
#include <common/dsa/rb_tree.h>
#include <common/refcount.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/limits.h>
#include <rendezvos/sync/spin_lock.h>

#ifndef PTE_SIZE
#define PTE_SIZE 8
#endif

/* One bit per logical CPU in [0, RENDEZVOS_MAX_CPU_NUMBER). */
#define VS_TLB_CPU_MASK_BITS (RENDEZVOS_MAX_CPU_NUMBER)
BITMAP_DEFINE_TYPE(vs_tlb_cpu_bitmap_t, VS_TLB_CPU_MASK_BITS)

/*
 * One type for nexus vs_common + page-table identity (anonymous union: members
 * are as if on VS_Common; interpret only per `type`):
 * - KERNEL_HEAP_REF: per-CPU kmem indirection; `vs` -> shared root (table
 *   vspace); `cpu_id` is allocating CPU for kmem routing.
 * - TABLE_VSPACE: owns page tables / nexus table branch (`vspace_root_addr`,
 *   locks, `_vspace_node`). Used for the global kernel root (`root_vspace`) and
 *   for each per-task address space from `new_vspace()` (user TTBR0 root).
 * Per-CPU heap-ref object: `nexus_kernel_heap_vs_common`.
 */
enum vs_common_kind {
        VS_COMMON_NONE = 0,
        VS_COMMON_KERNEL_HEAP_REF,
        VS_COMMON_TABLE_VSPACE,
};

typedef struct VS_Common {
        u64 type;
        /*
         * Physical memory policy/affinity for this address space.
         * For now it is a single PMM pointer (typically ZONE_NORMAL).
         * Future NUMA: this can evolve into a "mem policy" object or per-region
         * routing, but storing it here keeps nexus decoupled from map_handler.
         */
        struct pmm* pmm;
        /*
         * CPU affinity / ownership id (meaning depends on `type`):
         * - VS_COMMON_KERNEL_HEAP_REF: which CPU this per-CPU heap-ref belongs
         * to
         * - VS_COMMON_TABLE_VSPACE: which per-CPU nexus_root currently owns
         * this vspace (when it is a table vspace)
         */
        cpu_id_t cpu_id;
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
         * Table vspace: protects that vspace’s nexus subtree and PTE paths
         * under one lock (per address space). Shared `root_vspace` uses this
         * for kernel page-table serialization (SMP). Per-CPU `KERNEL_HEAP_REF`
         * has its own `nexus_vspace_lock` for that CPU’s kernel nexus RB tree
         * only.
         */
        cas_lock_t nexus_vspace_lock;
        union {
                struct {
                        struct VS_Common* vs;
                };
                struct {
                        paddr vspace_root_addr;
                        u64 vspace_id;
                        /* AArch64: Address Space Identifier for TTBR0. */
                        asid_t asid;
                        u16 _asid_pad;
                        /*
                         * CPUs that may have live TLB entries for this ASID.
                         * With the "no-IPI" scheme we clear a CPU's bit when it
                         * switches away from this vspace after doing a local
                         * TLBI ASIDE1(asid). Teardown waits for this mask to
                         * become 0 before freeing PT frames and recycling ASID.
                         */
                        vs_tlb_cpu_bitmap_t tlb_cpu_mask;
                        cas_lock_t tlb_cpu_mask_lock;
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

static inline bool vs_common_is_heap_ref(const VS_Common* vs)
{
        return vs && vs->type == (u64)VS_COMMON_KERNEL_HEAP_REF;
}

static inline bool vs_common_is_table_vspace(const VS_Common* vs)
{
        return vs && vs->type == (u64)VS_COMMON_TABLE_VSPACE;
}

static inline void vs_tlb_cpu_mask_zero(VS_Common* vs)
{
        if (!vs_common_is_table_vspace(vs))
                return;
        BITMAP_OPS(vs_tlb_cpu_bitmap, zero)(&vs->tlb_cpu_mask);
}
static inline void vs_tlb_cpu_mask_set(VS_Common* vs, u64 cpu)
{
        if (!vs_common_is_table_vspace(vs)
            || cpu >= (u64)RENDEZVOS_MAX_CPU_NUMBER)
                return;
        BITMAP_OPS(vs_tlb_cpu_bitmap, set)(&vs->tlb_cpu_mask, (u32)cpu);
}
static inline void vs_tlb_cpu_mask_clear(VS_Common* vs, u64 cpu)
{
        if (!vs_common_is_table_vspace(vs)
            || cpu >= (u64)RENDEZVOS_MAX_CPU_NUMBER)
                return;
        BITMAP_OPS(vs_tlb_cpu_bitmap, clear)(&vs->tlb_cpu_mask, (u32)cpu);
}
static inline bool vs_tlb_cpu_mask_is_zero(const VS_Common* vs)
{
        if (!vs_common_is_table_vspace(vs))
                return true;
        return BITMAP_OPS(vs_tlb_cpu_bitmap, is_zero)(&vs->tlb_cpu_mask);
}

extern VS_Common nexus_kernel_heap_vs_common;

extern VS_Common* current_vspace; // per cpu pointer
extern VS_Common root_vspace;
#define boot_stack_size 0x10000
extern u64 boot_stack_bottom;

VS_Common* new_vspace(void);
error_t free_vspace_ref(ref_count_t* refcount);
static inline void init_vspace(VS_Common* vs, u64 vspace_id, void* vspace_node)
{
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
