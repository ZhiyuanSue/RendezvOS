#include <shampoos/rb_tree.h>
#include <common/stddef.h>
#include <modules/log/log.h>
u64 update_height(struct rb_node* rb_p)
{
        rb_p->black_height =
                MAX(RB_HIGHT(rb_p->left_child), RB_HIGHT(rb_p->right_child));
        return RB_ISBLACK(rb_p) ? rb_p->black_height++ : rb_p->black_height;
}

void rb_link_node(struct rb_node* node, struct rb_node* parent,
                  struct rb_node** rb_link)
{
        RB_SET_PARENT(node, parent);
        node->left_child = node->right_child = NULL;
        *rb_link = node;
}
struct rb_node* rb_prev(struct rb_node* rb_p)
{
        if (!rb_p)
                return NULL;
        struct rb_node* p = rb_p;
        if (p->left_child) {
                p = p->left_child;
                while (RB_HASRCHILD(p))
                        p = p->right_child;
        } else {
                while (RB_ISLCHILD(p))
                        p = RB_PARENT(p);
                p = RB_PARENT(p);
        }
        return p;
}
struct rb_node* rb_next(struct rb_node* rb_p)
{
        if (!rb_p)
                return NULL;
        struct rb_node* s = rb_p;
        if (s->right_child) {
                s = s->right_child;
                while (RB_HASLCHILD(s))
                        s = s->left_child;
        } else {
                while (RB_ISRCHILD(s))
                        s = RB_PARENT(s);
                s = RB_PARENT(s);
        }
        return s;
}
struct rb_node* rb_removeAt(struct rb_node* rb_p, struct rb_root* root,
                            struct rb_node** _hot)
{
        struct rb_node* del = rb_p;
        struct rb_node* succ = NULL;
        struct rb_node* parent = RB_PARENT(rb_p);
        pr_info("remove at %d\n", rb_p->id);
        if (!RB_HASLCHILD(rb_p)) {
                succ = rb_p->right_child;
                if (!parent)
                        root->rb_root = succ;
                else {
                        if (RB_ISLCHILD(rb_p))
                                parent->left_child = succ;
                        else
                                parent->right_child = succ;
                }
        } else if (!RB_HASRCHILD(rb_p)) {
                pr_info("have left child\n");
                succ = rb_p->left_child;
                if (!parent)
                        root->rb_root = succ;
                else {
                        pr_info("have parent %d\n", parent->id);
                        if (RB_ISLCHILD(rb_p))
                                parent->left_child = succ;
                        else
                                parent->right_child = succ;
                }
        } else {
                /*if have both child, we del the next node
                but we don't really delete it, we need do 4 part
                1/let the parent of the del node point to the next node
                2/let the parent of the next node point to the del
                node->right_child (for next node ,it must have no left child)
                3/let the next node child point to the both child of the del
                node's child, and both child's parent point to next node
                4/let the next node's parent point to the del node's parent
                */
                del = rb_next(del);
                // part 2
                struct rb_node* del_par = RB_PARENT(del);
                if (del_par == rb_p) {
                        succ = del;
                        del->left_child = rb_p->left_child;
                        RB_SET_PARENT(del->left_child, del);
                } else {
                        del_par->left_child = succ = del->right_child;
                        // part 3
                        del->left_child = rb_p->left_child;
                        del->right_child = rb_p->right_child;
                        pr_info("set %d\n", del->id);
                        if (del->left_child)
                                RB_SET_PARENT(del->left_child, del);
                        if (del->right_child)
                                RB_SET_PARENT(del->right_child, del);
                }
                // part 1
                if (!parent) {
                        root->rb_root = del;
                        RB_SET_PARENT(del, NULL);
                } else {
                        pr_info("both child set %d's parent %d\n",
                                del->id,
                                parent->id);
                        if (RB_ISLCHILD(rb_p))
                                parent->left_child = del;
                        else
                                parent->right_child = del;
                        RB_SET_PARENT(del, parent);
                }
        }
        *_hot = RB_PARENT(del);
        if (succ) {
                pr_info("succ set %d", succ->id);
                if (*_hot) {
                        pr_info("'s parent %d\n", (*_hot)->id);
                } else {
                        pr_info("'s parent null\n");
                }
                RB_SET_PARENT(succ, (*_hot));
        }

        return succ;
}

