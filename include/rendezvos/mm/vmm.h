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
#include <rendezvos/mm/tlb_cpu_mask.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/limits.h>
#include <rendezvos/sync/spin_lock.h>

#ifndef PTE_SIZE
#define PTE_SIZE 8
#endif

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
         * routing, but storing it here keeps radix tree decoupled from
         * map_handler.
         */
        struct pmm* pmm;
        /*
         * Reference count for vspace lifetime.
         * - User thread: after create_vspace / clone / CLONE_VM get,
         *   create_thread or copy_thread takes ownership of that live ref
         *   onto thread->vs (no extra get). Kernel threads: caller gets
         *   root_vspace first (gen_thread_from_func), then same transfer;
         *   teardown put matches (boot keeps the base ref from ref_init).
         * - schedule may hold an extra CPU ref while current_vspace points at
         *   a user AS (get when switching in another user AS — incl. root→
         *   first user / leftover A→B; put on switch-away from non-root).
         *   User→kernel does not drop that extra / clear mask.
         * - Kernel vspace(root) is always exist during the system running time.
         * We init it to 1, and the kernel thread only get/put, but the ref
         * is always > 1.
         * Last ownership put typically runs free_vspace_ref(): unregister
         * then del_vspace() .
         */
        ref_count_t refcount;
        /*
         * CPUs that may have live TLB entries for this ASID.
         * schedule: set on switch-in, local TLBI + clear on switch-away.
         * x86 map/unmap: IPI only CPUs still in this mask.
         * Teardown waits for mask == 0 before freeing PT / recycling ASID.
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

/**
 * @brief Allocate a user VSpace: structure, empty L0 root, radix + shared
 *        kernel high-half install.
 * @param pmm Zone pmm stored on the VSpace (typically ZONE_NORMAL).
 * @return New VSpace*, or NULL on failure (with ASID / root / radix rolled
 * back).
 * @note Caller must @ref register_vspace after create (user spaces).
 */
VSpace* create_vspace(struct pmm* pmm);

/**
 * @brief Clone user VSpace from @p src_vs into a new VSpace.
 *
 * @param src_vs      Source (must be a user space with radix).
 * @param dst_vs_out  Out: new VSpace* on success.
 * @param flags       Must include @c VSPACE_CLONE_F_USER_4K_ONLY, and
 *                    exactly one of @c VSPACE_CLONE_F_COW_PREP or
 *                    @c VSPACE_CLONE_F_COPY_PAGES.
 * @return @c REND_SUCCESS or negative error_t; on failure no live dst.
 */
error_t clone_vspace(VSpace* src_vs, VSpace** dst_vs_out,
                     enum vspace_clone_flags flags);

/**
 * @brief Insert @p vs into @p root_vs RB registry (sets registered + root_vs).
 * @note Call after create/clone for user spaces; requires unregistered @p vs.
 */
error_t register_vspace(VSpace* vs, VSpace* root_vs);

/**
 * @brief Refcount destructor for @c VSpace::refcount.
 * Order: @c unregister_vspace ( @p vs ) then @c del_vspace ( @p &vs ).
 * @return Result of @c del_vspace.
 */
error_t free_vspace_ref(ref_count_t* refcount);

/**
 * @brief Remove @p vs from its root RB tree; idempotent if already
 * unregistered.
 * @note Call before @ref del_vspace , or let @c free_vspace_ref do it(for
 * defensive).
 */
error_t unregister_vspace(VSpace* vs);

/**
 * @brief Tear down a VSpace after last ref: clear user mappings, destroy
 *        radix, free root PT frame / struct / ASID. Sets *@p vs to NULL.
 *
 * @return @c REND_SUCCESS; @c -E_IN_PARAM if still @c registered; clear/radix
 *         errors (notably @c -E_REND_RC_UNEQUAL if @c tlb_cpu_mask non-zero).
 *
 * @note Must be unregistered first. 
 * @note No-op success if *@p vs is NULL or is root vspace.
 */
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
/** @brief Encode portable ENTRY_FLAGS into arch PTE bits for @p entry_level.
 */
ARCH_PFLAGS_t arch_decode_flags(int entry_level, ENTRY_FLAGS_t ENTRY_FLAGS);
/** @brief Decode arch PTE bits back to portable ENTRY_FLAGS.
 */
ENTRY_FLAGS_t arch_encode_flags(int entry_level, ARCH_PFLAGS_t ARCH_PFLAGS);

/**
 * @brief Per-CPU virtual MM bring-up after @c phy_mm_init.
 *
 * BSP also prepares the shared map window, ASID, and root VSpace; APs only
 * initialize their @c Map_Handler. Both set @c current_vspace to
 * @c &root_vspace and run @c kinit. AP requires @p arch_setup_info for the
 * boot stack.
 *
 * @param cpu_id            This CPU's id.
 * @param arch_setup_info   Required on AP for stack; BSP may ignore for stack.
 * @return @c REND_SUCCESS or negative error_t.
 */
error_t virt_mm_init(cpu_id_t cpu_id, struct setup_info* arch_setup_info);
#endif
