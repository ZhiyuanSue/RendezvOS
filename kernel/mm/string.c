#include <common/string.h>
#include <common/types.h>

/*
 * I have to tell you, it's a hard thing to write a right code of this file , in
 * aarch64 ,the alignment is a big problem, I will not try to optimize it again,
 * even I known there might have better way, e.x, the rep movsb under x86_64
 * if you want more better speed ,just do it
 * */

typedef u64 word_t;
#define WORD_SIZE sizeof(word_t)
#define WORD_MASK (WORD_SIZE - 1)
static inline int has_zero(word_t x)
{
        const word_t mask = 0x0101010101010101ULL;
        const word_t high = 0x8080808080808080ULL;
        return ((x - mask) & ~x & high) != 0;
}
/*a slow way for basic*/
/*Just a simple Duff device code*/
static inline void basic_memset(u8 *s, u8 c, size_t count)
{
        u8 *p = (u8 *)s;
        u8 uc = (u8)c;
        while (((u64)p & (WORD_SIZE - 1)) && count) {
                *p++ = uc;
                count--;
        }

        word_t word = uc;
        word |= word << 8;
        word |= word << 16;
        word |= word << 32;
        if (count >= WORD_SIZE) {
                word_t *wp = (word_t *)p;
                size_t words = count / WORD_SIZE;
                for (size_t i = 0; i < words; i++) {
                        wp[i] = word;
                }
                p = (u8 *)(wp + words);
                count -= words * WORD_SIZE;
        }

        for (size_t i = 0; i < count; i++) {
                p[i] = uc;
        }
}
void *memset(void *str, int c, size_t n)
{
        basic_memset((u8 *)str, (u8)c, n);
        return (str);
}

static inline void basic_memcpy(u8 *dst, const u8 *src, size_t count)
{
        unsigned char *d = dst;
        const unsigned char *s = src;

        if (((uintptr_t)d & 7) == 0 && ((uintptr_t)s & 7) == 0) {
                uint64_t *wd = (uint64_t *)d;
                const uint64_t *ws = (const uint64_t *)s;
                size_t words = count / 8;
                for (size_t i = 0; i < words; i++) {
                        wd[i] = ws[i];
                }
                d += words * 8;
                s += words * 8;
                count -= words * 8;
        }

        if (count) {
                int n_loops = (count + 7) / 8;
                switch (count % 8) {
                case 0:
                        do {
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 7:
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 6:
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 5:
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 4:
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 3:
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 2:
                                *d++ = *s++;
                                __attribute__((fallthrough));
                        case 1:
                                *d++ = *s++;
                        } while (--n_loops > 0);
                }
        }
}

void *memcpy(void *dst_str, const void *src_str, size_t n)
{
        basic_memcpy((u8 *)dst_str, (u8 *)src_str, n);
        return (dst_str);
}

size_t strlen(const char *str)
{
        const char *s = str;
        u64 addr = (u64)s;

        while (addr & 7) {
                if (*s == '\0')
                        return s - str;
                s++;
                addr++;
        }

        const word_t *word_ptr = (const word_t *)s;
        while (1) {
                word_t word = *word_ptr++;
                if (has_zero(word)) {
                        const char *byte_ptr = (const char *)(word_ptr - 1);
                        for (int i = 0; i < 8; i++) {
                                if (byte_ptr[i] == '\0')
                                        return byte_ptr + i - str;
                        }
                }
        }
}
int strcmp(const char *str1, const char *str2)
{
        const unsigned char *p1 = (const unsigned char *)str1;
        const unsigned char *p2 = (const unsigned char *)str2;
        uintptr_t addr1 = (uintptr_t)p1;
        uintptr_t addr2 = (uintptr_t)p2;

        while ((addr1 & WORD_MASK) || (addr2 & WORD_MASK)) {
                if (*p1 != *p2)
                        return *p1 - *p2;
                if (*p1 == '\0')
                        return 0;
                p1++;
                p2++;
                addr1++;
                addr2++;
        }

        const word_t *wp1 = (const word_t *)p1;
        const word_t *wp2 = (const word_t *)p2;
        while (1) {
                word_t w1 = *wp1++;
                word_t w2 = *wp2++;
                if (w1 != w2 || has_zero(w1)) {
                        const unsigned char *bp1 =
                                (const unsigned char *)(wp1 - 1);
                        const unsigned char *bp2 =
                                (const unsigned char *)(wp2 - 1);
                        for (size_t i = 0; i < WORD_SIZE; i++) {
                                if (bp1[i] != bp2[i])
                                        return bp1[i] - bp2[i];
                                if (bp1[i] == '\0')
                                        return 0;
                        }
                }
        }
}
int strcmp_s(const char *str1, const char *str2, size_t n)
{
        while (n && *str1 && (*str1 == *str2)) {
                str1++;
                str2++;
                n--;
        }
        return (*str1 - *str2);
}
char *strncpy(char *dst, const char *src, size_t n)
{
        unsigned char *d = (unsigned char *)dst;
        const unsigned char *s = (const unsigned char *)src;
        unsigned char *const d_start = d;
        size_t remaining = n;

        if (n == 0)
                return dst;

        while (((uintptr_t)s & WORD_MASK) || ((uintptr_t)d & WORD_MASK)) {
                if (remaining == 0)
                        goto fill_remaining;
                if ((*d = *s) == '\0')
                        goto zero_found;
                d++;
                s++;
                remaining--;
        }

        while (remaining >= WORD_SIZE) {
                word_t w = *(const word_t *)s;
                if (has_zero(w)) {
                        const unsigned char *p = (const unsigned char *)&w;
                        for (size_t i = 0; i < WORD_SIZE; i++) {
                                if (remaining == 0)
                                        goto fill_remaining;
                                if ((*d++ = p[i]) == '\0')
                                        goto zero_found;
                                remaining--;
                        }
                } else {
                        *(word_t *)d = w;
                        d += WORD_SIZE;
                        s += WORD_SIZE;
                        remaining -= WORD_SIZE;
                }
        }

        while (remaining) {
                if ((*d = *s) == '\0')
                        goto zero_found;
                d++;
                s++;
                remaining--;
        }

        return dst;

zero_found:
        d++;
fill_remaining:
        while ((size_t)(d - d_start) < n)
                *d++ = '\0';
        return dst;
}