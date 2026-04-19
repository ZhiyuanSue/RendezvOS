#ifndef _RENDEZVOS_LIST_H_
#define _RENDEZVOS_LIST_H_
#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/types.h>

/* is a simple copy of the linux list.h, just for fun,don't too strict*/
struct list_entry {
        struct list_entry *prev, *next;
};

#define LIST_HEAD_INIT(name) {&(name), &(name)}
#define LIST_HEAD(name)      struct list_entry name = LIST_HEAD_INIT(name);

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
/** True if node is not linked into any list (INIT_LIST_HEAD / list_del_init).
 */
static inline bool list_node_is_detached(struct list_entry *n)
{
        return n && n->next == n && n->prev == n;
}

/**
 * list_for_each - iterate over a list
 * @pos: the &struct list_entry to use as a loop cursor.
 * @head: the head for your list.
 */
#define list_for_each(pos, head) \
        for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * list_for_each_safe - iterate over a list safe against removal of list entry
 * @pos: the &struct list_entry to use as a loop cursor.
 * @n: another &struct list_entry to use as temporary storage
 * @head: the head for your list.
 */
#define list_for_each_safe(pos, n, head) \
        for (pos = (head)->next, n = pos->next; pos != (head); \
                pos = n, n = pos->next)

/**
 * list_for_each_entry - iterate over list of given type
 * @pos: the type * to use as a loop cursor.
 * @head: the head for your list.
 * @member: the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member) \
        for (pos = list_entry((head)->next, typeof(*pos), member); \
             &pos->member != (head); \
             pos = list_entry(pos->member.next, typeof(*pos), member))

/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal
 * @pos: the type * to use as a loop cursor.
 * @n: another type * to use as temporary storage
 * @head: the head for your list.
 * @member: the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member) \
        for (pos = list_entry((head)->next, typeof(*pos), member), \
             n = list_entry(pos->member.next, typeof(*pos), member); \
             &pos->member != (head); \
             pos = n, n = list_entry(n->member.next, typeof(*n), member))

/**
 * list_entry - get the struct for this entry
 * @ptr: the &struct list_entry pointer.
 * @type: the type of the struct this is embedded in.
 * @member: the name of the list_head within the struct.
 */
#define list_entry(ptr, type, member) \
        container_of(ptr, type, member)

#endif
