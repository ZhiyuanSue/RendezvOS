#ifndef _RENDEZVOS_MAP_HANDLER_
#define _RENDEZVOS_MAP_HANDLER_
#include <common/types.h>
#include <common/mm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/smp/cpu_id.h>
#include <rendezvos/sync/spin_lock.h>

#define map_pages 0xFFFFFFFFFFE00000
/*
        we use last 2M page as the set of the map used pages virtual addr
        we use one 4K page during the mapping stage
        but consider the multi-core, we use last 2M as this per cpu map page set
        and we think we should not have more than 512 cores
*/
struct map_handler {
        cpu_id_t cpu_id;
        vaddr map_vaddr[4];
        ppn_t handler_ppn[4];
        struct pmm* pmm;
        spin_lock_t vspace_lock_node;
};
extern struct map_handler Map_Handler;
void sys_init_map(struct pmm* pmm);
void init_map(struct map_handler* handler, cpu_id_t cpu_id, struct pmm* pmm);
/*
        kernel might try to mapping one page to a different vspace
        and if the vspace is not exist, it should try to alloc a new one
*/

error_t map(VS_Common* vs, ppn_t ppn, vpn_t vpn, int level,
            ENTRY_FLAGS_t eflags, struct map_handler* handler);
/*
 * @brief : unmap the vpn with that it mapped ppn
 *
 * @param vs : vspace this unmap happen
 * @param vpn : the vpn in this vspace
 * @param new_entry_addr : if you want to unmap the ppn and build a new map to
 * new_entry_addr(new ppn), use this param
 * @param hanlder : the map handler
 * @param lock : lock the vspace's data
 * @return i64 : if success, return the ppn(>0) it have mapped, if fail return a
 * <0 error value
 */
ppn_t unmap(VS_Common* vs, vpn_t vpn, u64 new_entry_addr,
            struct map_handler* handler);

/*
 * @brief judge one vpn have mapped in one vspace or not
 *
 * @param vs : The address space need to find
 * @param vpn : the vpn need to query
 * @param entry_flags_out : optional encoded flags for the final matched entry
 * @param entry_level_out : optional final matched entry level (2 for huge, 3
 * for 4K)
 * @param handler : the map handler that used
 *
 * @return i64 : >0, the ppn that vpn mapped, ==0, not mapped, maybe the value
 * is 0 or present flag is cleared,<0,error,remember that its' ppn
 */
ppn_t have_mapped(VS_Common* vs, vpn_t vpn, ENTRY_FLAGS_t* entry_flags_out,
                  int* entry_level_out, struct map_handler* handler);

/*
 * @brief Temporarily map a physical page into a per-CPU handler
 * mapping window.
 *
 * @param handler  Per-CPU map handler.
 * @param slot_id     Window slot index in [0, 4). Caller is responsible for
 *                 avoiding conflicts with concurrent map/unmap/have_mapped
 *                 usage on the same handler.
 * @param ppn
 *
 * @return Kernel virtual address for the mapped page, or 0 on error.
 */
vaddr map_handler_map_slot(struct map_handler* handler, int slot_id, ppn_t ppn);

/*
 * @brief Invalidate the mapping window slot after use.
 *
 * @param handler  Per-CPU map handler.
 * @param slot_id  Window slot index in [0, 4).
 */
void map_handler_unmap_slot(struct map_handler* handler, int slot_id);

/*
 * @brief Copy a physical memory range using the per-CPU mapping window.
 *
 * This is a low-level helper for COW split / vspace clone / page migration.
 * It does not assume a permanent phys->kva mapping.
 *
 * @param handler   Per-CPU map handler.
 * @param dst_paddr Destination physical address (may be unaligned).
 * @param src_paddr Source physical address (may be unaligned).
 * @param len       Bytes to copy.
 *
 * @return REND_SUCCESS on success; negative error code on failure.
 */
error_t map_handler_copy_data_range(struct map_handler* handler,
                                     paddr dst_paddr,
                                     paddr src_paddr,
                                     u64 len);

static inline error_t map_handler_copy_page(struct map_handler* handler,
                                                ppn_t dst_ppn,
                                                ppn_t src_ppn)
{
        return map_handler_copy_data_range(handler,
                                            PADDR(dst_ppn),
                                            PADDR(src_ppn),
                                            PAGE_SIZE);
}

/*
 * @brief Allocate a new top-level (L0) page-table root for a table vspace.
 *
 * Layering: this is a **low-level** primitive. It always seeds the **kernel
 * half** of the L0 page from the shared template (`L0_table`). The **user
 * half** (lower half of that same physical page in this layout) is either
 * zeroed or **shallow-copied** from another root — see below.
 *
 * @param old_vs_root_paddr  Physical address of an **existing** L0 root to
 *                           copy from, or 0.
 *   - **0**: `memset` the user half of the new L0 to empty (typical new user
 *     address space before nexus/map fills it).
 *   - **Non-0**: `memcpy` the **user half of L0 only** (first-level entries)
 *     from that root into the new root. This duplicates **pointers** into the
 *     same lower-level page tables as the source; it is **not** a full
 *     vspace clone and does **not** by itself establish COW or nexus truth.
 *     Upper layers (e.g. `vspace_clone` + nexus) own fork/COW semantics.
 *
 * @param handler  Per-CPU map handler (must match the CPU doing the alloc).
 *
 * @return Physical address of the new L0 root, or 0 on failure.
 *
 * AArch64 note: TTBR0/TTBR1 split means “kernel half” here is layout/policy;
 * we still fill it for a uniform API across architectures.
 */
paddr new_vs_root(paddr old_vs_root_paddr, struct map_handler* handler);
error_t vspace_free_user_pt(VS_Common* vs, struct map_handler* handler);
error_t vspace_free_root_page(VS_Common* vs, struct map_handler* handler);

/*
        TODO: we need to add a function to change the page entry's attribute
*/
#endif
