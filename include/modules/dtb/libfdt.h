#ifndef _RENDEZVOS_LIBFDT_H_
#define _RENDEZVOS_LIBFDT_H_
#include <common/endianness.h>
/*
    I copied most of the following code from
   u-boot(https://github.com/u-boot/u-boot) and changed something to let it pass
   complie 2024/5/21
*/
#define FDT_FIRST_SUPPORTED_VERSION 0x02
#define FDT_LAST_SUPPORTED_VERSION  0x11

/* Error codes: informative error codes */
#define FDT_ERR_NOTFOUND 1
/* FDT_ERR_NOTFOUND: The requested node or property does not exist */
#define FDT_ERR_EXISTS 2
/* FDT_ERR_EXISTS: Attempted to create a node or property which
 * already exists */
#define FDT_ERR_NOSPACE 3
/* FDT_ERR_NOSPACE: Operation needed to expand the device
 * tree, but its buffer did not have sufficient space to
 * contain the expanded tree. Use fdt_open_into() to move the
 * device tree to a buffer with more space. */

/* Error codes: codes for bad parameters */
#define FDT_ERR_BADOFFSET 4
/* FDT_ERR_BADOFFSET: Function was passed a structure block
 * offset which is out-of-bounds, or which points to an
 * unsuitable part of the structure for the operation. */
#define FDT_ERR_BADPATH 5
/* FDT_ERR_BADPATH: Function was passed a badly formatted path
 * (e.g. missing a leading / for a function which requires an
 * absolute path) */
#define FDT_ERR_BADPHANDLE 6
/* FDT_ERR_BADPHANDLE: Function was passed an invalid phandle.
 * This can be caused either by an invalid phandle property
 * length, or the phandle value was either 0 or -1, which are
 * not permitted. */
#define FDT_ERR_BADSTATE 7
/* FDT_ERR_BADSTATE: Function was passed an incomplete device
 * tree created by the sequential-write functions, which is
 * not sufficiently complete for the requested operation. */

/* Error codes: codes for bad device tree blobs */
#define FDT_ERR_TRUNCATED 8
/* FDT_ERR_TRUNCATED: FDT or a sub-block is improperly
 * terminated (overflows, goes outside allowed bounds, or
 * isn't properly terminated).  */
#define FDT_ERR_BADMAGIC 9
/* FDT_ERR_BADMAGIC: Given "device tree" appears not to be a
 * device tree at all - it is missing the flattened device
 * tree magic number. */
#define FDT_ERR_BADVERSION 10
/* FDT_ERR_BADVERSION: Given device tree has a version which
 * can't be handled by the requested operation.  For
 * read-write functions, this may mean that fdt_open_into() is
 * required to convert the tree to the expected version. */
#define FDT_ERR_BADSTRUCTURE 11
/* FDT_ERR_BADSTRUCTURE: Given device tree has a corrupt
 * structure block or other serious error (e.g. misnested
 * nodes, or subnodes preceding properties). */
#define FDT_ERR_BADLAYOUT 12
/* FDT_ERR_BADLAYOUT: For read-write functions, the given
 * device tree has it's sub-blocks in an order that the
 * function can't handle (memory reserve map, then structure,
 * then strings).  Use fdt_open_into() to reorganize the tree
 * into a form suitable for the read-write operations. */

/* "Can't happen" error indicating a bug in libfdt */
#define FDT_ERR_INTERNAL 13
/* FDT_ERR_INTERNAL: libfdt has failed an internal assertion.
 * Should never be returned, if it is, it indicates a bug in
 * libfdt itself. */

/* Errors in device tree content */
#define FDT_ERR_BADNCELLS 14
/* FDT_ERR_BADNCELLS: Device tree has a #address-cells, #size-cells
 * or similar property with a bad format or value */

#define FDT_ERR_BADVALUE 15
/* FDT_ERR_BADVALUE: Device tree has a property with an unexpected
 * value. For example: a property expected to contain a string list
 * is not NUL-terminated within the length of its value. */

#define FDT_ERR_BADOVERLAY 16
/* FDT_ERR_BADOVERLAY: The device tree overlay, while
 * correctly structured, cannot be applied due to some
 * unexpected or missing value, property or node. */

#define FDT_ERR_NOPHANDLES 17
/* FDT_ERR_NOPHANDLES: The device tree doesn't have any
 * phandle available anymore without causing an overflow */

#define FDT_ERR_BADFLAGS 18
/* FDT_ERR_BADFLAGS: The function was passed a flags field that
 * contains invalid flags or an invalid combination of flags. */

#define FDT_ERR_MAX 18

/* constants */
#define FDT_MAX_PHANDLE 0xfffffffe
/* Valid values for phandles range from 1 to 2^32-2. */

#define fdt_for_each_subnode(node, fdt, parent)                \
        for (node = fdt_first_subnode(fdt, parent); node >= 0; \
             node = fdt_next_subnode(fdt, node))
#define fdt_for_each_property_offset(property, fdt, node)                    \
        for (property = fdt_first_property_offset(fdt, node); property >= 0; \
             property = fdt_next_property_offset(fdt, property))
/**********************************************************************/
/* General functions                                                  */
/**********************************************************************/
#define fdt_get_header(fdt, field) \
        (SWAP_ENDIANNESS_32(((const struct fdt_header *)(fdt))->field))
#define fdt_magic(fdt)             (fdt_get_header(fdt, magic))
#define fdt_totalsize(fdt)         (fdt_get_header(fdt, totalsize))
#define fdt_off_dt_struct(fdt)     (fdt_get_header(fdt, off_dt_struct))
#define fdt_off_dt_strings(fdt)    (fdt_get_header(fdt, off_dt_strings))
#define fdt_off_mem_rsvmap(fdt)    (fdt_get_header(fdt, off_mem_rsvmap))
#define fdt_version(fdt)           (fdt_get_header(fdt, version))
#define fdt_last_comp_version(fdt) (fdt_get_header(fdt, last_comp_version))
#define fdt_boot_cpuid_phys(fdt)   (fdt_get_header(fdt, boot_cpuid_phys))
#define fdt_size_dt_strings(fdt)   (fdt_get_header(fdt, size_dt_strings))
#define fdt_size_dt_struct(fdt)    (fdt_get_header(fdt, size_dt_struct))

static inline const void *fdt_offset_ptr_(const void *fdt, int offset)
{
        return ((const char *)fdt + fdt_off_dt_struct(fdt) + offset);
}
#endif