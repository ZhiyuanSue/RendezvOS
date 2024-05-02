#ifndef	_SHAMPOOS_X86_SYS_CTRL_H_
#define _SHAMPOOS_X86_SYS_CTRL_H_
#include <common/types.h>
#include <arch/x86_64/desc.h>
/*cpuid*/
struct cpuid_result
{
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
};

static inline struct cpuid_result cpuid(u32 cpuid_op)
{
	struct cpuid_result tmp_result;
	asm volatile (
		"cpuid"
		:"=a"(tmp_result.eax),"=b"(tmp_result.ebx),"=c"(tmp_result.ecx),"=d"(tmp_result.edx)
		:"a"(cpuid_op)
	);
	return tmp_result;
}
#define CR0_PE	(1<<0)
#define CR0_MP	(1<<1)
#define CR0_EM	(1<<2)
#define CR0_TS	(1<<3)
#define CR0_ET	(1<<4)
#define CR0_NE	(1<<5)
#define	CR0_WP	(1<<16)
#define CR0_AM	(1<<18)
#define	CR0_NW	(1<<29)
#define	CR0_CD	(1<<30)
#define	CR0_PG	(1<<31)
static void inline set_cr0_bit(u64 cr0_bit){
	u64 cr0_tmp;
	asm volatile (
		"movq %%cr0,%0;"
		"orq %1,%0;"
		"movq %0,%%cr0;"
		:"=&r"(cr0_tmp)
		:"r"(cr0_bit)
	);
}
#define	CR3_PWT	(1<<3)
#define	CR3_PCD	(1<<4)
static void inline set_cr3_bit(u64 cr3_bit){
	u64 cr3_tmp;
	asm volatile (
		"movq %%cr3,%0;"
		"orq %1,%0;"
		"movq %0,%%cr3;"
		:"=&r"(cr3_tmp)
		:"r"(cr3_bit)
	);
}
#define	CR4_VME	(1<<0)
#define	CR4_PVI	(1<<1)
#define	CR4_TSD	(1<<2)
#define	CR4_DE	(1<<3)
#define	CR4_PSE	(1<<4)
#define	CR4_PAE	(1<<5)
#define	CR4_MCE	(1<<6)
#define	CR4_PGE	(1<<7)
#define	CR4_PCE	(1<<8)
#define	CR4_OSFXSR	(1<<9)
#define	CR4_OSXMMEXCPT	(1<<10)
#define	CR4_UMIP	(1<<11)
#define	CR4_VMXE	(1<<13)
#define	CR4_SMXE	(1<<14)
#define	CR4_FSGSBASE	(1<<16)
#define	CR4_PCIDE	(1<<17)
#define	CR4_OSXSAVE	(1<<18)
#define	CR4_SMEP	(1<<20)
#define	CR4_SMAP	(1<<21)
#define	CR4_PKE		(1<<22)
static void inline set_cr4_bit(u64 cr4_bit){
	u64 cr4_tmp;
	asm volatile (
		"movq %%cr4,%0;"
		"orq %1,%0;"
		"movq %0,%%cr4;"
		:"=&r"(cr4_tmp)
		:"r"(cr4_bit)
	);
}

#define	XCR0_X87	(1<<0)
#define	XCR0_SSE	(1<<1)
#define	XCR0_AVX	(1<<2)
#define	XCR0_BNDREG	(1<<3)
#define	XCR0_BNDCSR	(1<<4)
#define	XCR0_OPMASK	(1<<5)
#define	XCR0_ZMM_HI256	(1<<6)
#define	XCR0_HI16_ZMM	(1<<7)
#define	XCR0_PKRU	(1<<9)
static void inline set_xcr(u32 xcr_number,u64 xcr_value)
{
	u32 xcr_low = (u32)xcr_value;
	u32 xcr_high = (u32)(xcr_value >> 32);
	asm volatile(
		"xsetbv"
		:
		:"a"(xcr_low),"c"(xcr_number),"d"(xcr_high)
	);
}
static u64 inline get_xcr(u32 xcr_number)
{
	u64 xcr_value;
	u32	xcr_high;
	u32 xcr_low;
	asm volatile(
		"xgetbv"
		:"=a"(xcr_low),"=d"(xcr_high)
		:"c"(xcr_number)
	);
	xcr_value=(((u64)xcr_high)<<32)|xcr_low;
	return xcr_value;
}

#define	MXCSR_IE	(1<<0)
#define	MXCSR_DE	(1<<1)
#define	MXCSR_ZE	(1<<2)
#define	MXCSR_OE	(1<<3)
#define	MXCSR_UE	(1<<4)
#define	MXCSR_PE	(1<<5)
#define	MXCSR_DAZ	(1<<6)
#define	MXCSR_IM	(1<<7)
#define	MXCSR_DM	(1<<8)
#define	MXCSR_ZM	(1<<9)
#define	MXCSR_OM	(1<<10)
#define	MXCSR_UM	(1<<11)
#define	MXCSR_PM	(1<<12)
#define	MXCSR_FZ	(1<<16)
static void inline set_mxcsr(u32 mxcsr_value)	/*not bits*/
{
	asm volatile(
		"ldmxcsr	%0"
		:
		:"m"(mxcsr_value)
	);
}
static u32 inline get_mxcsr()
{
	u32	mxcsr_tmp;
	asm volatile(
		"stmxcsr	%0"
		:"=m"(mxcsr_tmp)
	);
	return mxcsr_tmp;
}
static void inline lidt(struct pseudo_descriptor* desc)
{
	asm volatile(
		"lidt	(%0)"
		:
		:"r"(desc)
		:"memory"
	);
}
#endif