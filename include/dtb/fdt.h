#ifndef _SHAMPOOS_FDT_H_
#define _SHAMPOOS_FDT_H_
#include <shampoos/types.h>
/*
 * For the devicetree specification,the version are all 17 from 0.1-0.4(the current version)
 */
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009
struct property_data_desc{
    uint32_t len;
    uint32_t nameoff;
};

void parse_dtb(uintptr_t dtb_addr);
#endif