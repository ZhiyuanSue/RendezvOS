#ifndef _ACPI_TABLE_MADT_H_
#define _ACPI_TABLE_MADT_H_
#include "acpi_table.h"
#include <common/stdbool.h>
#pragma pack(1)
struct acpi_table_madt {
        ACPI_TABLE_HEAD;
        u32 Local_int_ctrl_address;
        u32 flags;
#define MADT_PCAT_COMPAT 1
        char int_ctrl_structure[];
};
#define MADT_CTRL_HEAD \
        u8 type;       \
        u8 length

struct madt_int_ctrl_head {
        MADT_CTRL_HEAD;
};
enum madt_int_ctrl_type {
        madt_ctrl_type_Local_APIC,
        madt_ctrl_type_IO_APIC,
        madt_ctrl_type_Source_Override,
        madt_ctrl_type_NMI_Src,
        madt_ctrl_type_Local_APIC_NMI,
        madt_ctrl_type_Local_APIC_Addr_Override,
        madt_ctrl_type_IO_SAPIC,
        madt_ctrl_type_Local_SAPIC,
        madt_ctrl_type_Plt_Int_Src,
        madt_ctrl_type_Local_x2APIC,
        madt_ctrl_type_Local_x2APIC_NMI,
        madt_ctrl_type_GICC,
        madt_ctrl_type_GICD,
        madt_ctrl_type_GIC_MSI_Frame,
        madt_ctrl_type_GICR,
        madt_ctrl_type_GIC_ITS,
        madt_ctrl_type_Multiprocessor_wakeup,
        madt_ctrl_type_CORE_PIC,
        madt_ctrl_type_LIO_PIC,
        madt_ctrl_type_HT_PIC,
        madt_ctrl_type_EIO_PIC,
        madt_ctrl_type_MSI_PIC,
        madt_ctrl_type_BIO_PIC,
        madt_ctrl_type_LPC_PIC,
};
struct madt_Local_APIC { // type = 0
        MADT_CTRL_HEAD;
        u8 _ACPI_P_UID;
        u8 _APIC_ID;
        u32 flags;
#define Local_APIC_flags_enable         (1 << 0)
#define Local_APIC_flags_online_capable (1 < 1)
};
struct madt_IO_APIC {
        MADT_CTRL_HEAD;
        u8 IO_APIC_ID;
        u8 res;
        u32 IO_APIC_Addr;
        u32 Global_Sys_Int_Base;
};
struct madt_Source_Override {
        MADT_CTRL_HEAD;
        u8 Bus;
        u8 Source;
        u32 Global_Sys_Int;
        u16 flags; /*MPS INTI flags*/
};

struct madt_int_ctrl_head*
get_next_ctrl_head(struct madt_int_ctrl_head* curr_ctrl_head);
bool final_madt_int_ctrl_head(struct acpi_table_madt* madt_table,
                              struct madt_int_ctrl_head* curr_ctrl_head);
error_t parser_apic();
#define for_each_madt_ctrl_head(madt_table)                                      \
        for (struct madt_int_ctrl_head* curr_ctrl_head =                         \
                     (struct madt_int_ctrl_head*)(((struct acpi_table_madt*)     \
                                                           madt_table)           \
                                                          ->int_ctrl_structure); \
             !final_madt_int_ctrl_head((struct acpi_table_madt*)madt_table,      \
                                       curr_ctrl_head);                          \
             curr_ctrl_head = get_next_ctrl_head(curr_ctrl_head))
#endif