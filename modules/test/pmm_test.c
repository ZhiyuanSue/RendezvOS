#include <shampoos/mm/pmm.h>
#include <modules/test/test.h>
#include <shampoos/error.h>
void pmm_test(void)
{
	pr_info("start pmm test\n");
	u32 alloc_ppn;
	for(int i=0;i<10;++i)
	{
		alloc_ppn = pmm_alloc(i*2+3);
		if(alloc_ppn!=-ENOMEM)
		{
			pr_debug("try to get %x pages ,and alloc ppn 0x%x\n",i*2+3,alloc_ppn);
		}
		else
			pr_error("alloc error\n");
	}
}