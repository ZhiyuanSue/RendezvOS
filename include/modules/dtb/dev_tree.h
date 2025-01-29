/*
    here we must read the data from dtb
    and build a device node tree like linux
    and use that to manage the device tree
    I think this doc seems helpful
    https://developer.aliyun.com/article/1437597
    I don't want to change too much
    because the format of dtb is all the same
*/
#ifndef _SHAMPOOS_DEV_TREE_H_
#define _SHAMPOOS_DEV_TREE_H_

#include <common/types.h>
struct device_node {
        char* name;
        struct property* property;
        struct { /*for the tree*/
                struct device_node* parent;
                struct device_node* child;
                struct device_node* sibling;
        };
} __attribute__((packed));

struct property {
        char* name;
        void* data;
        int len;
        struct property* next;
} __attribute__((packed));

void print_device_tree(struct device_node* node);

/*device node part: search a node*/
enum dev_node_find_way {
        _dev_node_find_by_name = 1,
        _dev_node_find_by_type = 2,
        _dev_node_find_by_compatible = 3,
};
struct device_node* dev_node_find_by_name(struct device_node* node,
                                          char* dev_node_name);
struct device_node* dev_node_find_by_type(struct device_node* node,
                                          char* type_name);
struct device_node* dev_node_find_by_compatible(struct device_node* node,
                                                char* compatible_name);
struct device_node* dev_node_get_first_child(struct device_node* node);
struct device_node* dev_node_get_sibling(struct device_node* node);
/*after we find a node, we should read the property*/
struct property* dev_node_find_property(const struct device_node* node,
                                        char* prop_name, int n);
/*when we get the property, we should read the property value*/
error_t property_read_string(const struct property* prop, char** str);

error_t property_read_u8_arr(const struct property* prop, u8** arr, int n);
error_t property_read_u16_arr(const struct property* prop, u16** arr, int n);
error_t property_read_u32_arr(const struct property* prop, u32** arr, int n);
error_t property_read_u64_arr(const struct property* prop, u64** arr, int n);

error_t property_read_u8(const struct property* prop, u8* value);
error_t property_read_u16(const struct property* prop, u16* value);
error_t property_read_u32(const struct property* prop, u32* value);
error_t property_read_u64(const struct property* prop, u64* value);

#endif