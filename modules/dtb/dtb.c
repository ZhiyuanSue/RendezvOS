#include	<modules/log/log.h>
#include	<modules/dtb/dtb.h>

/*
	I copied most of the following code from u-boot,and changed something to let it pass complie
*/
size_t fdt_header_size_(uint32_t version)
{
	if (version <= 1)
		return FDT_V1_SIZE;
	else if (version <= 2)
		return FDT_V2_SIZE;
	else if (version <= 3)
		return FDT_V3_SIZE;
	else if (version <= 16)
		return FDT_V16_SIZE;
	else
		return FDT_V17_SIZE;
}

const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int len)
{
	unsigned int uoffset = offset;
	unsigned int absoffset = offset + fdt_off_dt_struct(fdt);

	if (offset < 0)
		return NULL;

	if (fdt_chk_basic())
		if ((absoffset < uoffset)
		    || ((absoffset + len) < absoffset)
		    || (absoffset + len) > fdt_totalsize(fdt))
			return NULL;

	if (!fdt_chk_version() || fdt_version(fdt) >= 0x11)
		if (((uoffset + len) < uoffset)
		    || ((offset + len) > fdt_size_dt_struct(fdt)))
			return NULL;

	return fdt_offset_ptr_(fdt, offset);
}