#include <arch/x86_64/sys_ctrl.h>
#include <shampoos/percpu.h>
#include <common/mm.h>
#include <modules/log/log.h>
#include <arch/x86_64/sys_ctrl_def.h>

DEFINE_PER_CPU(struct TSS, cpu_tss);
void prepare_per_cpu_tss(struct nexus_node* nexus_root,
                         union desc_selector* sel)
{
        vaddr stack_top =
                (vaddr)get_free_page(
                        2, ZONE_NORMAL, KERNEL_VIRT_OFFSET, 0, nexus_root)
                + 2 * PAGE_SIZE;
        set_ist(&per_cpu(cpu_tss, nexus_root->nexus_id), 1, stack_top);
        ltr(sel);
}
void prepare_per_cpu_tss_desc(union desc* desc_lower,union desc* desc_upper, int cpu_id)
{
        SET_TSS_LDT_DESC_LOWER(((*desc_lower).tss_ldt_desc_lower),
                         (vaddr)(&per_cpu(cpu_tss, cpu_id)),
                         KERNEL_PL);
        SET_TSS_LDT_DESC_UPPER(((*desc_upper).tss_ldt_desc_upper),
                         (vaddr)(&per_cpu(cpu_tss, cpu_id)));
}