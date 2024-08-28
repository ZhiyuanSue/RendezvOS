#ifndef _SHAMPOOS_PRINT_PROPERTY_H_
#define _SHAMPOOS_PRINT_PROPERTY_H_
#include "property.h"
#include <modules/log/log.h>

#define print_property pr_info

void print_property_value_empty(enum property_type_enum p_type, void *data,
                                uint32_t len);
void print_property_value_u32(enum property_type_enum p_type, void *data,
                              uint32_t len);
void print_property_value_u64(enum property_type_enum p_type, void *data,
                              uint32_t len);
void print_property_value_string(enum property_type_enum p_type, void *data,
                                 uint32_t len);
void print_property_value_prop_encoded_array(enum property_type_enum p_type,
                                             void *data, uint32_t len);
void print_property_value_phandle(enum property_type_enum p_type, void *data,
                                  uint32_t len);
void print_property_value_stringlist(enum property_type_enum p_type, void *data,
                                     uint32_t len);

void print_property_value(const char *property_name, void *data, uint32_t len);

void parse_print_dtb(void *fdt, int offset, int depth);

#endif