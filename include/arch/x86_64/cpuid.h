#ifndef _SHAMPOOS_X86_CPUID_H_
#define _SHAMPOOS_X86_CPUID_H_
#include <common/types.h>
/*
    A reference website is https://www.felixcloutier.com/x86/cpuid, Thanks for
   that
*/
struct cpuinfo_x86 {
#define X86_CPUID_VENDOR 0x0
    u8 MaxBasicCPUID[ 0x4 ];
    u8 Vendor[ 0xC ];
#define X86_CPUID_FAMILY_MODEL 0x1
    u8  SteppingID;
    u8  Model;
    u8  Family;
    u16 CLFLUSHLineSize;
    u8  APICID;
#define X86_CPUID_FEATURE_ECX_SSE3 (1 << 0)
#define X86_CPUID_FEATURE_ECX_PCLMULQDQ (1 << 1)
#define X86_CPUID_FEATURE_ECX_DTES64 (1 << 2)
#define X86_CPUID_FEATURE_ECX_MONITOR (1 << 3)
#define X86_CPUID_FEATURE_ECX_DS_CPL (1 << 4)
#define X86_CPUID_FEATURE_ECX_VMX (1 << 5)
#define X86_CPUID_FEATURE_ECX_SMX (1 << 6)
#define X86_CPUID_FEATURE_ECX_EIST (1 << 7)
#define X86_CPUID_FEATURE_ECX_TM2 (1 << 8)
#define X86_CPUID_FEATURE_ECX_SSSE3 (1 << 9)
#define X86_CPUID_FEATURE_ECX_CNXT_ID (1 << 10)
#define X86_CPUID_FEATURE_ECX_SDBG (1 << 11)
#define X86_CPUID_FEATURE_ECX_FMA (1 << 12)
#define X86_CPUID_FEATURE_ECX_CMPXCHG16B (1 << 13)
#define X86_CPUID_FEATURE_ECX_xTPR_Update_Control (1 << 14)
#define X86_CPUID_FEATURE_ECX_PDCM (1 << 15)

#define X86_CPUID_FEATURE_ECX_PCID (1 << 17)
#define X86_CPUID_FEATURE_ECX_DCA (1 << 18)
#define X86_CPUID_FEATURE_ECX_SSE4_1 (1 << 19)
#define X86_CPUID_FEATURE_ECX_SSE4_2 (1 << 20)
#define X86_CPUID_FEATURE_ECX_x2APIC (1 << 21)
#define X86_CPUID_FEATURE_ECX_MOVBE (1 << 22)
#define X86_CPUID_FEATURE_ECX_POPCNT (1 << 23)
#define X86_CPUID_FEATURE_ECX_TSC_Deadline (1 << 24)
#define X86_CPUID_FEATURE_ECX_AESNI (1 << 25)
#define X86_CPUID_FEATURE_ECX_XSAVE (1 << 26)
#define X86_CPUID_FEATURE_ECX_OSXSAVE (1 << 27)
#define X86_CPUID_FEATURE_ECX_AVX (1 << 28)
#define X86_CPUID_FEATURE_ECX_F16C (1 << 29)
#define X86_CPUID_FEATURE_ECX_RDRAND (1 << 30)
    u32 feature_1;
#define X86_CPUID_FEATURE_EDX_FPU (1 << 0)
#define X86_CPUID_FEATURE_EDX_VME (1 << 1)
#define X86_CPUID_FEATURE_EDX_DE (1 << 2)
#define X86_CPUID_FEATURE_EDX_PSE (1 << 3)
#define X86_CPUID_FEATURE_EDX_TSC (1 << 4)
#define X86_CPUID_FEATURE_EDX_MSR (1 << 5)
#define X86_CPUID_FEATURE_EDX_PAE (1 << 6)
#define X86_CPUID_FEATURE_EDX_MCE (1 << 7)
#define X86_CPUID_FEATURE_EDX_CX8 (1 << 8)
#define X86_CPUID_FEATURE_EDX_APIC (1 << 9)

#define X86_CPUID_FEATURE_EDX_SEP (1 << 11)
#define X86_CPUID_FEATURE_EDX_MTRR (1 << 12)
#define X86_CPUID_FEATURE_EDX_PGE (1 << 13)
#define X86_CPUID_FEATURE_EDX_MCA (1 << 14)
#define X86_CPUID_FEATURE_EDX_CMOV (1 << 15)
#define X86_CPUID_FEATURE_EDX_PAT (1 << 16)
#define X86_CPUID_FEATURE_EDX_PSE_36 (1 << 17)
#define X86_CPUID_FEATURE_EDX_PSN (1 << 18)
#define X86_CPUID_FEATURE_EDX_CLFSH (1 << 19)

#define X86_CPUID_FEATURE_EDX_DS (1 << 21)
#define X86_CPUID_FEATURE_EDX_ACPI (1 << 22)
#define X86_CPUID_FEATURE_EDX_MMX (1 << 23)
#define X86_CPUID_FEATURE_EDX_FXSR (1 << 24)
#define X86_CPUID_FEATURE_EDX_SSE (1 << 25)
#define X86_CPUID_FEATURE_EDX_SSE2 (1 << 26)
#define X86_CPUID_FEATURE_EDX_SS (1 << 27)
#define X86_CPUID_FEATURE_EDX_HTT (1 << 28)
#define X86_CPUID_FEATURE_EDX_TM (1 << 29)

#define X86_CPUID_FEATURE_EDX_PBE (1 << 31)
    u32 feature_2;
#define X86_CPUID_CACHE 0x2
    u8 cache_tlb_info[ 16 ]; /*not all used, and might in unexpected order,so
                                  check them all*/
#define X86_CPUID_MODEL_NAME_1 0x80000002
#define X86_CPUID_MODEL_NAME_2 0x80000003
#define X86_CPUID_MODEL_NAME_3 0x80000004
    u8 ModelName[ 48 ];
#define X86_CPUID_ADDR 0x80000008
    u8 VirtAddrBits;
    u8 PhyAddrBits;
};
static inline void cpuid(u32 op, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    *eax = op;
    *ecx = 0;
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "0"(*eax), "2"(*ecx)
                 : "memory");
}

#endif