#ifndef _RENDEZVOS_MSR_H_
#define _RENDEZVOS_MSR_H_

/*
    we will not use all the msr,and here is one of the reference of msr
    https://sandpile.org/x86/msr.htm
*/
/*IA32_EFER*/
#define IA32_EFER_addr 0xC0000080
#define IA32_EFER_SCE  (1 << 0)
#define IA32_EFER_LMR  (1 << 8)
#define IA32_EFER_NXE  (1 << 11)

/*IA32_APIC_BASE*/
#define IA32_APIC_BASE_addr          0x0000001B
#define IA32_APIC_BASE_phy_addr_MASK (0xffffff << 12)
#define IA32_APIC_BASE_X_ENABLE      (1 << 11)
#define IA32_APIC_BASE_X2_ENABLE     (1 << 10)
#define IA32_APIC_BASE_BSP           (1 << 8)

/*FS and GS*/
#define MSR_FS_BASE        0xC0000100
#define MSR_GS_BASE        0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

/*IA32_STAR*/
#define MSR_IA32_STAR 0xC0000081
/*IA32_LSTAR*/
#define MSR_IA32_LSTAR 0xc0000082
/*IA32_FMASK*/
#define MSR_IA32_FMASK 0xc0000084
#endif