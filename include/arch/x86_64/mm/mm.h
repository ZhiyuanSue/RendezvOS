#ifndef _SHAMPOOS_ARCH_MM_H_
#define _SHAMPOOS_ARCH_MM_H_

#include "pmm.h"
#include <common/stddef.h>


void *memset(void *str, int c, size_t n);
void *memcpy(void *str1, const void *str2, size_t n);

#endif
