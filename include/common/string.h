#ifndef _RENDEZVOS_STRING_H_
#define _RENDEZVOS_STRING_H_

#include <common/stddef.h>

void *memset(void *str, int c, size_t n);
void *memcpy(void *dst_str, const void *src_str, size_t n);
size_t strlen(const char *str);
int strcmp(const char *str1, const char *str2);
int strcmp_s(const char *str1, const char *str2, int n);

#endif