#include <shampoos/list.h>
#include <shampoos/mm/pmm.h>

void pmm_init(struct setup_info* arch_setup_info){
	pr_info("start pmm init\n");
	arch_init_pmm(arch_setup_info);
}
u64	pmm_alloc(size_t page_frame_number)
{

}
void	pmm_free(u64 start_addr,size_t page_frame_number)
{
	
}