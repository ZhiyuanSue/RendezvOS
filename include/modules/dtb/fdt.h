#ifndef _RENDEZVOS_FDT_H_
#define _RENDEZVOS_FDT_H_
#include <common/types.h>
/*
 * For the devicetree specification,the version are all 17 from 0.1-0.4(the
 * current version)
 */
/*
    I copied some of the following code from
   u-boot(https://github.com/u-boot/u-boot) and changed something to let it pass
   complie 2024/5/21
*/
struct fdt_header {
#define FDT_MAGIC 0xd00dfeed
        u32 magic;
        u32 totalsize;
        u32 off_dt_struct;
        u32 off_dt_strings;
        u32 off_mem_rsvmap;
        u32 version;
        u32 last_comp_version;
        u32 boot_cpuid_phys;
        u32 size_dt_strings;
        u32 size_dt_struct;
};

struct fdt_reserve_entry {
        u64 address;
        u64 size;
};
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009
struct fdt_node_header {
        u32 tag;
        char name[0];
};
struct fdt_property {
        u32 tag;
        u32 len;
        u32 nameoff;
        char data[0];
};
#define FDT_TAGSIZE  sizeof(u_int32_t)
#define FDT_V1_SIZE  (7 * sizeof(u_int32_t))
#define FDT_V2_SIZE  (FDT_V1_SIZE + sizeof(u_int32_t))
#define FDT_V3_SIZE  (FDT_V2_SIZE + sizeof(u_int32_t))
#define FDT_V16_SIZE FDT_V3_SIZE
#define FDT_V17_SIZE (FDT_V16_SIZE + sizeof(u_int32_t))
#endif