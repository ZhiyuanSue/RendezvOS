#ifndef _SHAMPOOS_AARCH64_SYS_CTRL_DEF_H_
#define _SHAMPOOS_AARCH64_SYS_CTRL_DEF_H_

/*CurrentEL*/
#define CurrentEL_EL0 0x0
#define CurrentEL_EL1 0x4
#define CurrentEL_EL2 0x8
#define CurrentEL_EL3 0xC
/*SCTLR_EL1*/
#define SCTLR_EL1_M (1)
#define SCTLR_EL1_I (1 << 12)
#define SCTLR_EL1_C (1 << 2)
/*TCR_EL1*/
#define TCR_EL1_ORGN0_NON_CACHE 0
#define TCR_EL1_ORGN0_WB_RA_WA_CACHE (1 << 10)
#define TCR_EL1_ORGN0_WT_RA_NO_WA_CACHE (1 << 11)
#define TCR_EL1_ORGN0_WB_RA_NO_WA_CACHE (0x3 << 10)

#define TCR_EL1_IRGN0_NON_CACHE 0
#define TCR_EL1_IRGN0_WB_RA_WA_CACHE (1 << 8)
#define TCR_EL1_IRGN0_WT_RA_NO_WA_CACHE (1 << 9)
#define TCR_EL1_IRGN0_WB_RA_NO_WA_CACHE (0x3 << 8)

#define TCR_EL1_ORGN1_NON_CACHE 0
#define TCR_EL1_ORGN1_WB_RA_WA_CACHE (1 << 26)
#define TCR_EL1_ORGN1_WT_RA_NO_WA_CACHE (1 << 27)
#define TCR_EL1_ORGN1_WB_RA_NO_WA_CACHE (0x3 << 26)

#define TCR_EL1_IRGN1_NON_CACHE 0
#define TCR_EL1_IRGN1_WB_RA_WA_CACHE (1 << 24)
#define TCR_EL1_IRGN1_WT_RA_NO_WA_CACHE (1 << 25)
#define TCR_EL1_IRGN1_WB_RA_NO_WA_CACHE (0x3 << 24)

#define TCR_EL1_SH0_NON_CACHE 0
#define TCR_EL1_SH0_OUTER_SHARED_CACHE (1 << 13)
#define TCR_EL1_SH0_INNER_SHARED_CACHE (0x3 << 12)

#define TCR_EL1_SH1_NON_CACHE 0
#define TCR_EL1_SH1_OUTER_SHARED_CACHE (1 << 29)
#define TCR_EL1_SH1_INNER_SHARED_CACHE (0x3 << 28)

#define TCR_EL1_TG0_4KB 0
#define TCR_EL1_TG0_16KB (1 << 15)
#define TCR_EL1_TG0_64KB (1 << 14)

#define TCR_EL1_TG1_4KB (1 << 31)
#define TCR_EL1_TG1_16KB (1 << 30)
#define TCR_EL1_TG1_64KB (0x3 << 30)

#define TCR_EL1_IPS_MASK (0x7 << 32)
#define TCR_EL1_IPS_32bit 0
#define TCR_EL1_IPS_36bit (1 << 32)
#define TCR_EL1_IPS_40bit (1 << 33)
#define TCR_EL1_IPS_42bit (0x3 << 32)
#define TCR_EL1_IPS_44bit (1 << 34)
#define TCR_EL1_IPS_48bit (0x5 << 32)
#define TCR_EL1_IPS_52bit (0x6 << 32)

#define TCR_EL1_TBI1_IGN (1 << 38)
#define TCR_EL1_TBI1_USED 0

#define TCR_EL1_TBI0_IGN (1 << 37)
#define TCR_EL1_TBI0_USED 0

#define TCR_EL1_AS (1 << 36)

#define TCR_EL1_EPD1_ENABLE_WALK 0
#define TCR_EL1_EPD1_DISABLE_WALK (1 << 23)

#define TCR_EL1_EPD0_ENABLE_WALK 0
#define TCR_EL1_EPD0_DISABLE_WALK (1 << 7)

#define TCR_EL1_T1SZ_MASK (0x3f << 16)
#define TCR_EL1_T0SZ_MASK (0x3f)

#define ID_AA64MMFR0_EL1_PARANGE_MASK (0xf)

/*MAIR*/

/*SPSel*/
#define SPSEL_SP_ELx (1)

/*CPACR_EL1*/
#define CPACR_EL1_TTA (1 << 28)
#define CPACR_EL1_FPEN_TRAP_EL0 (1 << 20)
#define CPACR_EL1_FPEN_TRAP_EL0_EL1 (1 << 21) /*the same with 0b00*/
#define CPACR_EL1_FPEN_NONE (0x3 << 20)

#define CPACR_EL1_ZEN_TRAP_EL0 (1 << 16)
#define CPACR_EL1_ZEN_TRAP_EL0_EL1 (1 << 17) /*the same with 0b00*/
#define CPACR_EL1_ZEN_TRAP_NONE (0x3 << 16)
#endif