#ifndef _SHAMPOOS_SYS_CTRL_DEF_H_
#define _SHAMPOOS_SYS_CTRL_DEF_H_

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
#define TCR_EL1_ORGN0_NON_CACHE         0
#define TCR_EL1_ORGN0_WB_RA_WA_CACHE    (1 << 10)
#define TCR_EL1_ORGN0_WT_RA_NO_WA_CACHE (1 << 11)
#define TCR_EL1_ORGN0_WB_RA_NO_WA_CACHE (0x3 << 10)

#define TCR_EL1_IRGN0_NON_CACHE         0
#define TCR_EL1_IRGN0_WB_RA_WA_CACHE    (1 << 8)
#define TCR_EL1_IRGN0_WT_RA_NO_WA_CACHE (1 << 9)
#define TCR_EL1_IRGN0_WB_RA_NO_WA_CACHE (0x3 << 8)

#define TCR_EL1_ORGN1_NON_CACHE         0
#define TCR_EL1_ORGN1_WB_RA_WA_CACHE    (1 << 26)
#define TCR_EL1_ORGN1_WT_RA_NO_WA_CACHE (1 << 27)
#define TCR_EL1_ORGN1_WB_RA_NO_WA_CACHE (0x3 << 26)

#define TCR_EL1_IRGN1_NON_CACHE         0
#define TCR_EL1_IRGN1_WB_RA_WA_CACHE    (1 << 24)
#define TCR_EL1_IRGN1_WT_RA_NO_WA_CACHE (1 << 25)
#define TCR_EL1_IRGN1_WB_RA_NO_WA_CACHE (0x3 << 24)

#define TCR_EL1_SH0_NON_CACHE          0
#define TCR_EL1_SH0_OUTER_SHARED_CACHE (1 << 13)
#define TCR_EL1_SH0_INNER_SHARED_CACHE (0x3 << 12)

#define TCR_EL1_SH1_NON_CACHE          0
#define TCR_EL1_SH1_OUTER_SHARED_CACHE (1 << 29)
#define TCR_EL1_SH1_INNER_SHARED_CACHE (0x3 << 28)

#define TCR_EL1_TG0_4KB  0
#define TCR_EL1_TG0_16KB (1 << 15)
#define TCR_EL1_TG0_64KB (1 << 14)

#define TCR_EL1_TG1_4KB  (1 << 31)
#define TCR_EL1_TG1_16KB (1 << 30)
#define TCR_EL1_TG1_64KB (0x3 << 30)

#define TCR_EL1_IPS_MASK  (0x7 << 32)
#define TCR_EL1_IPS_32bit 0
#define TCR_EL1_IPS_36bit (1 << 32)
#define TCR_EL1_IPS_40bit (1 << 33)
#define TCR_EL1_IPS_42bit (0x3 << 32)
#define TCR_EL1_IPS_44bit (1 << 34)
#define TCR_EL1_IPS_48bit (0x5 << 32)
#define TCR_EL1_IPS_52bit (0x6 << 32)

#define TCR_EL1_TBI1_IGN  (1 << 38)
#define TCR_EL1_TBI1_USED 0

#define TCR_EL1_TBI0_IGN  (1 << 37)
#define TCR_EL1_TBI0_USED 0

#define TCR_EL1_HA (1 << 39)
#define TCR_EL1_HD (1 << 40)

#define TCR_EL1_AS (1 << 36)

#define TCR_EL1_EPD1_ENABLE_WALK  0
#define TCR_EL1_EPD1_DISABLE_WALK (1 << 23)

#define TCR_EL1_EPD0_ENABLE_WALK  0
#define TCR_EL1_EPD0_DISABLE_WALK (1 << 7)

#define TCR_EL1_T1SZ_MASK (0x3f << 16)
#define TCR_EL1_T0SZ_MASK (0x3f)

#define ID_AA64MMFR0_EL1_PARANGE_MASK (0xf)

/*MAIR*/
#define MAIR_EL1_NR     (8)
#define MAIR_EL1_ATTR_0 (0b00000000) /*nGnRnE Device memory*/
/*for normal memory, use alloc(1)*/
#define MAIR_EL1_ATTR_1                                                \
        (0b11111111) /*Normal,inner and outer write back ,r/w allocate \
                        non-transient*/
#define MAIR_EL1_ATTR_2 (0b01000100) /*Normal,inner and outer non-cacheable*/
#define MAIR_EL1_ATTR_3 (0)
#define MAIR_EL1_ATTR_4 (0)
#define MAIR_EL1_ATTR_5 (0)
#define MAIR_EL1_ATTR_6 (0)
#define MAIR_EL1_ATTR_7 (0)
/*SPSel*/
#define SPSEL_SP_ELx (1)

/*CPACR_EL1*/
#define CPACR_EL1_TTA               (1 << 28)
#define CPACR_EL1_FPEN_TRAP_EL0     (1 << 20)
#define CPACR_EL1_FPEN_TRAP_EL0_EL1 (1 << 21) /*the same with 0b00*/
#define CPACR_EL1_FPEN_NONE         (0x3 << 20)

