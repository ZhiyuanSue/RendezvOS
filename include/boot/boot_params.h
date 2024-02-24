#ifndef _SHAMPOOS_BOOT_PARAM_H_
#define _SHAMPOOS_BOOT_PARAM_H_
#include <shampoos/types.h>
typedef struct boot_params{
    uint64_t    acpi_table_rsdp_addr;   /*if no acpi,just null*/
    uint64_t    dtb_base_addr;
    uint32_t    multiboot_addr;
}BOOT_PARAMS __attribute__((packed));

#endif