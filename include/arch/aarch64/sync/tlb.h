#ifndef _SHAMPOOS_TLB_H_
#define _SHAMPOOS_TLB_H_

#include <common/types.h>
#include <common/mm.h>
#include "barrier.h"
static inline void arch_tlb_invalidate_all(){
        //TODO:dsb
        asm volatile(
                "tlbi alle1;"
        );
        //TODO:dsb
        //TODO:isb
}
static inline void arch_tlb_invalidate_page(uint64_t vspace_id, vaddr addr)
{
        uint64_t tmp=(vspace_id << 16)|(addr>>12);
        //TODO:dsb
        asm volatile(
                "tlbi vae1,%0;"
                :
                :"r"(tmp)
        );
        //TODO:dsb
        //TODO:isb
}
static inline void arch_tlb_invalidate_kernel_page(vaddr addr){
        uint64_t tmp=(addr>>12);
        //TODO:dsb
        asm volatile(
                "tlbi vae1,%0;"
                :
                :"r"(tmp)
        );
        //TODO:dsb
        //TODO:isb
}
static inline void arch_tlb_invalidate_vspace_page(uint64_t vspace_id,vaddr addr){
        if(vspace_id>=(1<<16))
                return;
        uint64_t tmp=(vspace_id<<48);
        //TODO:dsb
        asm volatile(
                "tlbi aside1, %0;"
                :
                :"r"(tmp)
        );
        //TODO:dsb
        //TODO:isb
}

static inline void arch_tlb_invalidate_range(u_int64_t vspace_id,vaddr start, vaddr end)
{
        if(vspace_id>=(1<<16))
                return;
        //TODO:dsb
        /*the tlbi rvae1 is only supported when ARMv8.4 TLBI is implemented*/
        for (vaddr addr = start; addr < end; addr += PAGE_SIZE) {
                uint64_t tmp=(vspace_id << 16)|(addr>>12);
                asm volatile(
                        "tlbi vae1,%0;"
                        :
                        :"r"(tmp)
                );
        }
        //TODO:dsb
        //TODO:isb
}
#endif