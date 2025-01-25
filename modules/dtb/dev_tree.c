#include <modules/dtb/dtb.h>
#include <modules/log/log.h>
#include <shampoos/error.h>
extern struct device_node* device_root;
extern struct property_type property_types[PROPERTY_TYPE_NUM];
void _print_device_tree(struct device_node* node, int depth)
{
        /*print the device tree below node*/
        /*print this node*/
        rep_print(depth, '\t', pr_info);
        pr_info("%s{\n", node->name);
        // pr_info("type:%s\n", node->type);
        /*print this node's property*/
        struct property* prop = node->property;
        while (prop) {
                rep_print(depth + 1, '\t', pr_info);
                pr_info("%s\t:\t", prop->name);
                print_property_value(prop->name, prop->data, prop->len);
                pr_info("\n");
                prop = prop->next;
        }

        struct device_node* child = node->child;
        while (child) {
                /*print childs*/
                _print_device_tree(child, depth + 1);
                child = child->sibling;
        }
        rep_print(depth, '\t', pr_info);
        pr_info("}\n");
}
void print_device_tree(struct device_node* node)
{
        if (!node)
                node = device_root;
        _print_device_tree(node, 0);
}

/*device node part: search a node*/
struct device_node* _dev_node_find(struct device_node* node,
                                   char* search_string,
                                   enum dev_node_find_way way)
{
        if (!node || !search_string)
                return NULL;
        switch (way) {
        case _dev_node_find_by_name: {
                if (!strcmp(node->name, search_string))
                        return node;
                break;
        }
        case _dev_node_find_by_type: {
                struct property* prop = dev_node_find_property(
                        node,
                        property_types[PROPERTY_TYPE_DEVICE_TYPE]
                                .property_string,
                        12);
                if (prop && !strcmp(prop->data, search_string))
                        return node;
                break;
        }
        case _dev_node_find_by_compatible: {
                struct property* prop = dev_node_find_property(
                        node,
                        property_types[PROPERTY_TYPE_COMPATIBLE].property_string,
                        11);
                /*
                        here compatible data is a string list
                        which means we have to compare more then once
                        and the point is the two string conjuncted with ','
                        is one compatible string
                        two compatible string is seperated by '\0'
                */
                if (prop) {
                        char* src_ch = search_string;
                        char* dst_ch = prop->data;
                        char* dst_ch_end = prop->data + prop->len;
                        while (dst_ch < dst_ch_end) {
                                if (*src_ch != *dst_ch) {
                                        if (!*src_ch) {
                                                src_ch = search_string;
                                                while (*dst_ch)
                                                        dst_ch++;
                                                continue;
                                        } else {
                                                if (!*dst_ch) {
                                                        src_ch = search_string;
                                                } else {
                                                        src_ch = search_string;
                                                        while (*dst_ch)
                                                                dst_ch++;
                                                        continue;
                                                }
                                        }
                                } else if (!*src_ch) {
                                        return node;
                                }
                                src_ch++;
                                dst_ch++;
                        }
                }
                break;
        }
        default:
                pr_error("[ ERROR ] invalide dev node find way\n");
                return NULL;
        }

        struct device_node* res = NULL;
        /*first search childs,if have*/
        struct device_node* search = NULL;

        if (node->child) {
                search = node->child;
        } else if (node->sibling) {
                search = node->sibling;
        } else {
                search = node;
                while (search->parent && !search->parent->sibling)
                        search = search->parent;
                if (search->parent) {
                        search = search->parent->sibling;
                } else { /*this if-else is used for root*/
                        search = NULL;
                }
        }

        if (search) {
                res = _dev_node_find(search, search_string, way);
                if (res)
                        goto final;
        }
        /*finally we think cannot find one*/
final:
        return res;
}
struct device_node* dev_node_find_by_name(struct device_node* node,
                                          char* dev_node_name)
{
        if (!node) {
                node = device_root;
        }
        return _dev_node_find(node, dev_node_name, _dev_node_find_by_name);
}
struct device_node* dev_node_find_by_type(struct device_node* node,
                                          char* type_name)
{
        if (!node) {
                node = device_root;
        }
        return _dev_node_find(node, type_name, _dev_node_find_by_type);
}
struct device_node* dev_node_find_by_compatible(struct device_node* node,
                                                char* compatible_name)
{
        if (!node) {
                node = device_root;
        }
        return _dev_node_find(
                node, compatible_name, _dev_node_find_by_compatible);
}
/*after we find a node, we should read the property*/
struct property* dev_node_find_property(const struct device_node* node,
                                        char* prop_name, int n)
{
        if (!node || !prop_name || !n) {
                return NULL;
        }
        struct property* search = node->property;
        while (search) {
                if (!strcmp_s(prop_name, search->name, n)) {
                        return search;
                }
                search = search->next;
        }
        return NULL;
}
/*when we get the property, we should read the property value*/
error_t property_read_string(const struct property* prop, char** str)
{
        if (!prop || !str)
                return -EPERM;
        *str = prop->data;
        return 0;
}

error_t property_read_u8_arr(const struct property* prop, u8** arr, int n)
{
        if (!prop || !arr || !n)
                return -EPERM;
        if (n > prop->len)
                n = prop->len;
        memcpy(*arr, prop->data, n);
        return 0;
}
error_t property_read_u16_arr(const struct property* prop, u16** arr, int n)
{
        if (!prop || !arr || !n)
                return -EPERM;
        if ((n * sizeof(u16)) > prop->len)
                n = prop->len / sizeof(u16);
        memcpy(*arr, prop->data, n * sizeof(u16));
        return 0;
}
error_t property_read_u32_arr(const struct property* prop, u32** arr, int n)
{
        if (!prop || !arr || !n)
                return -EPERM;
        if ((n * sizeof(u32)) > prop->len)
                n = prop->len / sizeof(u32);
        memcpy(*arr, prop->data, n * sizeof(u32));
        return 0;
}
error_t property_read_u64_arr(const struct property* prop, u64** arr, int n)
{
        if (!prop || !arr || !n)
                return -EPERM;
        if ((n * sizeof(u64)) > prop->len)
                n = prop->len / sizeof(u64);
        memcpy(*arr, prop->data, n * sizeof(u64));
        return 0;
}

error_t property_read_u8(const struct property* prop, u8* value)
{
        if (!prop || !value)
                return -EPERM;
        *value = *((u8*)(prop->data));
        return 0;
}
error_t property_read_u16(const struct property* prop, u16* value)
{
        if (!prop || !value)
                return -EPERM;
        *value = *((u16*)(prop->data));
        return 0;
}
error_t property_read_u32(const struct property* prop, u32* value)
{
        if (!prop || !value)
                return -EPERM;
        *value = *((u32*)(prop->data));
        return 0;
}
error_t property_read_u64(const struct property* prop, u64* value)
{
        if (!prop || !value)
                return -EPERM;
        *value = *((u64*)(prop->data));
        return 0;
}