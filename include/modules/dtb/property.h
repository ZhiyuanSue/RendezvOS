#ifndef _SHAMPOOS_PROPERTY_H_
#define _SHAMPOOS_PROPERTY_H_
#include <common/types.h>
enum property_value_type{
	PROPERTY_VALUE_EMPTY,
	PROPERTY_VALUE_U32,
	PROPERTY_VALUE_U64,
	PROPERTY_VALUE_STRING,
	PROPERTY_VALUE_PROP_ENCODED_ARRAY,
	PROPERTY_VALUE_PHANDLE,
	PROPERTY_VALUE_STRINGLIST,
};
struct property_type{
	char property_string[20];
	int value_enum;
};
void print_property_value_empty(void* data,uint32_t len);
void print_property_value_u32(void* data,uint32_t len);
void print_property_value_u64(void* data,uint32_t len);
void print_property_value_string(void* data,uint32_t len);
void print_property_value_prop_encoded_array(void* data,uint32_t len);
void print_property_value_phandle(void* data,uint32_t len);
void print_property_value_stringlist(void* data,uint32_t len);

void print_property_value(const char* property_name,void* data,uint32_t len);

#endif