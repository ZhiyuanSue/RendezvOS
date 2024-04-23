#include <shampoos/list.h>
#include <shampoos/mm/pmm.h>
#include <modules/log/log.h>


void pmm_init(struct setup_info* arch_setup_info){
	pr_info("start pmm init\n");
	arch_init_pmm(arch_setup_info);
}
u64	pmm_alloc(size_t page_number)
{

}
u64 pmm_free_one(u64 page_address)
{

}
u64 pmm_free(u64 page_address,size_t page_number)
{

}
struct	pmm	buddy_pmm = {
	.pmm_init=pmm_init,
	.pmm_alloc=pmm_alloc,
	.pmm_free_one=pmm_free_one,
	.pmm_free=pmm_free
};
