#ifndef _SHAMPOOS_MM_H_
#define _SHAMPOOS_MM_H_

#define PAGE_SIZE 0x1000
#define MIDDLE_PAGE_SIZE 0x200000
#define HUGE_PAGE_SIZE 0x40000000

#define KiloBytes 0x400
#define MegaBytes 0x100000
#define GigaBytes 0x40000000

#define ROUND_UP(x, align) ((x + (align - 1)) & ~(align - 1))
#define ROUND_DOWN(x, align) (x & ~(align - 1))

#define ALIGNED(x, align) ((x & (align - 1)) == 0)

#include <common/stddef.h>

void  *memset(void *str, int c, size_t n);
void  *memcpy(void *str1, const void *str2, size_t n);
size_t strlen(const char *str);
int    strcmp(const char *str1, const char *str2);

#endif