#ifndef _RENDEZVOS_VMM_RADIX_TREE_H_
#define _RENDEZVOS_VMM_RADIX_TREE_H_

#include <common/types.h>
#include <common/mm.h>
#include <common/dsa/list.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/error.h>

#define VMM_RADIX_ENTRY_LOCK_OFF  (0)
#define VMM_RADIX_ENTRY_LOCK_MASK BIT_U64(VMM_RADIX_ENTRY_LOCK_OFF)

#define VMM_RADIX_ENTRY_VALID_OFF  (1)
#define VMM_RADIX_ENTRY_VALID_MASK BIT_U64(VMM_RADIX_ENTRY_VALID_OFF)

#define VMM_RADIX_ENTRY_HUGE_OFF  (11)
#define VMM_RADIX_ENTRY_HUGE_MASK BIT_U64(VMM_RADIX_ENTRY_HUGE_OFF)

#define VMM_RADIX_CNT_SHIFT (2)
#define VMM_RADIX_CNT_MASK  (0x3ffULL << VMM_RADIX_CNT_SHIFT)

#define VMM_RADIX_PTR_MASK (0xfffffffffffff000ULL)

typedef struct {
        u64 value;
} Radix_entry_t;

typedef struct {
        ENTRY_FLAGS_t flags;
        struct list_entry rmap_list;
        VS_Common* vs_ptr;
} Radix_node_t;

error_t vmm_radix_tree_insert_range(struct map_handler* handler, VS_Common* vs,
                                    vaddr page_vaddr, ENTRY_FLAGS_t flags,
                                    size_t page_number);

error_t vmm_radix_tree_leaf_bind(VS_Common* vs, vaddr page_vaddr, ppn_t ppn,
                                 struct map_handler* handler,
                                 ENTRY_FLAGS_t leaf_flags);

error_t vmm_radix_tree_leaf_unbind(VS_Common* vs, vaddr page_vaddr,
                                   struct map_handler* handler);

error_t vmm_radix_tree_delete_range(struct map_handler* handler, VS_Common* vs,
                                    vaddr page_vaddr, size_t page_number);

error_t vmm_radix_tree_change_leaf_ppn(VS_Common* vs, vaddr page_vaddr,
                                       ppn_t new_ppn,
                                       struct map_handler* handler,
                                       ENTRY_FLAGS_t leaf_flags);

error_t vmm_radix_tree_change_leaf_ppn_flag(VS_Common* vs, vaddr page_vaddr,
                                            ENTRY_FLAGS_t new_flag,
                                            ppn_t new_ppn,
                                            struct map_handler* handler);

error_t vmm_radix_tree_change_range_flag(VS_Common* vs, vaddr page_vaddr,
                                         ENTRY_FLAGS_t new_flags,
                                         size_t page_number);

error_t vmm_radix_tree_query_leaf(VS_Common* vs, vaddr page_vaddr,
                                  ENTRY_FLAGS_t* out_flags);

Radix_entry_t* vmm_radix_tree_init(struct map_handler* handler, VS_Common* vs);

error_t vmm_radix_tree_destroy(struct map_handler* handler, VS_Common* vs);

error_t
vmm_radix_tree_bootstrap_shared_kernel_high_half(struct map_handler* handler,
                                                 VS_Common* vs);
error_t
vmm_radix_tree_install_shared_kernel_high_half(struct map_handler* handler,
                                               VS_Common* vs);

#endif
