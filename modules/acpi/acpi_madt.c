#include <modules/acpi/acpi_madt.h>

struct madt_int_ctrl_head*
get_next_ctrl_head(struct madt_int_ctrl_head* curr_ctrl_head)
{
        u64 next_ctrl_head = (u64)curr_ctrl_head;
        next_ctrl_head += curr_ctrl_head->length;
        return (struct madt_int_ctrl_head*)next_ctrl_head;
}
bool final_madt_int_ctrl_head(struct acpi_table_madt* madt_table,
                              struct madt_int_ctrl_head* curr_ctrl_head)
{
        u64 madt_ptr = (u64)madt_table;
        madt_ptr += madt_table->length;
        u64 curr_head_ptr = (u64)curr_ctrl_head;
        curr_head_ptr += curr_ctrl_head->length;
        return madt_ptr == curr_head_ptr;
}
