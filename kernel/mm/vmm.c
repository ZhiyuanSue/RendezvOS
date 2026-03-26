#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
extern cpu_id_t BSP_ID;
extern u64 boot_stack;
DEFINE_PER_CPU(u64, boot_stack_bottom);
DEFINE_PER_CPU(struct map_handler, Map_Handler);
DEFINE_PER_CPU(struct nexus_node*, nexus_root);
DEFINE_PER_CPU(VS_Common*, current_vspace);
DEFINE_PER_CPU(VS_Common, nexus_kernel_heap_vs_common);
VS_Common root_vspace;
error_t virt_mm_init(cpu_id_t cpu_id, struct setup_info* arch_setup_info)
{
        if (cpu_id == BSP_ID) {
                sys_init_map(mem_zones[ZONE_NORMAL].pmm);
                root_vspace.type = (u64)VS_COMMON_USER_VSPACE;
                root_vspace.vspace_root_addr =
                        arch_get_current_kernel_vspace_root();
                /* nexus_root for this CPU is not allocated yet; _vspace_node
                 * is wired in init_vspace_nexus and later cleared. */
                init_vspace(&root_vspace, 0, NULL);
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
        if (!per_cpu(nexus_root, cpu_id))
                return -E_RENDEZVOS;
        if (!kinit(per_cpu(nexus_root, cpu_id), cpu_id))
                return -E_RENDEZVOS;
        /* init_vspace_nexus wires per-CPU nexus_kernel_heap_vs_common; drop
         * back-pointer. */
        return REND_SUCCESS;
}

VS_Common* new_vspace(void)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator)
                return NULL;
        VS_Common* user_vs = (VS_Common*)cpu_kallocator->m_alloc(
                cpu_kallocator, sizeof(VS_Common));
        if (!user_vs)
                return NULL;
        memset((void*)user_vs, 0, sizeof(*user_vs));
        user_vs->type = (u64)VS_COMMON_USER_VSPACE;
        return user_vs;
}
error_t del_vspace(VS_Common** vs)
{
        if (!(*vs))
                return REND_SUCCESS;
        /* Never free the kernel/root vspace page-table frames. */
        if (*vs == &root_vspace)
                return REND_SUCCESS;

        error_t ret = REND_SUCCESS;
        struct nexus_node* vspace_node =
                (struct nexus_node*)((*vs)->_vspace_node);
        if (!vspace_node || !vspace_node->handler) {
                ret = -E_IN_PARAM;
                goto free_vspace_only;
        }

        cpu_id_t handler_cpu_id = vspace_node->handler->cpu_id;
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
                 * (`handler->map_vaddr[]`), which is tied to the *current
                 * CPU's* map handler virtual slots.
                 */
                error_t e = del_vs_root(*vs, &percpu(Map_Handler));
                /* Still free the VS_Common; caller may decide whether to print.
                 */
                if (e)
                        ret = e;
        }

        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator)
                return ret;
        cpu_kallocator->m_free(cpu_kallocator, (void*)(*vs));
        *vs = NULL;
        return ret;
free_vspace_only:
        /*
         * If core invariants are already broken (missing vspace_node/handler),
         * do not continue page-table teardown: it can easily corrupt other
         * CPUs' page-table frames. We only free the VS_Common to avoid
         * leaving a dangling pointer in the TCB teardown path.
         */
        {
                struct allocator* cpu_kallocator = percpu(kallocator);
                if (cpu_kallocator) {
                        cpu_kallocator->m_free(cpu_kallocator, (void*)(*vs));
                }
                *vs = NULL;
        }
        return ret;
}
