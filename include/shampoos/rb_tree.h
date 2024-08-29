#ifndef _SHAMPOOS_RB_H_
#define _SHAMPOOS_RB_H_
#include <common/types.h>
/*my implementation of a red black tree,of course it referenced the Linux*/
struct rb_node {
        u64 rb_parent_color;
#define RB_RED   0
#define RB_BLACK 1
        struct rb_node* left_child;
        struct rb_node* right_child;
        u64 black_height;
};
__attribute__((aligned(sizeof(u64))))

struct rb_root {
        struct rb_node* rb_root;
};
#define RB_HIGHT(rb_p)   ((rb_p) ? (rb_p)->black_height : -1)
#define RB_COLOR(rb_p)   ((rb_p)->rb_parent_color & 0x1)
#define RB_ISBLACK(rb_p) (!(rb_p) || RB_COLOR(rb_p) == RB_BLACK)
#define RB_ISRED(rb_p)   (!ISBLACK(rb_p))
#define RB_PARENT(rb_p)  ((struct rb_node*)((rb_p)->rb_parent_color & ~3))
#define RB_HIGHT_UPDATED(rb_p)                                        \
        (RB_HIGHT(rb_p->left_child) == RB_HIGHT(rb_p->right_child)    \
         && rb_p->black_height                                        \
                    == (RB_ISRED(rb_p) ? RB_HIGHT(rb_p->left_child) : \
                                         (RB_HIGHT(rb_p->left_child) + 1)))

#define RB_ISROOT(rb_p) (!(RB_PARENT(rb_p)))
#define RB_ISLCHILD(rb_p) \
        ((RB_PARENT(rb_p)) && ((rb_p) == (RB_PARENT(rb_p))->left_child))
#define RB_ISRCHILD(rb_p) \
        ((RB_PARENT(rb_p)) && ((rb_p) == (RB_PARENT(rb_p))->right_child))
#define RB_HASCHILD(rb_p)     ((rb_p)->left_child || (rb_p)->right_child)
#define RB_HASBOTHCHILD(rb_p) ((rb_p)->left_child && (rb_p)->right_child)

#define RB_SIBLING(rb_p)                                    \
        (RB_ISLCHILD(rb_p) ? RB_PARENT(rb_p)->right_child : \
                             RB_PARENT(rb_p)->left_child)
#define RB_UNCLE(rb_p)                                       \
        (RB_ISLCHILD(RB_PARENT(rb_p)) ?                      \
                 (RB_PARENT(RB_PARENT(rb_p)))->right_child : \
                 (RB_PARENT(RB_PARENT(rb_p)))->left_child)

#define RB_SET_RED(rb_p)                       \
        do {                                   \
                (rb_p)->rb_parent_color &= ~1; \
        } while (0)
#define RB_SET_BLACK(rb_p)                    \
        do {                                  \
                (rb_p)->rb_parent_color |= 1; \
        } while (0)
#define RB_SET_PARENT(rb_p, parent)                                     \
        do {                                                            \
                (rb_p)->rb_parent_color = RB_COLOR(rb_p) | (u64)parent; \
        } while (0)
u64 update_height(struct rb_node* rb_p);

void rb_remove(struct rb_node* rb_p, struct rb_root* root);
void rb_insert(struct rb_node* rb_p);
struct rb_node* rb_prev(struct rb_node* rb_p);
struct rb_node* rb_next(struct rb_node* rb_p);
void rb_link_node(struct rb_node* node, struct rb_node* parent,
                  struct rb_node** rb_link);
void RB_SolveDoubleRed(struct rb_node* rb_p, struct rb_root* root);
void RB_SolveDoubleBlack(struct rb_node* rb_p);
#endif