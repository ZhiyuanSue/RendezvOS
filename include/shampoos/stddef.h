#ifndef _SHAMPOOS_STDDEF_H_
#define _SHAMPOOS_STDDEF_H_

typedef long unsigned int size_t;

#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER )

#define container_of(ptr,type,member) \
	({const typeof(((type *)0) -> member) * __mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type,member));	})

#endif
