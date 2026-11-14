#include <arch/x86_64/thread_arch.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <common/types.h>

/* GCC "m"/"=m" needs a sized object; a bare u8* only covers 1 byte. */
#define ARCH_FXSAVE_M(p) (*(u8(*)[ARCH_FP_SIZE])(p))

void arch_fp_save_to(void* buf)
{
        if (!buf)
                return;
        __asm__ __volatile__("fxsave64 %0"
                             : "=m"(ARCH_FXSAVE_M(buf))
                             :
                             : "memory");
}

void arch_fp_load_from(const void* buf)
{
        if (!buf)
                return;
        __asm__ __volatile__("fxrstor64 %0"
                             :
                             : "m"(ARCH_FXSAVE_M(buf))
                             : "memory");
}

void arch_fp_init_area(u8* area)
{
        if (!area)
                return;
        *(u16*)(area + ARCH_X86_FXSAVE_FCW_OFF) = FCW_DEFAULT;
        *(u32*)(area + ARCH_X86_FXSAVE_MXCSR_OFF) = MXCSR_DEFAULT;
}
