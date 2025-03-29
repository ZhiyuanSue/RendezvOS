#ifndef _RENDEZVOS_DTB_H_
#define _RENDEZVOS_DTB_H_
#include "fdt.h"
#include "libfdt.h"
#include "print_property.h"
#include "property.h"
#include "dev_tree.h"
#include <common/endianness.h>
#include <common/limits.h>
#include <common/string.h>
#include <common/mm.h>
#include <common/stddef.h>

int fdt_check_header(const void *fdt);
const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int len);
uint32_t fdt_next_tag(const void *fdt, int startoffset, int *nextoffset);
int fdt_next_node(const void *fdt, int offset, int *depth);
int fdt_first_subnode(const void *fdt, int offset);
int fdt_next_subnode(const void *fdt, int offset);
int fdt_first_property_offset(const void *fdt, int nodeoffset);
int fdt_next_property_offset(const void *fdt, int offset);
const char *fdt_string(const void *fdt, int stroffset);
struct fdt_property *
raw_get_prop_from_dtb(void *fdt, int offset, int depth,
                      struct property_type *property_types_addr,
                      const char *cmp_str, const char *cmp_type_str,
                      u64 raw_get_mode,
                      void (*f)(struct fdt_property *fdt_prop));
#define DTB_RAW_GET_PROP_MODE_SINGLE 1 /*we only want to find one prop*/
#define DTB_RAW_GET_PROP_MODE_MUL    0
/*we want get multi props and use function f to do it*/
#endif