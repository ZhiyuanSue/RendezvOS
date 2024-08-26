#include <modules/dtb/dtb.h>
#include <modules/log/log.h>

/*
	I copied most of the following code from
   u-boot(https://github.com/u-boot/u-boot) and changed something to let it pass
   complie 2024/5/21
*/
static int	check_off_(uint32_t hdrsize, uint32_t totalsize, uint32_t off)
{
	return (off >= hdrsize) && (off <= totalsize);
}
static int	check_block_(uint32_t hdrsize, uint32_t totalsize, uint32_t base,
		uint32_t size)
{
	if (!check_off_(hdrsize, totalsize, base))
		return (0); /* block start out of bounds */
	if ((base + size) < base)
		return (0); /* overflow */
	if (!check_off_(hdrsize, totalsize, base + size))
		return (0); /* block end out of bounds */
	return (1);
}
size_t	fdt_header_size(const void *fdt)
{
	int	version;

	version = fdt_version(fdt);
	if (version <= 1)
		return (FDT_V1_SIZE);
	else if (version <= 2)
		return (FDT_V2_SIZE);
	else if (version <= 3)
		return (FDT_V3_SIZE);
	else if (version <= 16)
		return (FDT_V16_SIZE);
	else
		return (FDT_V17_SIZE);
}
int	fdt_check_header(const void *fdt)
{
	size_t	hdrsize;

	if (fdt_magic(fdt) != FDT_MAGIC)
		return (-FDT_ERR_BADMAGIC);
	if ((fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION)
		|| (fdt_last_comp_version(fdt) > FDT_LAST_SUPPORTED_VERSION))
		return (-FDT_ERR_BADVERSION);
	if (fdt_version(fdt) < fdt_last_comp_version(fdt))
		return (-FDT_ERR_BADVERSION);
	hdrsize = fdt_header_size(fdt);
	if ((fdt_totalsize(fdt) < hdrsize) || (fdt_totalsize(fdt) > INT_MAX))
		return (-FDT_ERR_TRUNCATED);
	/* Bounds check memrsv block */
	if (!check_off_(hdrsize, fdt_totalsize(fdt), fdt_off_mem_rsvmap(fdt)))
		return (-FDT_ERR_TRUNCATED);
	/* Bounds check structure block */
	if (fdt_version(fdt) < 17)
	{
		if (!check_off_(hdrsize, fdt_totalsize(fdt), fdt_off_dt_struct(fdt)))
			return (-FDT_ERR_TRUNCATED);
	}
	else
	{
		if (!check_block_(hdrsize, fdt_totalsize(fdt), fdt_off_dt_struct(fdt),
				fdt_size_dt_struct(fdt)))
			return (-FDT_ERR_TRUNCATED);
	}
	/* Bounds check strings block */
	if (!check_block_(hdrsize, fdt_totalsize(fdt), fdt_off_dt_strings(fdt),
			fdt_size_dt_strings(fdt)))
		return (-FDT_ERR_TRUNCATED);
	return (0);
}

const void	*fdt_offset_ptr(const void *fdt, int offset, unsigned int len)
{
	unsigned int	uoffset;
	unsigned int	absoffset;

	uoffset = offset;
	absoffset = offset + fdt_off_dt_struct(fdt);
	if (offset < 0)
		return (NULL);
	if ((absoffset < uoffset) || ((absoffset + len) < absoffset) || (absoffset
			+ len) > fdt_totalsize(fdt))
		return (NULL);
	if (fdt_version(fdt) >= 0x11)
		if (((uoffset + len) < uoffset) || ((offset
					+ len) > fdt_size_dt_struct(fdt)))
			return (NULL);
	return (fdt_offset_ptr_(fdt, offset));
}

