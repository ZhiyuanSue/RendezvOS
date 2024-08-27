#ifndef _SHAMPOOS_RB_H_
#define _SHAMPOOS_RB_H_
#include <common/types.h>
/*my implementation of a red black tree,of course it referenced the Linux*/
struct rb_node {
    u64 rb_parent_color;
#define RB_RED 0
#define RB_BLACK 1
    struct rb_node *left_child;
    struct rb_node *right_child;
};
__attribute__((aligned(sizeof(u64))))

struct rb_root {
    struct rb_node *rb_node;
};
#define ISBLACK(rb_p) (!(rb_p) || ((rb_p)->rb_parend_color & 0x1) == RB_BLACK)
#define ISRED(rb_p) (!ISBLACK(rb_p))
#endif