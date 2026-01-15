#include <modules/test/test.h>
#include <common/dsa/rb_tree.h>
#include <common/stddef.h>
#include <modules/log/log.h>
#include <common/rand.h>
/*as we might have no kmalloc to use ,we just static alloc a lot of rb_nodes
 * array*/
/*besides, unlike the tranditional rb tree,
 the linux rb tree need you to define your own data struct
 and it's also an example of the usage of the rb tree*/
#define max_node_num 128
#define max_loops    128
static u64 next = 1;
struct t_node {
        struct rb_node rb;
        u32 key;
};
struct rb_root t_root = {NULL};
struct t_node node_list[max_node_num];

bool check_rb(struct rb_node* node, int* height, int* count, int level)
{
        if (node == NULL) {
                *height = 0;
                return true;
        }
        int left_height, right_height;
        bool l = check_rb(node->left_child, &left_height, count, level + 1);
        bool r = check_rb(node->right_child, &right_height, count, level + 1);

        /*update the height*/
        (*height) = RB_COLOR(node) ? (left_height + 1) : left_height;
        (*count)++;

        if (!l || !r) {
                pr_error("l is %d and r is %d\n", l, r);
                return false;
        }

        /*check height*/
        if (left_height != right_height) {
                pr_error("height is unequal ,left %d,right %d\n",
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
        bool res = check_rb(t_root.rb_root, &height, &count, 0);
        if (res == false || count != nr_nodes) {
                pr_error("check count is %d nr_node is %d\n", count, nr_nodes);
                return false;
        }
        return true;
}
struct t_node* test_search(struct rb_root* root, u32 key)
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
        RB_Link_Node(&node->rb, parent, new);
        RB_SolveDoubleRed(&node->rb, root);
}
void rb_tree_test_remove(struct t_node* node, struct rb_root* root)
{
        RB_Remove(&node->rb, root);
}
void rb_tree_test_init(void)
{
        for (int i = 0; i < max_node_num; i++) {
                next = rand64(next);
                node_list[i].key = next;
                node_list[i].rb.left_child = node_list[i].rb.right_child = NULL;
                node_list[i].rb.black_height = 0;
                node_list[i].rb.rb_parent_color = 0;
        }
        t_root.rb_root = NULL;
}

int rb_tree_test(void)
{
        for (int i = 0; i < max_loops; i++) {
                rb_tree_test_init();
                for (int j = 0; j < max_node_num; j++) {
                        debug("====== insert round %d loop %d ======\n", j, i);
                        if (!check(j)) {
                                pr_error("rb tree test insert error\n");
                                return -E_REND_TEST;
                        }
                        rb_tree_test_insert(&node_list[j], &t_root);
                }
                debug("finish round %d insert\n", i);
                for (int j = max_node_num; j > 0; j--) {
                        debug("====== delete round %d loop %d ======\n", j, i);
                        if (!check(j)) {
                                pr_error("rb tree test remove error\n");
                                return -E_REND_TEST;
                        }
                        rb_tree_test_remove(&node_list[j - 1], &t_root);
                }
        }
        return REND_SUCCESS;
}