#ifndef _RENDEZVOS_LIST_H_
#define _RENDEZVOS_LIST_H_
#include <common/stdbool.h>
#include <common/types.h>

/* is a simple copy of the linux list.h, just for fun,don't too strict*/
struct list_entry {
        struct list_entry *prev, *next;
};

#define LIST_HEAD_INIT(name)     \
        {                        \
                &(name), &(name) \
        }
#define LIST_HEAD(name) struct list_entry name = LIST_HEAD_INIT(name);

static inline void INIT_LIST_HEAD(struct list_entry *list_node)
{
        list_node->prev = list_node;
        list_node->next = list_node;
}
static inline void __list_add(struct list_entry *new_node,
                              struct list_entry *prev, struct list_entry *next)
{
        next->prev = new_node;
        new_node->next = next;
        new_node->prev = prev;
        prev->next = new_node;
}
static inline void list_add_head(struct list_entry *new_node,
                                 struct list_entry *head)
{
        __list_add(new_node, head, head->next);
}
static inline void list_add_tail(struct list_entry *new_node,
                                 struct list_entry *head)
{
        __list_add(new_node, head->prev, head);
}
static inline void __list_del(struct list_entry *prev, struct list_entry *next)
{
        prev->next = next;
        next->prev = prev;
}
/*here our module shoule node indepedent of the whole kernel,so we cannot
 * realize the list_del in linux*/
static inline void list_del_init(struct list_entry *node)
{
        __list_del(node->prev, node->next);
        INIT_LIST_HEAD(node);
}
static inline void list_del(struct list_entry *node)
{
        list_del_init(node);
}
static inline void list_replace(struct list_entry *old_node,
                                struct list_entry *new_node)
{
        new_node->next = old_node->next;
        new_node->next->prev = new_node;
        new_node->prev = old_node->prev;
        new_node->prev->next = new_node;
}
static inline void list_replace_init(struct list_entry *old_node,
                                     struct list_entry *new_node)
{
        list_replace(old_node, new_node);
        INIT_LIST_HEAD(old_node);
}
static inline bool list_empty(struct list_entry *head)
{
        return (head->next == head);
}
#endif
