#ifndef _ACPI_TABLE_FADT_H_
#define _ACPI_TABLE_FADT_H_
#include "acpi_table.h"
#pragma pack(1)
/*Fixed ACPI Description Table(FADT)*/
struct acpi_table_fadt {
        ACPI_TABLE_HEAD; /* The signature is "FACP",
                             and the revision is 6 ,and it's the major
                             version,the minor version is at offset 131*/
        u32 FIRMWARE_CTRL; /* Physical memory address of the
                                        FACS, where OSPM and Firmware
                                        exchange control information. If
                                        the HARDWARE_REDUCED_ACPI flag is
                                        set, and both this field and the
                                        X_FIRMWARE_CTRL field are zero,
                                        there is no FACS available.*/
        u32 DSDT; /* Physical memory address of the DSDT. */
        u8 reserved1;
        u8 Preferred_PM_profile; /* OSPM can use this field to set default power
                                    management policy parameters during OS
                                    installation*/
#define Pref_PM_Profile_Unspecified        0
#define Pref_PM_Profile_Desktop            1
#define Pref_PM_Profile_Mobile             2
#define Pref_PM_Profile_Workstation        3
#define Pref_PM_Profile_Enterprise_Server  4
#define Pref_PM_Profile_SOHO_Serve         5
#define Pref_PM_Profile_Appliance_PC       6
#define Pref_PM_Profile_Performance_Server 7
#define Pref_PM_Profile_Tablet             8
        /* >8 Reserved */
        u16 SCI_INT; /*System vector the SCI interrupt is wired to in 8259 mode.
                         On systems that do not contain the 8259, this field
                         contains the Global System interrupt number of the SCI
                         interrupt.    OSPM is required to treat the ACPI SCI
                         interrupt    as a shareable, level,
                             active low interrupt.*/
        u32 SMI_CMD; /**/
        u8 ACPI_ENABLE; /**/
        u8 ACPI_DISABLE;
        u8 S4BIOS_REQ;
        u8 PSTATE_CNT;
        u32 PM1a_EVT_BLK;
        u32 PM1b_EVT_BLK;
        u32 PM1a_CNT_BLK;
        u32 PM1b_CNT_BLK;
        u32 PM2_CNT_BLK;
        u32 PM_TMR_BLK;
        u32 GPE0_BLK;
        u32 GPE1_BLK;
        u8 PM1_EVT_LEN;
        u8 PM1_CNT_LEN;
        u8 PM2_CNT_LEN;
        u8 PM_TMR_LEN;
        u8 GPE0_BLK_LEN;
        u8 GPE1_BLK_LEN;
        u8 GPE1_BASE;
        u8 CST_CNT;
        u16 P_LVL2_LAT;
        u16 P_LVL3_LAT;
        u16 FLUSH_SIZE;
        u16 FLUSH_STRIDE;
        u8 DUTY_OFFSET;
        u8 DUTY_WIDTH;
        u8 DAY_ALRM;
        u8 MON_ALRM;
        u8 CENTURY;
        u16 IAPC_BOOT_ARCH;
        u8 reserved2;
        u32 Flags;
        struct acpi_gas RESET_REG;
        u8 RESET_VALUE;
        u16 ARM_BOOT_ARCH;
        u8 FADT_Minor_Version;
        u64 X_FIRMWARE_CTRL;
        u64 X_DSDT;
        struct acpi_gas X_PM1a_EVT_BLK;
        struct acpi_gas X_PM1b_EVT_BLK;
        struct acpi_gas X_PM1a_CNT_BLK;
        struct acpi_gas X_PM1b_CNT_BLK;
        struct acpi_gas X_PM2_CNT_BLK;
        struct acpi_gas X_PM_TMR_BLK;
        struct acpi_gas X_GPE0_BLK;
        struct acpi_gas X_GPE1_BLK;
        struct acpi_gas SLEEP_CONTROL_REG;
        struct acpi_gas SLEEP_STATUS_REG;
        u64 Hypervisor_Vendor_Id;
};
#endif