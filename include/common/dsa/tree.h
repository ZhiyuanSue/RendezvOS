#ifndef _RENDEZVOS_TREE_H_
#define _RENDEZVOS_TREE_H_
#include <common/types.h>
#include <common/stdbool.h>
#include <common/stddef.h>
struct tree_node {
        struct tree_node* parent;
        struct tree_node* child;
        struct tree_node* last_child;
        struct tree_node* sibling;
};
#define for_each_tree_node_child(parent_tree_node_ptr)               \
        for (struct tree_node* t_node = parent_tree_node_ptr->child; \
             t_node != NULL;                                         \
             t_node = t_node->sibling)
static inline void tree_node_insert(struct tree_node* parent,
                                    struct tree_node* child)
{
        if (!child) /*no insert node*/
                return;
        if (parent) {
                if (!parent->child) {
                        parent->child = parent->last_child = child;
                } else {
                        parent->last_child->sibling = child;
                }
                parent->last_child = child;
        }
        child->parent = parent;
}
static inline struct tree_node* tree_node_get_next(struct tree_node* t_node)
{
        struct tree_node* next = (struct tree_node*)NULL;

        if (!t_node)
                goto return_next;

        if (t_node->child) {
                next = t_node->child;
        } else if (t_node->sibling) {
                next = t_node->sibling;
        } else {
                next = t_node;
                while (next->parent && !next->parent->sibling)
                        next = next->parent;
                if (next->parent) {
                        next = next->parent->sibling;
                } else {
                        next = (struct tree_node*)NULL;
                }
        }
return_next:
        return next;
}

#endif