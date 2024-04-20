#include <shampoos/types.h>

inline void cpuid(u32 EAX,u32 *EBX,u32 *ECX,u32* EDX)
{
	u32 tmp_ebx,tmp_ecx,tmp_edx;
	asm volatile (
		"movl	,%eax;"
		"cpuid"
		:
		:
	);
	*EBX=tmp_ebx;
	*ECX=tmp_ecx;
	*EDX=tmp_edx;
}