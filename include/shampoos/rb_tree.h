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
	u64 black_height;
};
__attribute__((aligned(sizeof(u64))))

struct rb_root {
    struct rb_node *rb_node;
};
#define RB_COLOR(rb_p) ((rb_p)->rb_parent_color & 0x1)
#define RB_ISBLACK(rb_p) (!(rb_p) || RB_COLOR(rb_p) == RB_BLACK)
#define RB_ISRED(rb_p) (!ISBLACK(rb_p))
#define RB_PARENT(rb_p) ((struct rb_node*)((rb_p)->rb_parent_color & ~3))
#define RB_SET_RED(rb_p) do { (rb_p)->rb_parent_color &= ~1; } while(0)
#define RB_SET_BLACK(rb_p) do { (rb_p)->rb_parent_color |= 1;} while(0)
u64 update_height(struct rb_node* rb_p);

void rb_remove(struct rb_node* rb_p);
void rb_insert(struct rb_node* rb_p);
struct rb_node* rb_prev(struct rb_node* rb_p);
struct rb_node* rb_next(struct rb_node* rb_p);
#endif