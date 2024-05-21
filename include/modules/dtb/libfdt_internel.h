#ifndef _SHAMPOOS_LIBFDT_INTERNEL_H_
#define _SHAMPOOS_LIBFDT_INTERNEL_H_

static inline const void *fdt_offset_ptr_(const void *fdt, int offset)
{
	return (const char *)fdt + fdt_off_dt_struct(fdt) + offset;
}

#endif