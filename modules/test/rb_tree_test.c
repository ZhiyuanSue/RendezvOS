#include <modules/test/test.h>
#include <shampoos/rb_tree.h>
/*as we might have no kmalloc to use ,we just static alloc a lot of rb_nodes array*/
#define max_node_num 256
struct t_node {
    struct rb_node rb;
    int            key;
};
struct t_node  node_list[ max_node_num ];
int            next_node = 0;
struct t_node *alloc_node() {
    struct t_node *node = &node_list[ next_node ];
    next_node++;
    return node;
}

void rb_tree_test(void) {
}