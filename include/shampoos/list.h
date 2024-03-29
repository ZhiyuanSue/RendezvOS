#ifndef _SHAMPOOS_LIST_H_
#define _SHAMPOOS_LIST_H_
#include <shampoos/types.h>

/* is a simple copy of the linux list.h, just for fun,don't too strict*/
struct list_entry{
	struct list_entry *prev, *next;
};

#define LIST_HEAD_INIT(name) { &(name),&(name) }
#define LIST_HEAD(name)	\
	struct list_entry name = LIST_HEAD_INIT(name);

static inline void INIT_LIST_HEAD(struct list_entry *list_node)
{
	list_node->prev=list_node;
	list_node->next=list_node;
}
static inline void __list_add(struct list_entry *new_node,
		struct list_entry *prev,
		struct list_entry *next)
{
	next->prev=new_node;
	new_node->next=next;
	new_node->prev=prev;
	prev->next=new_node;
}
static inline void list_add(struct list_entry *new_node,struct list_entry *head)
{
	__list_add(new_node,head,head->next);
}
static inline void list_add_tail(struct list_entry *new_node,struct list_entry *head)
{
	__list_add(new_node,head->prev,head);
}
#endif
