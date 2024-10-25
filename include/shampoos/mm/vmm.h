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

#endif