void rb_remove(struct rb_node* rb_p, struct rb_root* root)
{
        if (!rb_p || !root || !root->rb_root)
                return;
        struct rb_node* _hot = NULL;
        pr_debug("start remove at\n");
        struct rb_node* succ = rb_removeAt(rb_p, root, &_hot);
        pr_debug("end remove at\n");
        if (!(root->rb_root))
                return; // empty
        if (!_hot) // the root is deleted
        {
                RB_SET_BLACK(root->rb_root);
                update_height(root->rb_root);
                return;
        }
        if (RB_HIGHT_UPDATED(_hot))
                return;
        if (RB_ISRED(succ)) {
                RB_SET_BLACK(succ);
                succ->black_height++;
                return;
        }
        pr_debug("start rb solvedouble black\n");
        RB_SolveDoubleBlack(succ, root, _hot);
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
        pr_info("rotateAt\n");
        struct rb_node* p = RB_PARENT(v);
        pr_debug("connect 34 p\n");
        struct rb_node* g = RB_PARENT(p);
        pr_debug("connect 34 g\n");
        if (RB_ISLCHILD(p)) {
                if (RB_ISLCHILD(v)) {
                        pr_debug("connect 34 1\n");
                        RB_SET_PARENT(p, RB_PARENT(g));
                        pr_debug("connect 34 1\n");
                        return connect34(v,
                                         p,
                                         g,
                                         v->left_child,
                                         v->right_child,
                                         p->right_child,
                                         g->right_child);
                } else {
                        pr_debug("connect 34 2\n");
                        RB_SET_PARENT(v, RB_PARENT(g));
                        pr_debug("connect 34 2\n");
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
                        pr_debug("connect 34 3\n");
                        RB_SET_PARENT(p, RB_PARENT(g));
                        pr_debug("connect 34 3\n");
                        return connect34(g,
                                         p,
                                         v,
                                         g->left_child,
                                         p->left_child,
                                         v->left_child,
                                         v->right_child);
                } else {
                        pr_debug("connect 34 4\n");
                        RB_SET_PARENT(v, RB_PARENT(g));
                        pr_debug("connect 34 4\n");
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
                struct rb_node* r;
                if (RB_ISROOT(g)) {
                        root->rb_root = r = rotateAt(rb_p);
                } else {
                        if (RB_ISLCHILD(g)) {
                                gg->left_child = r = rotateAt(rb_p);
                        } else {
                                gg->right_child = r = rotateAt(rb_p);
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
void RB_SolveDoubleBlack(struct rb_node* rb_p, struct rb_root* root,
                         struct rb_node* _hot)
{
        pr_debug("[double black]");
        if (_hot) {
                pr_info("[Hot:%d]\n", _hot->id);
        } else {
                pr_info("[Hot:null]\n");
        }
        struct rb_node* p = rb_p ? RB_PARENT(rb_p) : _hot;
        if (!p)
                return;
        pr_debug("[double black] 1\n");
        struct rb_node* s = (rb_p == p->left_child) ? p->right_child :
                                                      p->left_child;
        pr_debug("[double black] 2\n");
        if (RB_ISBLACK(s)) {
                pr_debug("[double black] s %d is black\n", s->id);
                struct rb_node* t = NULL;
                if (RB_HASLCHILD(s) && RB_ISRED(s->left_child))
                        t = s->left_child;
                else if (RB_HASRCHILD(s) && RB_ISRED(s->right_child))
                        t = s->right_child;
                pr_debug("[double black] 3\n");
                if (t) {
                        pr_debug("[double black] t\n");
                        bool oldColor = RB_COLOR(p);
                        struct rb_node* b;
                        if (RB_ISROOT(p)) {
                                root->rb_root = b = rotateAt(t);
                        } else {
                                if (RB_ISLCHILD(p)) {
                                        RB_PARENT(p)->left_child = b =
                                                rotateAt(t);
                                } else {
                                        RB_PARENT(p)->right_child = b =
                                                rotateAt(t);
                                }
                        }
                        if (RB_HASLCHILD(b))
                                RB_SET_BLACK(b->left_child);
                        if (RB_HASRCHILD(b))
                                RB_SET_BLACK(b->right_child);
                        if (oldColor)
                                RB_SET_BLACK(b);
                        else
                                RB_SET_RED(b);
                } else {
                        RB_SET_RED(s);
                        if (RB_ISRED(p)) {
                                RB_SET_BLACK(p);
                        } else {
                                RB_SolveDoubleBlack(p, root, _hot);
                        }
                }
        } else {
                pr_debug("[double black] s %d is red\n", s->id);
                RB_SET_BLACK(s);
                pr_debug("[double black] 4\n");
                RB_SET_RED(p);
                pr_debug("[double black] 5\n");
                struct rb_node* t = RB_ISLCHILD(s) ? s->left_child :
                                                     s->right_child;
                pr_debug("[double black] 6\n");
                if (t == NULL)
                        pr_debug("t is null\n");
                _hot = p;
                if (RB_ISROOT(p)) {
                        pr_debug("[double black] root\n");
                        root->rb_root = rotateAt(t);
                } else {
                        if (RB_ISLCHILD(p)) {
                                pr_debug("[double black] lchild\n");
                                RB_PARENT(p)->left_child = rotateAt(t);
                        } else {
                                pr_debug("[double black] rchild\n");
                                RB_PARENT(p)->right_child = rotateAt(t);
                        }
                }
                pr_debug("[double black] next double black\n");
                RB_SolveDoubleBlack(rb_p, root, _hot);
        }
}