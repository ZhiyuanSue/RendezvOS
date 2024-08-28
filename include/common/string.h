#ifndef _SHAMPOOS_STRING_H_
#define _SHAMPOOS_STRING_H_

#include <common/stddef.h>

void *memset(void *str, int c, size_t n);
void *memcpy(void *str1, const void *str2, size_t n);
size_t strlen(const char *str);
int strcmp(const char *str1, const char *str2);

#endif