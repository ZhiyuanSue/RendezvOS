#include <rendezvos/common.h>
#include <rendezvos/mm/buddy_pmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/spmalloc.h>
#include <rendezvos/smp/percpu.h>
extern struct buddy buddy_pmm;
extern int BSP_ID;
extern u64 boot_stack;
DEFINE_PER_CPU(u64, boot_stack_bottom);
DEFINE_PER_CPU(struct map_handler, Map_Handler);
DEFINE_PER_CPU(struct nexus_node *, nexus_root);
DEFINE_PER_CPU(VSpace *, current_vspace);
VSpace root_vspace;
error_t phy_mm_init(struct setup_info *arch_setup_info)
{
        // memory part init
        buddy_pmm.pmm_init(arch_setup_info);
        return 0;
}
error_t virt_mm_init(int cpu_id, struct setup_info *arch_setup_info)
{
        if (cpu_id == BSP_ID) {
                sys_init_map();
                init_vspace(&root_vspace,
                            arch_get_current_kernel_vspace_root(),
                            0,
                            per_cpu(nexus_root, BSP_ID));
                per_cpu(boot_stack_bottom, cpu_id) =
                        (vaddr)(&boot_stack) + boot_stack_size;
        } else {
                per_cpu(boot_stack_bottom, cpu_id) =
                        arch_setup_info->ap_boot_stack_ptr;
        }
        per_cpu(current_vspace, cpu_id) = &root_vspace;
        init_map(&per_cpu(Map_Handler, cpu_id),
                 cpu_id,
                 ZONE_NORMAL,
                 (struct pmm *)&buddy_pmm);
        per_cpu(nexus_root, cpu_id) = init_nexus(&per_cpu(Map_Handler, cpu_id));
        sp_init(per_cpu(nexus_root, cpu_id),
                per_cpu(Map_Handler, cpu_id).cpu_id);
        return 0;
}