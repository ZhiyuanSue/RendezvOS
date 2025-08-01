#include <arch/x86_64/sys_ctrl.h>
#include <rendezvos/smp/percpu.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <common/mm.h>

DEFINE_PER_CPU(struct TSS, cpu_tss);
void prepare_per_cpu_tss(struct nexus_node* nexus_root)
{
        set_rsp(&per_cpu(cpu_tss, nexus_root->nexus_id), 0, boot_stack_bottom);
        union desc_selector tmp_sel = {
                .rpl = 0,
                .index = GDT_TSS_LOWER_INDEX,
                .table_indicator = 0,
        };
        ltr(&tmp_sel);
}
void prepare_per_cpu_tss_desc(union desc* desc_lower, union desc* desc_upper,
                              int cpu_id)
{
        SET_TSS_LDT_DESC_LOWER(((*desc_lower).tss_ldt_desc_lower),
                               (vaddr)(&per_cpu(cpu_tss, cpu_id)),
                               KERNEL_PL);
        SET_TSS_LDT_DESC_UPPER(((*desc_upper).tss_ldt_desc_upper),
                               (vaddr)(&per_cpu(cpu_tss, cpu_id)));
}