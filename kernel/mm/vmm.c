#include <common/string.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/map_handler.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/mm/asid.h>
#include <rendezvos/mm/vmm_radix_tree.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/error.h>
#include <modules/log/log.h>
extern cpu_id_t BSP_ID;
extern u64 boot_stack;
DEFINE_PER_CPU(u64, boot_stack_bottom);
DEFINE_PER_CPU(struct map_handler, Map_Handler);
DEFINE_PER_CPU(VSpace*, current_vspace);
VSpace root_vspace;

static void vspace_rb_tree_insert(VSpace* vs, VSpace* root_vs)
{
        struct rb_node **new = &root_vs->_vspace_rb_root.rb_root,
                       *parent = NULL;
        u64 key = vs->vspace_root_addr;
        while (*new) {
                parent = *new;
                VSpace* tmp_node =
                        container_of(parent, VSpace, _vspace_rb_node);
                if (key < (u64)tmp_node->vspace_root_addr)
                        new = &parent->left_child;
                else if (key > (u64)tmp_node->vspace_root_addr)
                        new = &parent->right_child;
                else {
                        return;
                }
        }
        RB_Link_Node(&vs->_vspace_rb_node, parent, new);
        RB_SolveDoubleRed(&vs->_vspace_rb_node, &root_vs->_vspace_rb_root);
}
static void vspace_rb_tree_remove(VSpace* vs, VSpace* root_vs)
{
        RB_Remove(&vs->_vspace_rb_node, &root_vs->_vspace_rb_root);
        vs->_vspace_rb_node.black_height = vs->_vspace_rb_node.rb_parent_color =
                0;
        vs->_vspace_rb_node.left_child = vs->_vspace_rb_node.right_child = NULL;
}
static __attribute__((unused)) VSpace* vspace_rb_tree_prev(VSpace* vs)
{
        struct rb_node* curr_rb = &vs->_vspace_rb_node;
        struct rb_node* prev_rb = RB_Prev(curr_rb);
        if (!prev_rb)
                return NULL;
        return container_of(prev_rb, VSpace, _vspace_rb_node);
}
static __attribute__((unused)) VSpace* vspace_rb_tree_next(VSpace* vs)
{
        struct rb_node* curr_rb = &vs->_vspace_rb_node;
        struct rb_node* next_rb = RB_Next(curr_rb);
        if (!next_rb)
                return NULL;
        return container_of(next_rb, VSpace, _vspace_rb_node);
}

error_t free_vspace_ref(ref_count_t* refcount)
{
        VSpace* vs = container_of(refcount, VSpace, refcount);
        VSpace* tmp = vs;
        return del_vspace(&tmp);
}

/*
 * Kernel object for a user table VSpace (kallocator). Caller supplies
 * root_vs for RB registry parent and PMM policy.
 */
static VSpace* alloc_user_vs_structure(VSpace* root_vs)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !root_vs)
                return NULL;
        VSpace* user_vs = (VSpace*)cpu_kallocator->m_alloc(cpu_kallocator,
                                                           sizeof(VSpace));
        if (!user_vs)
                return NULL;
        memset((void*)user_vs, 0, sizeof(*user_vs));
        user_vs->pmm = root_vs->pmm;
        user_vs->asid = asid_alloc();
        if (user_vs->asid == 0) {
                cpu_kallocator->m_free(cpu_kallocator, (void*)user_vs);
                return NULL;
        }
        vs_tlb_cpu_mask_zero(user_vs);
        lock_init_cas(&user_vs->tlb_cpu_mask_lock);
        ref_init(&user_vs->refcount);
        user_vs->root_vs = root_vs;
        return user_vs;
}

static void free_user_vs_structure(VSpace* vs)
{
        struct allocator* cpu_kallocator;
        if (!vs)
                return;
        cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator)
                cpu_kallocator->m_free(cpu_kallocator, (void*)vs);
}

error_t init_root_vspace(VSpace* root_vs, cpu_id_t cpu_id)
{
        struct map_handler* h = &per_cpu(Map_Handler, cpu_id);

        root_vs->pmm = mem_zones[ZONE_NORMAL].pmm;

        root_vs->vspace_id = 0;
        root_vs->vspace_lock = NULL;
        root_vs->asid = 0;
        root_vs->vspace_root_addr = arch_get_current_kernel_vspace_root();
        root_vs->root_vs=root_vs;

        if (!vmm_radix_tree_init(h, root_vs)) {
                pr_error("[VMM] init_root_vspace: vmm_radix_tree_init failed\n");
                return -E_RENDEZVOS;
        }
        if (vmm_radix_tree_bootstrap_shared_kernel_high_half(h, root_vs)
            != REND_SUCCESS) {
                pr_error(
                        "[VMM] init_root_vspace: bootstrap_shared_kernel_high_half failed\n");
                (void)vmm_radix_tree_destroy(h, root_vs);
                return -E_RENDEZVOS;
        }
        if (vmm_radix_tree_install_shared_kernel_high_half(h, root_vs)
            != REND_SUCCESS) {
                pr_error(
                        "[VMM] init_root_vspace: install_shared_kernel_high_half failed\n");
                (void)vmm_radix_tree_destroy(h, root_vs);
                return -E_RENDEZVOS;
        }

        INIT_LIST_HEAD(&root_vs->root_manage_list_head);
        lock_init_cas(&root_vs->vspace_register_lock);
        root_vspace._vspace_rb_root.rb_root = NULL;

        vs_tlb_cpu_mask_zero(root_vs);
        lock_init_cas(&root_vs->tlb_cpu_mask_lock);
        ref_init(&root_vs->refcount);
        return REND_SUCCESS;
}

