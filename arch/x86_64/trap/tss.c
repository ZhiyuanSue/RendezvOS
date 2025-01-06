#include <arch/x86_64/trap/tss.h>
#include <shampoos/percpu.h>

DEFINE_PER_CPU(struct TSS, cpu_tss);
static vaddr alloc_stack(int page_number)
{
        /*
                alloc some stack for the stack switch
        */
        return 0;
}
void prepare_per_cpu_tss(void)
{
        alloc_stack(1);
}