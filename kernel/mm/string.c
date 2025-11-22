#include <common/string.h>
#include <common/types.h>

/*a slow way for basic*/
/*Just a simple Duff device code*/
static inline void basic_memset(char *str, u8 c, size_t count)
{
        int n = (count + 7) / 8;
        switch (count % 8) {
        case 0:
                do {
                        *str++ = c;
                        /* fall through */
                case 7:
                        *str++ = c;
                        /* fall through */
                case 6:
                        *str++ = c;
                        /* fall through */
                case 5:
                        *str++ = c;
                        /* fall through */
                case 4:
                        *str++ = c;
                        /* fall through */
                case 3:
                        *str++ = c;
                        /* fall through */
                case 2:
                        *str++ = c;
                        /* fall through */
                case 1:
                        *str++ = c;
                } while (--n > 0);
        }
}
void *memset(void *str, int c, size_t n)
{
        basic_memset((char *)str, (u8)c, n);
        return (str);
}

static inline void basic_memcpy(char *str1, const char *str2, size_t count)
{
        int n = (count + 7) / 8;
        switch (count % 8) {
        case 0:
                do {
                        *str1++ = *str2++;
                        /* fall through */
                case 7:
                        *str1++ = *str2++;
                        /* fall through */
                case 6:
                        *str1++ = *str2++;
                        /* fall through */
                case 5:
                        *str1++ = *str2++;
                        /* fall through */
                case 4:
                        *str1++ = *str2++;
                        /* fall through */
                case 3:
                        *str1++ = *str2++;
                        /* fall through */
                case 2:
                        *str1++ = *str2++;
                        /* fall through */
                case 1:
                        *str1++ = *str2++;
                } while (--n > 0);
        }
}

void *memcpy(void *dst_str, const void *src_str, size_t n)
{
        basic_memcpy((char *)dst_str, (char *)src_str, n);
        return (dst_str);
}

__attribute__((optimize("O0"))) size_t strlen(const char *str)
{
        const char *p = str;
        size_t len;

        len = 0;
        while (*p++)
                len++;
        return (len);
}
int strcmp(const char *str1, const char *str2)
{
        while (*str1 && (*str1 == *str2)) {
                str1++;
                str2++;
        }
        return (*str1 - *str2);
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