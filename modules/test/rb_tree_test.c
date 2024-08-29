#include <modules/test/test.h>
#include <shampoos/rb_tree.h>
#include <common/stddef.h>
#include <modules/log/log.h>
/*as we might have no kmalloc to use ,we just static alloc a lot of rb_nodes
 * array*/
/*besides, unlike the tranditional rb tree,
 the linux rb tree need you to define your own data struct
 and it's also an example of the usage of the rb tree*/
#define max_node_num 256
#define max_loops    256
u64 next = 1;
struct t_node {
        struct rb_node rb;
        u32 key;
};
struct rb_root t_root = {NULL};
struct t_node node_list[max_node_num];

bool check(int nr_nodes)
{
        if (t_root.rb_root == NULL)
                return true;
        if (RB_COLOR(t_root.rb_root) != RB_BLACK) {
                pr_error("the root is not black\n");
                return false;
        }
        int height;
        int count = 0;
        bool res = check_rb(t_root.rb_root, &height, &count);
        if (res == false || count != nr_nodes)
                return false;
        return true;
}
bool check_rb(struct rb_node* node, int* height, int* count)
{
        if (node == NULL) {
                *height = 0;
                return true;
        }
        int left_height, right_height;
        bool l = check_rb(node->left_child, &left_height, count);
        bool r = check_rb(node->right_child, &right_height, count);

        if (!l || !r)
                return false;
        /*check height*/
        if (left_height != right_height)
                return false;
        /*check color*/
        if ((RB_COLOR(node) == RB_RED)
            && (!RB_PARENT(node) || RB_COLOR(RB_PARENT(node)) == RB_RED))
                return false;
        /*update the height*/
        if (RB_COLOR(node) == RB_BLACK)
                *height++;
        count++;
        return true;
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
void rb_tree_test_insert()
{
        // TODO
}
void rb_tree_test_remove()
{
        // TODO
}
void rb_tree_test_init()
{
        for (int i = 0; i < max_node_num; i++) {
                next = (u64)next * 1103515245 + 12345;
                node_list[i].key = ((next / 65536) % 32768);
        }
}

void rb_tree_test(void)
{
        rb_tree_test_init();
        for (int i = 0; i < max_loops; i++) {
                for (int j = 0; j < max_node_num; j++) {
                        if (!check(j)) {
                                pr_error("rb tree test error\n");
                                return;
                        }
                        rb_tree_test_insert();
                }
                for (int j = 0; j < max_node_num; j++) {
                        if (!check(j)) {
                                pr_error("rb tree test error\n");
                                return;
                        }
                        rb_tree_test_remove();
                }
        }
        pr_info("rb tree test succ\n");
}