VSpace* create_user_vspace(VSpace* root_vs)
{
        struct map_handler* handler;
        VSpace* vs;
        paddr new_root_paddr;

        if (!root_vs || !root_vs->pmm)
                return NULL;

        vs = alloc_user_vs_structure(root_vs);
        if (!vs)
                return NULL;

        handler = &percpu(Map_Handler);
        new_root_paddr = new_vs_root(0, handler);
        if (!new_root_paddr)
                goto fail_asid_and_vs;

        set_vspace_root_addr(vs, new_root_paddr);

        if (!vmm_radix_tree_init(handler, vs))
                goto fail_user_root_phys;

        if (vmm_radix_tree_install_shared_kernel_high_half(handler, vs)
            != REND_SUCCESS)
                goto fail_radix_installed;

        lock_cas(&root_vs->vspace_register_lock);
        vspace_rb_tree_insert(vs, root_vs);
        unlock_cas(&root_vs->vspace_register_lock);

        return vs;

fail_radix_installed:
        (void)vmm_radix_tree_destroy(handler, vs);
fail_user_root_phys:
        (void)vspace_free_root_page(vs, handler);
fail_asid_and_vs:
        asid_free(vs->asid);
        free_user_vs_structure(vs);
        return NULL;
}

error_t del_vspace(VSpace** vs)
{
        if (!(*vs))
                return REND_SUCCESS;
        /* Never free the kernel/root vspace page-table frames. */
        if (*vs == (*vs)->root_vs)
                return REND_SUCCESS;

        VSpace* vspace = *vs;

        if (!vspace->root_vs)
                return -E_IN_PARAM;

        struct map_handler* map_handler = &percpu(Map_Handler);
        error_t ret = REND_SUCCESS;

        lock_cas(&vspace->tlb_cpu_mask_lock);
        bool empty = vs_tlb_cpu_mask_is_zero(vspace);
        unlock_cas(&vspace->tlb_cpu_mask_lock);
        if (!empty)
                return -E_REND_RC_UNEQUAL;

        bool had_radix = vspace->root_radix != NULL;

        if (had_radix) {
                error_t dr = vmm_radix_tree_destroy(map_handler, vspace);
                if (dr != REND_SUCCESS) {
                        pr_error(
                                "[VMM] del_vspace: vmm_radix_tree_destroy failed e=%d\n",
                                (int)dr);
                        return dr;
                }
        }

        if (vspace->vspace_root_addr) {
                if (had_radix) {
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

        asid_free(vspace->asid);

        lock_cas(&vspace->root_vs->vspace_register_lock);
        vspace_rb_tree_remove(vspace, vspace->root_vs);
        unlock_cas(&vspace->root_vs->vspace_register_lock);

        {
                struct allocator* cpu_kallocator = percpu(kallocator);
                if (cpu_kallocator)
                        cpu_kallocator->m_free(cpu_kallocator, (void*)vspace);
                else if (ret == REND_SUCCESS)
                        ret = -E_RENDEZVOS;
        }
        *vs = NULL;
        return ret;
}

error_t virt_mm_init(cpu_id_t cpu_id, struct setup_info* arch_setup_info)
{
        if (cpu_id == BSP_ID) {
                sys_init_map(mem_zones[ZONE_NORMAL].pmm);
                init_map(&per_cpu(Map_Handler, cpu_id),
                         cpu_id,
                         mem_zones[ZONE_NORMAL].pmm);
                asid_init();

                memset(&root_vspace, 0, sizeof(struct VSpace));

                if (init_root_vspace(&root_vspace, cpu_id) != REND_SUCCESS)
                        return -E_RENDEZVOS;

                per_cpu(boot_stack_bottom, cpu_id) =
                        (vaddr)(&boot_stack) + boot_stack_size;
        } else {
                init_map(&per_cpu(Map_Handler, cpu_id),
                         cpu_id,
                         mem_zones[ZONE_NORMAL].pmm);
                per_cpu(boot_stack_bottom, cpu_id) =
                        arch_setup_info->ap_boot_stack_ptr;
        }
        per_cpu(current_vspace, cpu_id) = &root_vspace;
        if (!kinit((int)cpu_id))
                return -E_RENDEZVOS;
        /* init_vspace_nexus wires per-CPU nexus_kernel_heap_vs_common; drop
         * back-pointer. */
        return REND_SUCCESS;
}
