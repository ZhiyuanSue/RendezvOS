#include <modules/dtb/property.h>

struct property_type property_types[255]={
	{"compatible"			,	PROPERTY_VALUE_STRINGLIST},
	{"model"				,	PROPERTY_VALUE_STRING},
	{"phandle"				,	PROPERTY_VALUE_U32},
	{"status"				,	PROPERTY_VALUE_STRING},
	{"#address-cells"		,	PROPERTY_VALUE_U32},
	{"#size-cells"			,	PROPERTY_VALUE_U32},
	{"reg"					,	PROPERTY_VALUE_PROP_ENCODED_ARRAY},
	{"virtual-reg"			,	PROPERTY_VALUE_U32},
	{"ranges"				,	PROPERTY_VALUE_PROP_ENCODED_ARRAY},
	{"dma-ranges"			,	PROPERTY_VALUE_PROP_ENCODED_ARRAY},
	{"dma-coherent"			,	PROPERTY_VALUE_EMPTY},
	{"dma-noncoherent"		,	PROPERTY_VALUE_EMPTY},
	{"name"					,	PROPERTY_VALUE_STRING},
	{"device_type"			,	PROPERTY_VALUE_STRING},
	{"interrupts"			,	PROPERTY_VALUE_PROP_ENCODED_ARRAY},

};