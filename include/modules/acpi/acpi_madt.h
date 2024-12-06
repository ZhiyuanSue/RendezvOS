#ifndef _ACPI_TABLE_MADT_H_
#define _ACPI_TABLE_MADT_H_
#include "acpi_table.h"

struct acpi_table_madt {
        ACPI_TABLE_HEAD;
        u32 Local_int_ctrl_address;
        u32 flags;
#define MADT_PCAT_COMPAT 1
};
#endif