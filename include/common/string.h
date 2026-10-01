#ifndef _RENDEZVOS_STRING_H_
#define _RENDEZVOS_STRING_H_

#include <common/stddef.h>

/**
 * @brief Minimal C string/memory helpers (not full libc).
 */
void *memset(void *str, int c, size_t n);
void *memcpy(void *dst_str, const void *src_str, size_t n);
size_t strlen(const char *str);
int strcmp(const char *str1, const char *str2);
/**
 * @brief Bounded strcmp: advance while @p n>0, *@p str1 nonzero, chars equal;
 * then return (*str1 - *str2). Not strict strncmp (equal first @p n chars still
 * compare the next byte).
 */
int strcmp_s(const char *str1, const char *str2, size_t n);
char *strncpy(char *dst, const char *src, size_t n);

#endif