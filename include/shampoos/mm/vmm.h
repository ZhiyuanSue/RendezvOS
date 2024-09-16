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

void arch_set_L0_entry(paddr ppn, vaddr vpn, union L0_entry* pt_addr,
                       u64 flags);
void arch_set_L1_entry(paddr ppn, vaddr vpn, union L1_entry* pt_addr,
                       u64 flags);
void arch_set_L1_entry_huge(paddr ppn, vaddr vpn, union L1_entry_huge* pt_addr,
                            u64 flags);
void arch_set_L2_entry(paddr ppn, vaddr vpn, union L2_entry* pt_addr,
                       u64 flags);
void arch_set_L2_entry_huge(paddr ppn, vaddr vpn, union L2_entry_huge* pt_addr,
                            u64 flags);
void arch_set_L3_entry(paddr ppn, vaddr vpn, union L3_entry* pt_addr,
                       u64 flags);
/*use those functions to set page entry flags for every page entry*/
u64 arch_decode_flags(int entry_level, u64 ENTRY_FLAGS);
u64 arch_encode_flags(int entry_level, u64 ARCH_PFLAGS);

#define map_pages 0xFFFFFFFFFFE00000
/*
        we use last 2M page as the set of the map used pages virtual addr
        we use one 4K page during the mapping stage
        but consider the multi-core, we use last 2M as this per cpu map page set
        and we think we should not have more than 512 cores
*/
void init_map();
/*kernel might try to mapping one page to a different vspace*/
error_t map(paddr vspace_root_paddr, u64 ppn, u64 vpn, int level);
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