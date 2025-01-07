#include <arch/x86_64/sys_ctrl.h>
#include <shampoos/percpu.h>
#include <common/mm.h>
#include <modules/log/log.h>

DEFINE_PER_CPU(struct TSS, cpu_tss);
void prepare_per_cpu_tss(struct nexus_node *nexus_root)
{
        vaddr stack_top =
                (vaddr)get_free_page(
                        2, ZONE_NORMAL, KERNEL_VIRT_OFFSET, 0, nexus_root)
                + 2 * PAGE_SIZE;
        set_ist(&per_cpu(cpu_tss, nexus_root->nexus_id), 1, stack_top);
        pr_info("before ltr 0x%x 0x%x\n",
                (&per_cpu(cpu_tss, nexus_root->nexus_id))->ist1_lower_32_bits,
                (&per_cpu(cpu_tss, nexus_root->nexus_id))->ist1_upper_32_bits);
        ltr(&per_cpu(cpu_tss, nexus_root->nexus_id));
}