uint32_t	fdt_next_tag(const void *fdt, int startoffset, int *nextoffset)
{
	uint32_t	tag;
	int			offset;
	const char	*p;

	const uint32_t *tagp, *lenp;
	offset = startoffset;
	*nextoffset = -FDT_ERR_TRUNCATED;
	tagp = fdt_offset_ptr(fdt, offset, FDT_TAGSIZE);
	if (!tagp)
		return (FDT_END); /* premature end */
	tag = SWAP_ENDIANNESS_32(*tagp);
	offset += FDT_TAGSIZE;
	*nextoffset = -FDT_ERR_BADSTRUCTURE;
	switch (tag)
	{
	case FDT_BEGIN_NODE:
		/* skip name */
		do
		{
			p = fdt_offset_ptr(fdt, offset++, 1);
		} while (p && (*p != '\0'));
		if (!p)
			return (FDT_END); /* premature end */
		break ;
	case FDT_PROP:
		lenp = fdt_offset_ptr(fdt, offset, sizeof(*lenp));
		if (!lenp)
			return (FDT_END); /* premature end */
		/* skip-name offset, length and value */
		offset += sizeof(struct fdt_property) - FDT_TAGSIZE
			+ SWAP_ENDIANNESS_32(*lenp);
		if (fdt_version(fdt) < 0x10 && SWAP_ENDIANNESS_32(*lenp) >= 8
			&& ((offset - SWAP_ENDIANNESS_32(*lenp)) % 8) != 0)
			offset += 4;
		break ;
	case FDT_END:
	case FDT_END_NODE:
	case FDT_NOP:
		break ;
	default:
		return (FDT_END);
	}
	if (!fdt_offset_ptr(fdt, startoffset, offset - startoffset))
		return (FDT_END); /* premature end */
	*nextoffset = ROUND_UP(offset, FDT_TAGSIZE);
	return (tag);
}
int	fdt_check_node_offset_(const void *fdt, int offset)
{
	if ((offset < 0) || (offset % FDT_TAGSIZE) || (fdt_next_tag(fdt, offset,
				&offset) != FDT_BEGIN_NODE))
		return (-FDT_ERR_BADOFFSET);
	return (offset);
}
int	fdt_check_prop_offset_(const void *fdt, int offset)
{
	if ((offset < 0) || (offset % FDT_TAGSIZE) || (fdt_next_tag(fdt, offset,
				&offset) != FDT_PROP))
		return (-FDT_ERR_BADOFFSET);
	return (offset);
}

int	fdt_next_node(const void *fdt, int offset, int *depth)
{
	int			nextoffset;
	uint32_t	tag;

	nextoffset = 0;
	if (offset >= 0)
		if ((nextoffset = fdt_check_node_offset_(fdt, offset)) < 0)
			return (nextoffset);
	do
	{
		offset = nextoffset;
		tag = fdt_next_tag(fdt, offset, &nextoffset);
		switch (tag)
		{
		case FDT_PROP:
		case FDT_NOP:
			break ;
		case FDT_BEGIN_NODE:
			if (depth)
				(*depth)++;
			break ;
		case FDT_END_NODE:
			if (depth && ((--(*depth)) < 0))
				return (nextoffset);
			break ;
		case FDT_END:
			if ((nextoffset >= 0) || ((nextoffset == -FDT_ERR_TRUNCATED)
					&& !depth))
				return (-FDT_ERR_NOTFOUND);
			else
				return (nextoffset);
		}
	} while (tag != FDT_BEGIN_NODE);
	return (offset);
}

static int	nextprop_(const void *fdt, int offset)
{
	uint32_t	tag;
	int			nextoffset;

	do
	{
		tag = fdt_next_tag(fdt, offset, &nextoffset);
		switch (tag)
		{
		case FDT_END:
			if (nextoffset >= 0)
				return (-FDT_ERR_BADSTRUCTURE);
			else
				return (nextoffset);
		case FDT_PROP:
			return (offset);
		}
		offset = nextoffset;
	} while (tag == FDT_NOP);
	return (-FDT_ERR_NOTFOUND);
}

int	fdt_first_subnode(const void *fdt, int offset)
{
	int	depth;

	depth = 0;
	offset = fdt_next_node(fdt, offset, &depth);
	if (offset < 0 || depth != 1)
		return (-FDT_ERR_NOTFOUND);
	return (offset);
}

int	fdt_next_subnode(const void *fdt, int offset)
{
	int	depth;

	depth = 1;
	/*
		* With respect to the parent, the depth of the next subnode will be
		* the same as the last.
		*/
	do
	{
		offset = fdt_next_node(fdt, offset, &depth);
		if (offset < 0 || depth < 1)
			return (-FDT_ERR_NOTFOUND);
	} while (depth > 1);
	return (offset);
}

int	fdt_first_property_offset(const void *fdt, int nodeoffset)
{
	int	offset;

	if ((offset = fdt_check_node_offset_(fdt, nodeoffset)) < 0)
		return (offset);
	return (nextprop_(fdt, offset));
}

int	fdt_next_property_offset(const void *fdt, int offset)
{
	if ((offset = fdt_check_prop_offset_(fdt, offset)) < 0)
		return (offset);
	return (nextprop_(fdt, offset));
}
const char	*fdt_get_string(const void *fdt, int stroffset, int *lenp)
{
	const char	*s;

	if (stroffset > fdt_size_dt_strings(fdt))
		return (NULL);
	s = (const char *)fdt + fdt_off_dt_strings(fdt) + stroffset;
	if (lenp)
		*lenp = strlen(s);
	return (s);
}

const char	*fdt_string(const void *fdt, int stroffset)
{
	return (fdt_get_string(fdt, stroffset, NULL));
}