#include <shampoos/types.h>
/*cpuid*/
struct cpuid_result
{
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
};

inline struct cpuid_result cpuid(u32 cpuid_op)
{
	struct cpuid_result tmp_result;
	asm volatile (
		"cpuid"
		:"=a"(tmp_result.eax),"=b"(tmp_result.ebx),"=c"(tmp_result.ecx),"=d"(tmp_result.edx)
		:"a"(cpuid_op)
	);
	return tmp_result;
}
