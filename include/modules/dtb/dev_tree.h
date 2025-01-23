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

#endif