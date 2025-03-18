#ifndef _RENDEZVOS_SYS_CTRL_H_
#define _RENDEZVOS_SYS_CTRL_H_
#include <arch/x86_64/desc.h>
#include <arch/x86_64/trap/tss.h>
#include <common/types.h>
// #include <arch/x86_64/sys_ctrl_def.h>
// /*cpuid*/
// struct cpuid_result {
// 	u32 eax;
// 	u32 ebx;
// 	u32 ecx;
// 	u32 edx;
// };

// static inline struct cpuid_result cpuid(u32 cpuid_op) {
// 	struct cpuid_result tmp_result;
// 	__asm__ __volatile__("cpuid"
// 					: "=a"(tmp_result.eax),
// "=b"(tmp_result.ebx),
// 					"=c"(tmp_result.ecx),
// "=d"(tmp_result.edx) 					:
// "a"(cpuid_op)); return (tmp_result);
// }

static void inline set_cr0_bit(u64 cr0_bit)
{
        u64 cr0_tmp;

        __asm__ __volatile__("movq %%cr0,%0;"
                             "orq %1,%0;"
                             "movq %0,%%cr0;"
                             : "=&r"(cr0_tmp)
                             : "r"(cr0_bit));
}

static void inline set_cr3_bit(u64 cr3_bit)
{
        u64 cr3_tmp;

        __asm__ __volatile__("movq %%cr3,%0;"
                             "orq %1,%0;"
                             "movq %0,%%cr3;"
                             : "=&r"(cr3_tmp)
                             : "r"(cr3_bit));
}

static void inline set_cr4_bit(u64 cr4_bit)
{
        u64 cr4_tmp;

        __asm__ __volatile__("movq %%cr4,%0;"
                             "orq %1,%0;"
                             "movq %0,%%cr4;"
                             : "=&r"(cr4_tmp)
                             : "r"(cr4_bit));
}

static void inline set_xcr(u32 xcr_number, u64 xcr_value)
{
        u32 xcr_low;
        u32 xcr_high;

        xcr_low = (u32)xcr_value;
        xcr_high = (u32)(xcr_value >> 32);
        __asm__ __volatile__("xsetbv"
                             :
                             : "a"(xcr_low), "c"(xcr_number), "d"(xcr_high));
}
static u64 inline get_xcr(u32 xcr_number)
{
        u64 xcr_value;
        u32 xcr_high;
        u32 xcr_low;

        __asm__ __volatile__("xgetbv"
                             : "=a"(xcr_low), "=d"(xcr_high)
                             : "c"(xcr_number));
        xcr_value = (((u64)xcr_high) << 32) | xcr_low;
        return (xcr_value);
}

static void inline set_mxcsr(u32 mxcsr_value) /*not bits*/
{
        __asm__ __volatile__("ldmxcsr	%0" : : "m"(mxcsr_value));
}
static u32 inline get_mxcsr()
{
        u32 mxcsr_tmp;

        __asm__ __volatile__("stmxcsr	%0" : "=m"(mxcsr_tmp));
        return (mxcsr_tmp);
}
static void inline lgdt(struct pseudo_descriptor *desc)
{
        __asm__ __volatile__("lgdt      (%0)" : : "r"(desc) : "memory");
}
static void inline lidt(struct pseudo_descriptor *desc)
{
        __asm__ __volatile__("lidt	(%0)" : : "r"(desc) : "memory");
}
static void inline ltr(union desc_selector *selector)
{
        __asm__ __volatile__("ltr       %0" : : "r"(*selector) : "memory");
}

static u64 inline rdmsr(u32 msr_id)
{
        u64 val;

        __asm__ __volatile__("rdmsr" : "=A"(val) : "c"(msr_id));
        return (val);
}
static void inline wrmsr(u32 msr_id, u64 val)
{
        __asm__ __volatile__("wrmsr" ::"c"(msr_id), "A"(val));
}
static void inline cli(void)
{
        __asm__ __volatile__("cli");
}
static void inline sti(void)
{
        __asm__ __volatile__("sti");
}
#endif