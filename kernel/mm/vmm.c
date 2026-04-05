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

error_t vspace_free_last_ref(ref_count_t* refcount)
{
        VS_Common* vs = container_of(refcount, VS_Common, refcount);
        VS_Common* tmp = vs;
        return del_vspace(&tmp);
}

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
                ref_init(&root_vspace.refcount);
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
        ref_init(&user_vs->refcount);
        return user_vs;
}

error_t del_vspace(VS_Common** vs)
{
        if (!(*vs))
                return REND_SUCCESS;
        /* Never free the kernel/root vspace page-table frames. */
        if (*vs == &root_vspace)
                return REND_SUCCESS;
        if ((*vs)->type == (u64)VS_COMMON_KERNEL_HEAP_REF)
                return REND_SUCCESS;

        VS_Common* vspace = *vs;
        struct map_handler* map_handler = &percpu(Map_Handler);
        error_t ret = REND_SUCCESS;
        /*
         * Snapshot: nexus_delete_vspace clears _vspace_node. If there was no
         * node at entry, delete_task already reclaimed descendants — only free
         * root below, do not walk user PT again.
         */
        bool had_vspace_node = vspace->_vspace_node != NULL;

        if (had_vspace_node) {
                struct nexus_node* vspace_node =
                        (struct nexus_node*)vspace->_vspace_node;
                bool nexus_ready = vspace_node && vspace_node->handler
                        && vspace_node->handler->cpu_id
                                   < RENDEZVOS_MAX_CPU_NUMBER;

                if (!nexus_ready)
                        ret = -E_IN_PARAM;
                else
                        nexus_delete_vspace(
                                per_cpu(nexus_root,
                                        vspace_node->handler->cpu_id),
                                vspace);
        }

        if (vspace->vspace_root_addr) {
                if (had_vspace_node) {
                        error_t user_err =
                                vspace_free_user_pt(vspace, map_handler);
                        if (user_err != REND_SUCCESS && ret == REND_SUCCESS)
                                ret = user_err;
                }
                error_t root_err =
                        vspace_free_root_page(vspace, map_handler);
                if (root_err != REND_SUCCESS && ret == REND_SUCCESS)
                        ret = root_err;
        }

        struct allocator* cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator) {
                cpu_kallocator->m_free(cpu_kallocator, (void*)vspace);
                *vs = NULL;
        }
        return ret;
}
