#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/mm/spmalloc.h>
#include <rendezvos/smp/percpu.h>
extern int BSP_ID;
extern u64 boot_stack;
DEFINE_PER_CPU(u64, boot_stack_bottom);
DEFINE_PER_CPU(struct map_handler, Map_Handler);
DEFINE_PER_CPU(struct nexus_node *, nexus_root);
DEFINE_PER_CPU(VSpace *, current_vspace);
VSpace root_vspace;
error_t virt_mm_init(int cpu_id, struct setup_info *arch_setup_info)
{
        if (cpu_id == BSP_ID) {
                sys_init_map();
                root_vspace.vspace_root_addr =
                        arch_get_current_kernel_vspace_root();
                init_vspace(&root_vspace, 0, per_cpu(nexus_root, BSP_ID));
                per_cpu(boot_stack_bottom, cpu_id) =
                        (vaddr)(&boot_stack) + boot_stack_size;
        } else {
                per_cpu(boot_stack_bottom, cpu_id) =
                        arch_setup_info->ap_boot_stack_ptr;
        }
        per_cpu(current_vspace, cpu_id) = &root_vspace;
        init_map(&per_cpu(Map_Handler, cpu_id),
                 cpu_id,
                 mem_zones[ZONE_NORMAL].pmm);
        per_cpu(nexus_root, cpu_id) = init_nexus(&per_cpu(Map_Handler, cpu_id));
        sp_init(per_cpu(nexus_root, cpu_id), cpu_id);
        return 0;
}