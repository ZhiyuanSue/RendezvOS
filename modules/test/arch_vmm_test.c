#include <modules/test/test.h>

#ifdef _AARCH64_
# include <arch/aarch64/mm/vmm.h>
#elif defined _LOONGARCH_
# include <arch/loongarch/mm/vmm.h>
#elif defined _RISCV64_
# include <arch/riscv64/mm/vmm.h>
#elif defined _X86_64_
# include <arch/x86_64/mm/vmm.h>
#else /*for default config is x86_64*/
# include <arch/x86_64/mm/vmm.h>
#endif

union L0_entry		l0;
union L1_entry_huge	l1_h;
union L1_entry		l1;
union L2_entry_huge	l2_h;
union L2_entry		l2;
union L3_entry		l3;

void	arch_vmm_test(void)
{
	if (sizeof(l0) != sizeof(u64) || sizeof(l1_h) != sizeof(u64)
		|| sizeof(l1) != sizeof(u64) || sizeof(l2_h) != sizeof(u64)
		|| sizeof(l2) != sizeof(u64) || sizeof(l3) != sizeof(u64))
	{
		pr_error("vmm entry align error\n");
		goto arch_vmm_test_error;
	}
	pr_info("arch vmm test pass!\n");
	return ;
arch_vmm_test_error:
	pr_error("arch vmm test failed\n");
}