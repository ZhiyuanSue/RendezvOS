#ifndef _SHAMPOOS_STDDEF_H_
#define _SHAMPOOS_STDDEF_H_

typedef long unsigned int size_t;

#define offsetof(TYPE, MEMBER) ((size_t) & ((TYPE *)0)->MEMBER ) 

#endif