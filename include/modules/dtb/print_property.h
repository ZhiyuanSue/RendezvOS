#ifndef _SHAMPOOS_PRINT_PROPERTY_H_
#define _SHAMPOOS_PRINT_PROPERTY_H_
#include "property.h"

void print_property_value_empty(void* data,uint32_t len);
void print_property_value_u32(void* data,uint32_t len);
void print_property_value_u64(void* data,uint32_t len);
void print_property_value_string(void* data,uint32_t len);
void print_property_value_prop_encoded_array(void* data,uint32_t len);
void print_property_value_phandle(void* data,uint32_t len);
void print_property_value_stringlist(void* data,uint32_t len);

void print_property_value(const char* property_name,void* data,uint32_t len);


#endif