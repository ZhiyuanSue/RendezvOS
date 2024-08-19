#ifndef _SHAMPOOS_X86_SYS_CTRL_H_
#define _SHAMPOOS_X86_SYS_CTRL_H_
#include <arch/x86_64/desc.h>
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
// 	asm volatile("cpuid"
// 				 : "=a"(tmp_result.eax), "=b"(tmp_result.ebx),
// 				   "=c"(tmp_result.ecx), "=d"(tmp_result.edx)
// 				 : "a"(cpuid_op));
// 	return tmp_result;
// }

static void inline set_cr0_bit(u64 cr0_bit) {
	u64 cr0_tmp;
	asm volatile(
		"movq %%cr0,%0;"
		"orq %1,%0;"
		"movq %0,%%cr0;"
		: "=&r"(cr0_tmp)
		: "r"(cr0_bit));
}

static void inline set_cr3_bit(u64 cr3_bit) {
	u64 cr3_tmp;
	asm volatile(
		"movq %%cr3,%0;"
		"orq %1,%0;"
		"movq %0,%%cr3;"
		: "=&r"(cr3_tmp)
		: "r"(cr3_bit));
}

static void inline set_cr4_bit(u64 cr4_bit) {
	u64 cr4_tmp;
	asm volatile(
		"movq %%cr4,%0;"
		"orq %1,%0;"
		"movq %0,%%cr4;"
		: "=&r"(cr4_tmp)
		: "r"(cr4_bit));
}

static void inline set_xcr(u32 xcr_number, u64 xcr_value) {
	u32 xcr_low = (u32)xcr_value;
	u32 xcr_high = (u32)(xcr_value >> 32);
	asm volatile("xsetbv" : : "a"(xcr_low), "c"(xcr_number), "d"(xcr_high));
}
static u64 inline get_xcr(u32 xcr_number) {
	u64 xcr_value;
	u32 xcr_high;
	u32 xcr_low;
	asm volatile("xgetbv" : "=a"(xcr_low), "=d"(xcr_high) : "c"(xcr_number));
	xcr_value = (((u64)xcr_high) << 32) | xcr_low;
	return xcr_value;
}

static void inline set_mxcsr(u32 mxcsr_value) /*not bits*/
{
	asm volatile("ldmxcsr	%0" : : "m"(mxcsr_value));
}
static u32 inline get_mxcsr() {
	u32 mxcsr_tmp;
	asm volatile("stmxcsr	%0" : "=m"(mxcsr_tmp));
	return mxcsr_tmp;
}
static void inline lidt(struct pseudo_descriptor *desc) {
	asm volatile("lidt	(%0)" : : "r"(desc) : "memory");
}
#endif