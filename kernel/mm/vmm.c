#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/smp/percpu.h>
extern u32 BSP_ID;
extern u64 boot_stack;
DEFINE_PER_CPU(u64, boot_stack_bottom);
DEFINE_PER_CPU(struct map_handler, Map_Handler);
DEFINE_PER_CPU(struct nexus_node*, nexus_root);
DEFINE_PER_CPU(VSpace*, current_vspace);
VSpace root_vspace;
error_t virt_mm_init(u32 cpu_id, struct setup_info* arch_setup_info)
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
        kinit(per_cpu(nexus_root, cpu_id), cpu_id);
        return REND_SUCCESS;
}

VSpace* new_vspace(void)
{
        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return NULL;
        VSpace* new_vs = (VSpace*)(cpu_allocator->m_alloc(cpu_allocator,
                                                          sizeof(VSpace)));
        if (new_vs)
                memset((void*)new_vs, 0, sizeof(VSpace));
        return new_vs;
}
error_t del_vspace(VSpace** vs)
{
        if (!(*vs))
                return REND_SUCCESS;
        /* Never free the kernel/root vspace page-table frames. */
        if (*vs == &root_vspace)
                return REND_SUCCESS;

        error_t ret = REND_SUCCESS;
        struct nexus_node* vspace_node = (struct nexus_node*)((*vs)->_vspace_node);
        if (!vspace_node || !vspace_node->handler) {
                ret = -E_IN_PARAM;
                goto free_vspace_only;
        }

        u32 handler_cpu_id = vspace_node->handler->cpu_id;
        paddr root_paddr = (*vs)->vspace_root_addr;
        if (handler_cpu_id >= RENDEZVOS_MAX_CPU_NUMBER) {
                ret = -E_IN_PARAM;
                goto free_vspace_only;
        }

        nexus_delete_vspace(per_cpu(nexus_root, handler_cpu_id), *vs);
        /* Teardown needs to reclaim the vspace's page-table frames too. */
        if (root_paddr) {
                /*
                 * `del_vs_root()` uses the page-table self-mapping window
                 * (`handler->map_vaddr[]`), which is tied to the *current CPU's*
                 * map handler virtual slots.
                 */
                error_t e = del_vs_root(root_paddr, &percpu(Map_Handler));
                /* Still free the VSpace struct; caller may decide whether to print. */
                if (e)
                        ret = e;
        }

        struct allocator* cpu_allocator = percpu(kallocator);
        if (!cpu_allocator)
                return ret;
        cpu_allocator->m_free(cpu_allocator, (void*)(*vs));
        *vs = NULL;
        return ret;
free_vspace_only:
        /*
         * If core invariants are already broken (missing vspace_node/handler),
         * do not continue page-table teardown: it can easily corrupt other
         * CPUs' page-table frames. We only free the VSpace struct to avoid
         * leaving a dangling pointer in the TCB teardown path.
         */
        {
                struct allocator* cpu_allocator = percpu(kallocator);
                if (cpu_allocator) {
                        cpu_allocator->m_free(cpu_allocator, (void*)(*vs));
                }
                *vs = NULL;
        }
        return ret;
}