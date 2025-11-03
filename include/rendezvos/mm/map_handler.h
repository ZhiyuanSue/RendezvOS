#ifndef _RENDEZVOS_MAP_HANDLER_
#define _RENDEZVOS_MAP_HANDLER_
#include <common/types.h>
#include <common/mm.h>
#include <rendezvos/mm/mm.h>

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
        i64 handler_ppn[4];
        struct pmm* pmm;
        int page_table_zone;
} __attribute__((packed));
extern struct map_handler Map_Handler;
void sys_init_map();
void init_map(struct map_handler* handler, int cpu_id, int pt_zone,
              struct pmm* pmm);
/*
        kernel might try to mapping one page to a different vspace
        and if the vspace is not exist, it should try to alloc a new one
*/

error_t map(VSpace* vs, u64 ppn, u64 vpn, int level, ENTRY_FLAGS_t eflags,
            struct map_handler* handler, spin_lock* lock);
/*
        here we think the vspace root paddr must exist.
        and we expect the vpn and the page number we need to unmap,
        as for the new_entry_addr ,we might store some info in the entry.
        but we only store it in the paddr member,
        because we are not sure about the affect of other bits under different
   arch.
*/
error_t unmap(VSpace* vs, u64 vpn, u64 new_entry_addr,
              struct map_handler* handler, spin_lock* lock);

/*
 * @brief judge one vpn have mapped in one vspace or not
 *
 * @param vs : The address space need to find
 * @param vpn : the vpn need to query
 * @param handler : the map handler that used
 *
 * @return i64 : >0, the ppn that vpn mapped, ==0, not mapped, maybe the value
 * is 0 or present flag is cleared,<0,error,remember that its' ppn
 */
i64 have_mapped(VSpace* vs, u64 vpn, struct map_handler* handler);

/*
        in order to generate a new vspace
        we should use the following function
        but, conside that sometimes we just want to copy the kernel part
        sometimes, we need to copy the kernel and user part
        so we must add a parameter: old_vs_root_paddr
        if it's 0, it's seems as it try to only copy the kernel part
        if it's not 0, we just copy the kernel and user part

        besides,we have to consider that under the aarch64,
        we have ttbr0 and ttbr1,
        so copy kernel part is meaningless here,
        but we also copy it for compatible,

        as for there might have COW(copy on write)
        it's TODO
*/
paddr new_vs_root(paddr old_vs_root_paddr, struct map_handler* handler);

/*
        TODO: we need to add a function to change the page entry's attribute
*/
#endif