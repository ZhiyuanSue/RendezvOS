#ifndef _SHAMPOOS_MSR_H_
#define _SHAMPOOS_MSR_H_

/*
    we will not use all the msr,and here is one of the reference of msr
    https://sandpile.org/x86/msr.htm
*/
/*IA32_EFER*/
#define IA32_EFER_addr 0xC0000080
#define IA32_EFER_SCE (1 << 0)
#define IA32_EFER_LMR (1 << 8)
#define IA32_EFER_NXE (1 << 11)

/*IA32_APIC_BASE*/
#define IA32_APIC_BASE_addr 0x0000001B
#define IA32_APIC_BASE_phy_addr_MASK (0xffffff << 12)
#define IA32_APIC_BASE_X_ENABLE (1 << 11)
#define IA32_APIC_BASE_X2_ENABLE (1 << 10)
#define IA32_APIC_BASE_BSP (1 << 8)
#endif