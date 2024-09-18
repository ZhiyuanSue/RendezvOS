#ifndef _SHAMPOOS_VMM_H_
#define _SHAMPOOS_VMM_H_
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

#define mask_9_bit     0x1ff
#define L0_INDEX(addr) ((addr >> 39) & mask_9_bit)
#define L1_INDEX(addr) ((addr >> 30) & mask_9_bit)
#define L2_INDEX(addr) ((addr >> 21) & mask_9_bit)
#define L3_INDEX(addr) ((addr >> 12) & mask_9_bit)

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

#define map_pages 0xFFFFFFFFFFE00000
/*
        we use last 2M page as the set of the map used pages virtual addr
        we use one 4K page during the mapping stage
        but consider the multi-core, we use last 2M as this per cpu map page set
        and we think we should not have more than 512 cores
*/
void init_map();
/*kernel might try to mapping one page to a different vspace*/
error_t map(paddr* vspace_root_paddr, u64 ppn, u64 vpn, int level,
            struct pmm pmm);
#define MM_COMMON                       \
        void (*init)(struct pmm * pmm); \
        void* (*m_alloc)(size_t Bytes); \
        void (*m_free)(void* p)
struct allocator {
        MM_COMMON;
};

struct vma_struct {
        struct mm_struct* mm;
        struct rb_node vma_rb;
};
struct mm_struct {
        struct rb_root mm_root;
};

struct mm_struct* create_mm();
struct vma_struct* create_vma();
void insert_vma(struct mm_struct* mm, struct vma_struct* vma);
#endif