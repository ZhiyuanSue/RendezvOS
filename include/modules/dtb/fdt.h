#ifndef _SHAMPOOS_FDT_H_
# define _SHAMPOOS_FDT_H_
# include <common/types.h>
/*
 * For the devicetree specification,the version are all 17 from 0.1-0.4(the
 * current version)
 */
/*
	I copied some of the following code from
   u-boot(https://github.com/u-boot/u-boot) and changed something to let it pass
   complie 2024/5/21
*/
struct			fdt_header
{
# define FDT_MAGIC 0xd00dfeed
	uint32_t	magic;
	uint32_t	totalsize;
	uint32_t	off_dt_struct;
	uint32_t	off_dt_strings;
	uint32_t	off_mem_rsvmap;
	uint32_t	version;
	uint32_t	last_comp_version;
	uint32_t	boot_cpuid_phys;
	uint32_t	size_dt_strings;
	uint32_t	size_dt_struct;
};

struct			fdt_reserve_entry
{
	uint64_t	address;
	uint64_t	size;
};
# define FDT_BEGIN_NODE 0x00000001
# define FDT_END_NODE 0x00000002
# define FDT_PROP 0x00000003
# define FDT_NOP 0x00000004
# define FDT_END 0x00000009
struct			fdt_node_header
{
	uint32_t	tag;
	char		name[0];
};
struct			fdt_property
{
	uint32_t	tag;
	uint32_t	len;
	uint32_t	nameoff;
	char		data[0];
};
# define FDT_TAGSIZE sizeof(u_int32_t)
# define FDT_V1_SIZE (7 * sizeof(u_int32_t))
# define FDT_V2_SIZE (FDT_V1_SIZE + sizeof(u_int32_t))
# define FDT_V3_SIZE (FDT_V2_SIZE + sizeof(u_int32_t))
# define FDT_V16_SIZE FDT_V3_SIZE
# define FDT_V17_SIZE (FDT_V16_SIZE + sizeof(u_int32_t))
#endif