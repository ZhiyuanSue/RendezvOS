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
        u32 id;
};
struct rb_root t_root = {NULL};
struct t_node node_list[max_node_num];

bool check_rb(struct rb_node* node, int* height, int* count)
{
        pr_info("[check rb]\n");
        if (node == NULL) {
                *height = 0;
                return true;
        }
        int left_height, right_height;
        pr_info("check child left 0x%x right 0x%x\n",
                node->left_child,
                node->right_child);
        bool l = check_rb(node->left_child, &left_height, count);
        bool r = check_rb(node->right_child, &right_height, count);

        if (!l || !r) {
                pr_info("l is %d and r is %d\n", l, r);
                return false;
        }

        /*check height*/
        if (left_height != right_height) {
                pr_error("height is unequal,left %d,right %d\n",
                         left_height,
                         right_height);
                return false;
        }

        /*check color*/
        if ((RB_COLOR(node) == RB_RED)
            && (!RB_PARENT(node) || RB_COLOR(RB_PARENT(node)) == RB_RED)) {
                pr_error("double red error\n");
                return false;
        }
        /*update the height*/
        if (RB_COLOR(node) == RB_BLACK)
                (*height)++;
        (*count)++;
        return true;
}
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
        pr_info("go into check rb\n");
        bool res = check_rb(t_root.rb_root, &height, &count);
        if (res == false || count != nr_nodes) {
                pr_info("check count is %d nr_node is %d\n", count, nr_nodes);
                return false;
        }
        return true;
}
struct t_node* test_search(struct rb_root* root, int key)
{
        struct rb_node* node = root->rb_root;
        while (node) {
                struct t_node* test_data =
                        container_of(node, struct t_node, rb);
                if (key < test_data->key) {
                        node = node->left_child;
                } else if (key > test_data->key) {
                        node = node->right_child;
                } else {
                        return test_data;
                }
        }
        return NULL;
}
void rb_tree_test_insert(struct t_node* node, struct rb_root* root)
{
        struct rb_node** new = &root->rb_root, *parent = NULL;
        u32 key = node->key;
        while (*new) {
                parent = *new;
                struct t_node* test_data =
                        container_of(parent, struct t_node, rb);
                if (key < test_data->key)
                        new = &parent->left_child;
                else
                        new = &parent->right_child;
        }
        RB_SET_RED(&(node->rb));
        rb_link_node(&node->rb, parent, new);
        RB_SolveDoubleRed(&node->rb, root);
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
                node_list[i].rb.left_child = node_list[i].rb.right_child = NULL;
                node_list[i].rb.id = i;
        }
        pr_info("the first node address is 0x%x 0x%x\n",
                (u64)&node_list,
                (u64)&node_list[0].rb);
}

void rb_tree_test(void)
{
        rb_tree_test_init();
        for (int i = 0; i < max_loops; i++) {
                for (int j = 0; j < max_node_num; j++) {
                        pr_info("====== insert round %d ======\n", j);
                        if (!check(j)) {
                                pr_error("rb tree test insert error\n");
                                return;
                        }
                        pr_info("the 0 node color is %d\n",
                                RB_COLOR(&node_list[0].rb));
                        rb_tree_test_insert(&node_list[j], &t_root);
                }
                for (int j = 0; j < max_node_num; j++) {
                        if (!check(j)) {
                                pr_error("rb tree test remove error\n");
                                return;
                        }
                        rb_tree_test_remove();
                }
        }
        pr_info("rb tree test succ\n");
}