#define CPACR_EL1_ZEN_TRAP_EL0     (1 << 16)
#define CPACR_EL1_ZEN_TRAP_EL0_EL1 (1 << 17) /*the same with 0b00*/
#define CPACR_EL1_ZEN_TRAP_NONE    (0x3 << 16)

/*ESR_EL1*/
/*some are used for aarch32, acturally ignore them in shampoos*/
#define ESR_EL1_MASK                   (0xfc000000) /*bit 31-26*/
#define ESR_EL1_EC_OFF                 (26) /*The following EC code must offset 26*/
#define ESR_EL1_UNKNOWN_REASON         (0 << ESR_EL1_EC_OFF)
#define ESR_EL1_WFI_WFE                (1 << ESR_EL1_EC_OFF)
#define ESR_EL1_MCR_MRC_1              (3 << ESR_EL1_EC_OFF)
#define ESR_EL1_MCRR_MRRC_1            (4 << ESR_EL1_EC_OFF)
#define ESR_EL1_MCR_MRC_0              (5 << ESR_EL1_EC_OFF)
#define ESR_EL1_LDC_STC                (6 << ESR_EL1_EC_OFF)
#define ESR_EL1_FPEN_TFP               (7 << ESR_EL1_EC_OFF)
#define ESR_EL1_MRRC_0                 (0xC << ESR_EL1_EC_OFF)
#define ESR_EL1_BR                     (0xD << ESR_EL1_EC_OFF)
#define ESR_EL1_ILLEGAL_ES             (0xE << ESR_EL1_EC_OFF)
#define ESR_EL1_SVC_AARCH32            (0x11 << ESR_EL1_EC_OFF)
#define ESR_EL1_SVC_AARCH64            (0x18 << ESR_EL1_EC_OFF)
#define ESR_EL1_SVE                    (0x19 << ESR_EL1_EC_OFF)
#define ESR_EL1_POINTER_AUTH           (0x1C << ESR_EL1_EC_OFF)
#define ESR_EL1_LOWER_EL_INST_ABORT    (0x20 << ESR_EL1_EC_OFF)
#define ESR_EL1_CURR_EL_INST_ABORT     (0x21 << ESR_EL1_EC_OFF)
#define ESR_EL1_PC_ALIGN               (0x22 << ESR_EL1_EC_OFF)
#define ESR_EL1_LOWER_EL_DATA_ABORT    (0x24 << ESR_EL1_EC_OFF)
#define ESR_EL1_CURR_EL_DATA_ABORT     (0x25 << ESR_EL1_EC_OFF)
#define ESR_EL1_SP_ALIGN               (0x26 << ESR_EL1_EC_OFF)
#define ESR_EL1_FP_AARCH32             (0x28 << ESR_EL1_EC_OFF)
#define ESR_EL1_FP_AARCH64             (0x2C << ESR_EL1_EC_OFF)
#define ESR_EL1_SERROR                 (0x2F << ESR_EL1_EC_OFF)
#define ESR_EL1_LOWER_EL_BREAK_POINT   (0x30 << ESR_EL1_EC_OFF)
#define ESR_EL1_CURR_EL_BREAK_POINT    (0x31 << ESR_EL1_EC_OFF)
#define ESR_EL1_LOWER_EL_SOFTWARE_STEP (0x32 << ESR_EL1_EC_OFF)
#define ESR_EL1_CURR_EL_SOFTWARE_STEP  (0x33 << ESR_EL1_EC_OFF)
#define ESR_EL1_LOWER_EL_WATCH_POINT   (0x34 << ESR_EL1_EC_OFF)
#define ESR_EL1_CURR_EL_WATCH_POINT    (0x35 << ESR_EL1_EC_OFF)
#define ESR_EL1_BKPT_AARCH32           (0x38 << ESR_EL1_EC_OFF)
#define ESR_EL1_BKP_AARCH64            (0x3C << ESR_EL1_EC_OFF)

#define ESR_EL1_IL       (1 << 25)
#define ESR_EL1_ISS_MASK (0x1ffffff)

/*MPIDR,read only*/
#define MPIDR_EL1_AFF3(MPIDR_VAL) ((MPIDR_VAL & (0xffUL << 32)) >> 32)
#define MPIDR_EL1_AFF2(MPIDR_VAL) ((MPIDR_VAL & (0xffUL << 16)) >> 16)
#define MPIDR_EL1_AFF1(MPIDR_VAL) ((MPIDR_VAL & (0xffUL << 8)) >> 8)
#define MPIDR_EL1_AFF0(MPIDR_VAL) (MPIDR_VAL & 0xffUL)
#define MPIDR_EL1_U(MPIDR_VAL)    ((MPIDR_VAL & (0x1UL << 30)) >> 30)
#define MPIDR_EL1_MT(MPIDR_VAL)   ((MPIDR_VAL & (0x1UL << 24)) >> 24)

#endif