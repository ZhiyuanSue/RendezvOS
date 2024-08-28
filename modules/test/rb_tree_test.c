#include <modules/test/test.h>
#include <shampoos/rb_tree.h>
#include <common/stddef.h>
/*as we might have no kmalloc to use ,we just static alloc a lot of rb_nodes
 * array*/
/*besides, unlike the tranditional rb tree,
 the linux rb tree need you to define your own data struct
 and it's also an example of the usage of the rb tree*/
#define max_node_num 256
struct t_node {
        struct rb_node rb;
        int key;
};
struct t_node node_list[max_node_num];
int next_node = 0;
struct t_node* alloc_node()
{
        struct t_node* node = &node_list[next_node];
        next_node++;
        return node;
}

struct t_node* test_search(struct rb_root* root, int key)
{
        struct rb_node* node = root->rb_root;
        while (node) {
                struct t_node* test_data =
                        container_of(node, struct t_node, rb);
                if (key > test_data->key) {
                        node = node->left_child;
                } else if (key < test_data->key) {
                        node = node->right_child;
                } else {
                        return test_data;
                }
        }
        return NULL;
}

void rb_tree_test(void)
{
}