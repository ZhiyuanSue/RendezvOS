#include <shampoos/mm/pmm.h>
#include <modules/test/test.h>
void pmm_test(void)
{
	pr_info("start pmm test\n");
	u64 alloc_ppn;
	for(int i=0;i<10;++i)
	{
		alloc_ppn = pmm_alloc(i*2+3);
		pr_info("alloc 0x%x\n",alloc_ppn);
	}
}