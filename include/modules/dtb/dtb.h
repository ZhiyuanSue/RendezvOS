#ifndef _SHAMPOOS_DTB_H_
#define _SHAMPOOS_DTB_H_
#include	<common/endianness.h>
#include	<common/stddef.h>
#include    "libfdt.h"
#include	"fdt.h"

uint32_t fdt_next_tag(struct fdt_header* header_ptr,u32 cur_offset,u32* nextoffset);
uint32_t fdt_next_node();
const char* fdt_get_name(struct fdt_header* header_ptr,u32 cur_offset,u32* nextoffset);


#endif