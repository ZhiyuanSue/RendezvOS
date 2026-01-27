#ifndef _RENDEZVOS_STDDEF_H_
#define _RENDEZVOS_STDDEF_H_

typedef long unsigned int size_t;

#undef NULL
#define NULL ((void *)0)

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member)                                \
        ({                                                             \
                const __typeof__(((type *)0)->member) *__mptr = (ptr); \
                (type *)((char *)__mptr - offsetof(type, member));     \
        })
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
