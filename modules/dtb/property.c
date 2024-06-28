#include <modules/dtb/property.h>
#include <modules/log/log.h>

void print_property_value_empty(void* data,uint32_t len){

}
void print_property_value_u32(void* data,uint32_t len){

}
void print_property_value_u64(void* data,uint32_t len){

}
void print_property_value_string(void* data,uint32_t len){
	pr_info("%s\n",data);
}
void print_property_value_prop_encoded_array(void* data,uint32_t len){

}
void print_property_value_phandle(void* data,uint32_t len){

}
void print_property_value_stringlist(void* data,uint32_t len){

}

void (*print_property_value_list[7])(void* data,uint32_t len) = {
	print_property_value_empty,
	print_property_value_u32,
	print_property_value_u64,
	print_property_value_string,
	print_property_value_prop_encoded_array,
	print_property_value_phandle,
	print_property_value_stringlist
};

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

void print_property_value(const char* property_name,void* data,uint32_t len){
	
}