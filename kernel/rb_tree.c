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
static struct rb_node* connect34(struct rb_node* a, struct rb_node* b,
                                 struct rb_node* c, struct rb_node* T0,
                                 struct rb_node* T1, struct rb_node* T2,
                                 struct rb_node* T3)
{
        a->left_child = T0;
        if (T0)
                RB_SET_PARENT(T0, a);
        a->right_child = T1;
        if (T1)
                RB_SET_PARENT(T1, a);
        update_height(a);

        c->left_child = T2;
        if (T2)
                RB_SET_PARENT(T2, c);
        c->right_child = T3;
        if (T3)
                RB_SET_PARENT(T3, c);
        update_height(c);

        b->left_child = a;
        RB_SET_PARENT(a, b);
        b->right_child = c;
        RB_SET_PARENT(c, b);
        update_height(b);

        return b;
}
static struct rb_node* rotateAt(struct rb_node* v)
{
        struct rb_node* p = RB_PARENT(v);
        struct rb_node* g = RB_PARENT(p);
        if (RB_ISLCHILD(p)) {
                if (RB_ISLCHILD(v)) {
                        RB_SET_PARENT(p, RB_PARENT(g));
                        return connect34(v,
                                         p,
                                         g,
                                         v->left_child,
                                         v->right_child,
                                         p->right_child,
                                         g->right_child);
                } else {
                        RB_SET_PARENT(v, RB_PARENT(g));
                        return connect34(p,
                                         v,
                                         g,
                                         p->left_child,
                                         v->left_child,
                                         v->right_child,
                                         g->right_child);
                }
        } else {
                if (RB_ISRCHILD(v)) {
                        RB_SET_PARENT(p, RB_PARENT(g));
                        return connect34(g,
                                         p,
                                         v,
                                         g->left_child,
                                         p->left_child,
                                         v->left_child,
                                         v->right_child);
                } else {
                        RB_SET_PARENT(v, RB_PARENT(g));
                        return connect34(g,
                                         v,
                                         p,
                                         g->left_child,
                                         v->left_child,
                                         v->right_child,
                                         p->right_child);
                }
        }
}
void RB_SolveDoubleRed(struct rb_node* rb_p, struct rb_root* root)
{
        if (RB_ISROOT(rb_p)) {
                RB_SET_BLACK(rb_p);
                rb_p->black_height++;
                return;
        }
        struct rb_node* p = (struct rb_node*)RB_PARENT(rb_p);
        if (RB_ISBLACK(p))
                return;
        struct rb_node* g = (struct rb_node*)RB_PARENT(p);
        struct rb_node* u = (struct rb_node*)RB_UNCLE(rb_p);
        if (RB_ISBLACK(u)) {
                if (RB_ISLCHILD(rb_p) == RB_ISLCHILD(p))
                        RB_SET_BLACK(p);
                else
                        RB_SET_BLACK(rb_p);
                RB_SET_RED(g);
                struct rb_node* gg = (struct rb_node*)RB_PARENT(g);
                struct rb_node* r = rotateAt(rb_p);

                if (RB_ISROOT(rb_p)) {
                        root->rb_root = r;
                } else {
                        if (RB_ISLCHILD(rb_p)) {
                                (RB_PARENT(rb_p))->left_child = r;
                        } else {
                                (RB_PARENT(rb_p))->right_child = r;
                        }
                }
                RB_SET_PARENT(r, gg);
        } else {
                RB_SET_BLACK(p);
                p->black_height++;
                RB_SET_BLACK(u);
                u->black_height++;
                if (!RB_ISROOT(g))
                        RB_SET_RED(g);
                RB_SolveDoubleRed(g, root);
        }
}
void RB_SolveDoubleBlack(struct rb_node* rb_p)
{
	
}