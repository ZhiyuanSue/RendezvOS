#include <arch/x86_64/mm/mm.h>
#include <modules/log/log.h>

/*a slow way for basic*/
static inline void basic_memset(char *str, u8 c, size_t n)
{
	for(size_t i=0;i<n;i++){
		str[i]=c;
	}
}
void* memset(void *str, int c, size_t n)
{
	basic_memset((char*)str,(u8)c,n);
	return str;
}

static inline void basic_memcpy(char *str1, const char *str2, size_t n)
{
	for(size_t i=0;i<n;i++){
		str1[i]=str2[i];
	}
}

void *memcpy(void *str1, const void *str2, size_t n)
{
	basic_memcpy((char*)str1,(char*)str2,n);
	return str1;
}

