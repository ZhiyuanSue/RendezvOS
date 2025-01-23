#include <modules/dtb/dtb.h>
#include <modules/log/log.h>

extern struct device_node* device_root;
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