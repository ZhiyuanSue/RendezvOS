#include <common/mm.h>
#include <modules/dtb/property.h>

struct property_type	property_types[PROPERTY_TYPE_NUM] = {
	{"", PROPERTY_TYPE_NONE, {PROPERTY_VALUE_EMPTY}},
	{"compatible", PROPERTY_TYPE_COMPATIBLE, {PROPERTY_VALUE_STRINGLIST}},
	{"model", PROPERTY_TYPE_MODEL, {PROPERTY_VALUE_STRING}},
	{"phandle", PROPERTY_TYPE_PHANDLE, {PROPERTY_VALUE_U32}},
	{"status", PROPERTY_TYPE_STATUS, {PROPERTY_VALUE_STRING}},
	{"#address-cells", PROPERTY_TYPE_ADDRESS_CELLS, {PROPERTY_VALUE_U32}},
	{"#size-cells", PROPERTY_TYPE_SIZE_CELLS, {PROPERTY_VALUE_U32}},
	{"reg", PROPERTY_TYPE_REG, {PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"virtual-reg", PROPERTY_TYPE_VIRTUAL_REG, {PROPERTY_VALUE_U32}},
	{"ranges", PROPERTY_TYPE_RANGES, {PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"dma-ranges", PROPERTY_TYPE_DMA_RANGES,
		{PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"dma-coherent", PROPERTY_TYPE_DMA_COHERENT, {PROPERTY_VALUE_EMPTY}},
	{"dma-noncoherent", PROPERTY_TYPE_DMA_NONCOHERENT, {PROPERTY_VALUE_EMPTY}},
	{"name", PROPERTY_TYPE_NAME, {PROPERTY_VALUE_STRING}},
	{"device_type", PROPERTY_TYPE_DEVICE_TYPE, {PROPERTY_VALUE_STRING}},
	{"interrupts", PROPERTY_TYPE_INTERRUPT,
		{PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"interrupt-parent", PROPERTY_TYPE_INTERRUPT_PARENT,
		{PROPERTY_VALUE_PHANDLE}},
	{"interrupts-extended", PROPERTY_TYPE_INTERRUPTS_EXTENDED,
		{PROPERTY_VALUE_PHANDLE, PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"#interrupt-cells", PROPERTY_TYPE_INTERRUPT_CELLS, {PROPERTY_VALUE_U32}},
	{"interrupt-controller", PROPERTY_TYPE_INTERRUPT_CONTROLLER,
		{PROPERTY_VALUE_EMPTY}},
	{"interrupt-map", PROPERTY_TYPE_INTERRUPT_MAP,
		{PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"interrupt-map-mask", PROPERTY_TYPE_INTERRUPT_MAP_MASK,
		{PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	// the specifier is not defined for centainty, so need be decided
	{"-map", PROPERTY_TYPE_SPECIFIER_MAP, {PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"-map-mask", PROPERTY_TYPE_SPECIFIER_MAP_MASK,
		{PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"-map-pass-thru", PROPERTY_TYPE_SPECIFIER_MAP_PASS_THRU,
		{PROPERTY_VALUE_PROP_ENCODED_ARRAY}},
	{"-cells", PROPERTY_TYPE_SPECIFIER_CELLS, {PROPERTY_VALUE_U32}},
	{"other", PROPERTY_TYPE_NONE, {PROPERTY_VALUE_EMPTY}},
};
enum property_type_enum	get_property_type(const char *property_name)
{
	int i = 1;
	if (!property_name)
		return (PROPERTY_TYPE_NONE);
	for (; i < PROPERTY_TYPE_SPECIFIER_MAP; ++i)
	{
		if (!strcmp(property_types[i].property_string, property_name))
			return (i);
	}
	for (; i < PROPERTY_TYPE_OTHER; ++i)
	{
		int len_src = strlen(property_name);
		int len_cmp = strlen(property_types[i].property_string);
		if (len_cmp > len_src)
			continue ;
		if (!strcmp(property_types[i].property_string, property_name + len_src
				- len_cmp))
			return (i);
	}
	return (PROPERTY_TYPE_OTHER);
}