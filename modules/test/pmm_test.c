#include <shampoos/mm/pmm.h>
#include <modules/test/test.h>
#include <shampoos/error.h>
#define PPN_TEST_CASE_NUM 10
void pmm_test(void)
{
	pr_info("start pmm test\n");
	u32 alloc_ppn[PPN_TEST_CASE_NUM];

	for(int i=0;i<PPN_TEST_CASE_NUM;++i)
	{
		alloc_ppn[i] = pmm_alloc(i*2+3);
		if(alloc_ppn!=-ENOMEM)
		{
			pr_debug("try to get %x pages ,and alloc ppn 0x%x\n",i*2+3,alloc_ppn[i]);
		}
		else
			pr_error("alloc error\n");
	}
	for(int i=0;i<PPN_TEST_CASE_NUM;++i)
	{
		if(!pmm_free(alloc_ppn[i],i*2+3)){
			pr_debug("free ppn 0x%x success\n",alloc_ppn[i]);
		}
		else
			pr_error("free error\n");
	}
}