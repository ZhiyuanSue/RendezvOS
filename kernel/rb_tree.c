#include <shampoos/rb_tree.h>
#include <common/stddef.h>
u64 update_height(struct rb_node* rb_p)
{
        rb_p->black_height = MAX(rb_p->left_child->black_height,
                                 rb_p->right_child->black_height);
        return RB_ISBLACK(rb_p) ? rb_p->black_height++ : rb_p->black_height;
}
void rb_remove(struct rb_node* rb_p)
{
}
void rb_insert(struct rb_node* rb_p)
{
}
struct rb_node* rb_prev(struct rb_node* rb_p)
{
}
struct rb_node* rb_next(struct rb_node* rb_p)
{
}