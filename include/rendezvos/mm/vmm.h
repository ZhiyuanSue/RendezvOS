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
typedef struct VSpace VSpace;
struct VSpace {
        /* AArch64: Address Space Identifier for TTBR0. */
        asid_t asid;
        u16 asid_padding;
        bool registered;
        /*
         * Physical memory policy/affinity for this address space.
         * For now it is a single PMM pointer (typically ZONE_NORMAL).
         * Future NUMA: this can evolve into a "mem policy" object or per-region
         * routing, but storing it here keeps radix tree decoupled from map_handler.
         */
        struct pmm* pmm;
        /*
         * Reference count for vspace lifetime.
         * - Owner (task) holds one reference.
         * - CPUs hold active references while CR3/TTBR points to this vspace.
         * - Kernel vspace(root) is always exist during the system running time.
         * Last ref_put runs del_vspace(): radix destroy, user PT reclaim, root
         * frame free, RB unregister, and VSpace struct free.
         */
        ref_count_t refcount;
        u64 vspace_id;
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

        /* L0 radix metadata page (Radix_entry_t*); void* avoids vmm.h ↔ radix
         * hdr. */
        void* root_radix;
        paddr vspace_root_addr;
        union {
                struct {
                        struct list_entry root_manage_list_head;
                        struct rb_root _vspace_rb_root;
                        cas_lock_t vspace_register_lock;
                }; /*root vspace node*/
                struct {
                        struct rb_node _vspace_rb_node;
                        VSpace* root_vs;
                }; /*normal vspace node*/
        };
};

enum vspace_clone_flags {
        /*must be set, for the kernel pages allow the 2M，but the user only
           allow 4K*/
        VSPACE_CLONE_F_USER_4K_ONLY = (1ULL << 0),
        /*the whole page table tree is copied,only L3 entry shared*/
        VSPACE_CLONE_F_COW_PREP = (1ULL << 1),
        /*the L3 entry point to new pages*/
        VSPACE_CLONE_F_COPY_PAGES = (1ULL << 2),
};

static inline void vs_tlb_cpu_mask_zero(VSpace* vs)
{
        BITMAP_OPS(vs_tlb_cpu_bitmap, zero)(&vs->tlb_cpu_mask);
}
static inline void vs_tlb_cpu_mask_set(VSpace* vs, cpu_id_t cpu_id)
{
        if (cpu_id >= (u64)RENDEZVOS_MAX_CPU_NUMBER)
                return;
        BITMAP_OPS(vs_tlb_cpu_bitmap, set)(&vs->tlb_cpu_mask, (u32)cpu_id);
}
static inline void vs_tlb_cpu_mask_clear(VSpace* vs, cpu_id_t cpu_id)
{
        if (cpu_id >= (u64)RENDEZVOS_MAX_CPU_NUMBER)
                return;
        BITMAP_OPS(vs_tlb_cpu_bitmap, clear)(&vs->tlb_cpu_mask, (u32)cpu_id);
}
static inline bool vs_tlb_cpu_mask_is_zero(const VSpace* vs)
{
        return BITMAP_OPS(vs_tlb_cpu_bitmap, is_zero)(&vs->tlb_cpu_mask);
}

extern VSpace* current_vspace; // per cpu pointer
extern VSpace root_vspace;
#define boot_stack_size 0x10000
extern u64 boot_stack_bottom;

error_t init_root_vspace(VSpace* root_vs, cpu_id_t cpu_id);
VSpace* create_vspace(struct pmm* pmm);
error_t clone_vspace(VSpace* src_vs, VSpace** dst_vs_out,
                     enum vspace_clone_flags flags);
/*remember register the vspace after create/clone user vspace*/
error_t register_vspace(VSpace* vs, VSpace* root_vs, u64 vspace_id);
error_t free_vspace_ref(ref_count_t* refcount);
error_t unregister_vspace(VSpace* vs);
/*remember unregister the vs before del vspace*/
error_t del_vspace(VSpace** vs);

struct map_handler;

/*
 * Clear user low-half mappings (radix clean_user + vspace_free_user_pt).
 *
 * Keeps: VSpace object, ASID, registration, page-table root frame, L0 radix
 * page, kernel high-half PTE and L0[256..511] radix slots. Use for in-place
 * exec before load_elf_to_vs.
 *
 * del_vspace (after unregister_vspace) calls this, then vmm_radix_tree_delete,
 * then vspace_free_root_page, then frees the VSpace / ASID.
 *
 * Caller obligations (Linux/exec policy stays above core):
 * - No other thread in the owning task may still run on this @p vs.
 * - @p allow_self_use: if true (in-place exec), only remote CPUs may have
 *   tlb_cpu_mask bits set; this CPU may retain its bit when current_vspace==vs.
 *   If false (del_vspace), vs_tlb_cpu_mask must be zero on all CPUs.
 * - @p vs must not be root_vspace.
 */
error_t vspace_clear_user_mappings(VSpace* vs, struct map_handler* handler,
                                   bool allow_self_use);

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
