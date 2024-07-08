#include <modules/dtb/print_property.h>
#include <modules/log/log.h>
extern struct property_type property_types[255];
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

void print_property_value(const char* property_name,void* data,uint32_t len){
	enum property_type_enum p_type = get_property_type(property_name);
    pr_info("%s",property_types[p_type].property_string);
}