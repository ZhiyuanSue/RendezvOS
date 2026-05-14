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

static error_t vspace_rb_tree_insert(VSpace* vs, VSpace* root_vs)
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
                        return -E_IN_PARAM;
                }
        }
        RB_Link_Node(&vs->_vspace_rb_node, parent, new);
        RB_SolveDoubleRed(&vs->_vspace_rb_node, &root_vs->_vspace_rb_root);
        return REND_SUCCESS;
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
        unregister_vspace(vs);
        return del_vspace(&vs);
}

/*
 * Kernel object for a user table VSpace (kallocator). Caller supplies
 * root_vs for RB registry parent and PMM policy.
 */
static VSpace* alloc_user_vs_structure(struct pmm* pmm)
{
        struct allocator* cpu_kallocator = percpu(kallocator);
        if (!cpu_kallocator || !pmm)
                return NULL;
        VSpace* user_vs = (VSpace*)cpu_kallocator->m_alloc(cpu_kallocator,
                                                           sizeof(VSpace));
        if (!user_vs)
                return NULL;
        memset((void*)user_vs, 0, sizeof(*user_vs));
        user_vs->asid = asid_alloc();
        user_vs->registered = false;
        user_vs->pmm = pmm;
        if (user_vs->asid == 0) {
                cpu_kallocator->m_free(cpu_kallocator, (void*)user_vs);
                return NULL;
        }
        vs_tlb_cpu_mask_zero(user_vs);
        lock_init_cas(&user_vs->tlb_cpu_mask_lock);
        ref_init(&user_vs->refcount);
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
        root_vs->root_vs = root_vs;

        if (!vmm_radix_tree_init(h, root_vs)) {
                pr_error(
                        "[VMM] init_root_vspace: vmm_radix_tree_init failed\n");
                return -E_RENDEZVOS;
        }
        if (vmm_radix_tree_bootstrap_shared_kernel_high_half(h, root_vs)
            != REND_SUCCESS) {
                pr_error(
                        "[VMM] init_root_vspace: bootstrap_shared_kernel_high_half failed\n");
                (void)vmm_radix_tree_destroy(h, root_vs);
                return -E_RENDEZVOS;
        }
        if (vmm_radix_tree_install_shared_kernel_high_half(root_vs)
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

VSpace* create_vspace(struct pmm* pmm)
{
        struct map_handler* handler = &percpu(Map_Handler);
        VSpace* vs;
        paddr new_root_paddr;

        if (!pmm)
                return NULL;

        vs = alloc_user_vs_structure(pmm);
        if (!vs)
                return NULL;

        new_root_paddr = new_vs_root(0, handler);
        if (!new_root_paddr)
                goto vs_structure_fail;

        vs->vspace_root_addr = new_root_paddr;

        if (!vmm_radix_tree_init(handler, vs))
                goto radix_root_fail;

        if (vmm_radix_tree_install_shared_kernel_high_half(vs) != REND_SUCCESS)
                goto radix_install_fail;

        return vs;

radix_install_fail:
        (void)vmm_radix_tree_destroy(handler, vs);
radix_root_fail:
        (void)vspace_free_root_page(vs, handler);
vs_structure_fail:
        asid_free(vs->asid);
        free_user_vs_structure(vs);
        return NULL;
}
static void clone_rollback_src_write_pte_pages(VSpace* src_vs,
                                               vaddr va_start,
                                               vaddr va_end,
                                               struct map_handler* handler)
{
        for (vaddr va = va_start; va < va_end; va += PAGE_SIZE) {
                ENTRY_FLAGS_t src_map_flag;
                ppn_t src_ppn = have_mapped(src_vs,
                                            VPN(va),
                                            &src_map_flag,
                                            NULL,
                                            handler);
                if (invalid_ppn(src_ppn)) {
                        pr_error(
                                "[Error] vspace clone rollback src have_mapped\n");
                        continue;
                }
                if (map(src_vs,
                        src_ppn,
                        VPN(va),
                        3,
                        set_mask_u64(src_map_flag, PAGE_ENTRY_WRITE),
                        handler)
                    != REND_SUCCESS) {
                        pr_error(
                                "[Error] vspace clone rollback src map write\n");
                }
        }
}

error_t clone_vspace(VSpace* src_vs, VSpace** dst_vs_out,
                     enum vspace_clone_flags flags)
{
        /* Do not change this comment!!!
         * clone vspace will first try to lock all the l0 entry, which is ok,
         * but lock all the l0 only means no more core will go into this radix
         * tree it cannot promis there have some exist core is hold the lock of
         * l2 entry for lock acquire, it first hold the l0 lock ,and change the
         * structure and count of tree, then it hold the l2 lock, then release
         * l0 lock, the lock of l2 will be hold until using release so there has
         * a window, that the l0 lock is released but the l2 lock is hold but
         * only lock all the l0 cannot promise there have no l2 is locked. That
         * is the problem we must do it in the period of find the first usable
         * range it will try to hold the l2 lock, and clone will wait until
         * other have release l2 lock. will anyone behind clone to try to get
         * the l2 lock? No! l0 lock have promise it. that is the key of this
         * part lock model. Please do not change this comment
         */
        if (!src_vs || !dst_vs_out)
                return -E_IN_PARAM;
        if (!(flags & VSPACE_CLONE_F_USER_4K_ONLY))
                return -E_IN_PARAM;
        if (!!(flags & VSPACE_CLONE_F_COW_PREP)
            == !!(flags & VSPACE_CLONE_F_COPY_PAGES)) {
                /* must select exactly one strategy */
                return -E_IN_PARAM;
        }
        error_t e = REND_SUCCESS;
        struct map_handler* handler = &percpu(Map_Handler);
        VSpace* dst_vs = create_vspace(root_vspace.pmm);
        if (!dst_vs) {
                pr_error("[Error] clone vspace cannot create vspace\n");
                return -E_RENDEZVOS;
        }

        if (vmm_radix_tree_lock_range_big(src_vs, PAGE_SIZE, USER_SPACE_TOP + 1)
            != REND_SUCCESS) {
                goto free_vspace_no_unlock;
        }

        vaddr iter_start = PAGE_SIZE; /*we don't allow the 0 page*/
        vaddr iter_end = USER_SPACE_TOP + 1;

        vaddr searched_start;
        vaddr searched_end;
        ENTRY_FLAGS_t range_flags;
        vaddr page_iter = USER_SPACE_TOP + 1;

        while (vmm_radix_tree_find_first_occupied_interval(src_vs,
                                                           iter_start,
                                                           iter_end,
                                                           &searched_start,
                                                           &searched_end,
                                                           &range_flags)) {
                /*try to insert the range first, we have to handle the flags,
                 * the valid flag must set to the lazy for later bind*/

                ENTRY_FLAGS_t insert_flags =
                        clear_mask_u64(range_flags, PAGE_ENTRY_VALID);
                bool need_bind = range_flags & PAGE_ENTRY_VALID;

                if (vmm_radix_tree_lock_range_small(handler,
                                                    dst_vs,
                                                    searched_start,
                                                    searched_end,
                                                    RADIX_RL_INSERT)
                    != REND_SUCCESS) {
                        pr_error(
                                "[Error] clone vspace lock range small fail\n");
                        e = -E_RENDEZVOS;
                        goto rollback_prev_interval;
                }
                tagged_ptr_t dst_owner =
                        tp_new((void*)dst_vs, (u16)percpu(cpu_number));
                if (vmm_radix_tree_insert_range(dst_vs,
                                                dst_owner,
                                                searched_start,
                                                insert_flags,
                                                searched_end)
                    != REND_SUCCESS) {
                        pr_error("[Error] clone vspace insert_range fail\n");
                        (void)vmm_radix_tree_unlock_range_small(
                                dst_vs, searched_start, searched_end);
                        e = -E_RENDEZVOS;
                        goto rollback_prev_interval;
                }
                /*if no need to bind the page, it only need to insert the
                 * range*/
                if (need_bind) {
                        /*if need , bind the dst pages*/
                        for (page_iter = searched_start;
                             page_iter < searched_end;
                             page_iter += PAGE_SIZE) {
                                /*need to find the src's page*/
                                ENTRY_FLAGS_t src_map_flag;
                                ppn_t src_ppn = have_mapped(src_vs,
                                                            VPN(page_iter),
                                                            &src_map_flag,
                                                            NULL,
                                                            handler);
                                if (invalid_ppn(src_ppn)) {
                                        e = -E_RENDEZVOS;
                                        (void)vmm_radix_tree_unlock_range_small(
                                                dst_vs,
                                                searched_start,
                                                searched_end);
                                        goto rollback_prev_interval;
                                }
                                if (flags & VSPACE_CLONE_F_COPY_PAGES) {
                                        size_t alloced_page_number;
                                        ppn_t dst_ppn = dst_vs->pmm->pmm_alloc(
                                                dst_vs->pmm,
                                                1,
                                                &alloced_page_number);
                                        if (invalid_ppn(dst_ppn)
                                            || alloced_page_number != 1) {
                                                e = -E_RENDEZVOS;
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto rollback_prev_interval;
                                        }
                                        /*copy the data*/
                                        if (map_handler_copy_page(
                                                    handler, dst_ppn, src_ppn)
                                            != REND_SUCCESS) {
                                                e = -E_RENDEZVOS;
                                                dst_vs->pmm->pmm_free(
                                                        dst_vs->pmm,
                                                        dst_ppn,
                                                        1);
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto free_vspace;
                                        }
                                        /*bind*/
                                        if (vmm_radix_tree_leaf_bind(dst_vs,
                                                                     page_iter,
                                                                     dst_ppn,
                                                                     range_flags)
                                            != REND_SUCCESS) {
                                                e = -E_RENDEZVOS;
                                                dst_vs->pmm->pmm_free(
                                                        dst_vs->pmm,
                                                        dst_ppn,
                                                        1);
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto free_vspace;
                                        }
                                        /*map*/
                                        if (map(dst_vs,
                                                dst_ppn,
                                                VPN(page_iter),
                                                3,
                                                range_flags,
                                                handler)
                                            != REND_SUCCESS) {
                                                e = -E_RENDEZVOS;
                                                dst_vs->pmm->pmm_free(
                                                        dst_vs->pmm,
                                                        dst_ppn,
                                                        1);
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto free_vspace;
                                        }
                                } else {
                                        /* link the src page*/
                                        if (vmm_radix_tree_leaf_bind(
                                                    dst_vs,
                                                    page_iter,
                                                    src_ppn,
                                                    src_map_flag)
                                            != REND_SUCCESS) {
                                                e = -E_RENDEZVOS;
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto rollback_prev_interval;
                                        }
                                        /*map the dst*/
                                        pmm_change_pages_ref(
                                                dst_vs->pmm, src_ppn, 1, true);
                                        if (map(dst_vs,
                                                src_ppn,
                                                VPN(page_iter),
                                                3,
                                                range_flags,
                                                handler)
                                            != REND_SUCCESS) {
                                                e = -E_RENDEZVOS;
                                                pmm_change_pages_ref(
                                                        dst_vs->pmm,
                                                        src_ppn,
                                                        1,
                                                        false);
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto rollback_prev_interval;
                                        }
                                        /*change the src pte flags*/
                                        if (map(src_vs,
                                                src_ppn,
                                                VPN(page_iter),
                                                3,
                                                clear_mask_u64(src_map_flag,
                                                               PAGE_ENTRY_WRITE),
                                                handler)
                                            != REND_SUCCESS) {
                                                e = -E_RENDEZVOS;
                                                (void)vmm_radix_tree_unlock_range_small(
                                                        dst_vs,
                                                        searched_start,
                                                        searched_end);
                                                goto rollback_pte_flags_prev;
                                        }
                                }
                        }
                        if (flags & VSPACE_CLONE_F_COW_PREP) {
                                /*change the src vs range flags*/
                                if (vmm_radix_tree_change_range_flag(
                                            dst_vs,
                                            searched_start,
                                            searched_end,
                                            range_flags | PAGE_ENTRY_COW)
                                    != REND_SUCCESS) {
                                        e = -E_RENDEZVOS;
                                        (void)vmm_radix_tree_unlock_range_small(
                                                dst_vs,
                                                searched_start,
                                                searched_end);
                                        goto rollback_pte_flags_prev;
                                }
                                if (vmm_radix_tree_change_range_flag(
                                            src_vs,
                                            searched_start,
                                            searched_end,
                                            range_flags | PAGE_ENTRY_COW)
                                    != REND_SUCCESS) {
                                        e = -E_RENDEZVOS;
                                        (void)vmm_radix_tree_unlock_range_small(
                                                dst_vs,
                                                searched_start,
                                                searched_end);
                                        goto rollback_pte_flags_prev;
                                }
                        }
                }

                if (vmm_radix_tree_unlock_range_small(
                            dst_vs, searched_start, searched_end)
                    != REND_SUCCESS) {
                        pr_error(
                                "[Error] clone vspace unlock range small fail\n");
                        e = -E_RENDEZVOS;
                        goto rollback_prev_interval;
                }

                iter_start = searched_end;
        }

        if (vmm_radix_tree_unlock_range_big(
                    src_vs, PAGE_SIZE, USER_SPACE_TOP + 1)
            != REND_SUCCESS) {
                pr_error("[Erorr] unlock the range big lock fail\n");
        }
        *dst_vs_out = dst_vs;
        return e;
rollback_pte_flags_prev:
        /* Same half-open range as the descending loop: [searched_start, page_iter). */
        clone_rollback_src_write_pte_pages(
                src_vs, searched_start, page_iter, handler);
        goto rollback_prev_interval;
rollback_prev_interval:
        if (flags & VSPACE_CLONE_F_COW_PREP) {
                /*roll back the changed src vs*/
                vaddr rollback_searched_start;
                vaddr rollback_searched_end;
                ENTRY_FLAGS_t rollback_range_flags;
                vaddr rollback_iter = PAGE_SIZE;
                while (vmm_radix_tree_find_first_occupied_interval(
                        src_vs,
                        rollback_iter,
                        searched_start,
                        &rollback_searched_start,
                        &rollback_searched_end,
                        &rollback_range_flags)) {
                        bool need_bind = rollback_range_flags
                                         & PAGE_ENTRY_VALID;
                        if (need_bind) {
                                clone_rollback_src_write_pte_pages(src_vs,
                                                                   rollback_searched_start,
                                                                   rollback_searched_end,
                                                                   handler);
                                if (vmm_radix_tree_change_range_flag(
                                            src_vs,
                                            rollback_searched_start,
                                            rollback_searched_end,
                                            clear_mask_u64(rollback_range_flags,
                                                           PAGE_ENTRY_COW))
                                    != REND_SUCCESS) {
                                        pr_error(
                                                "[Error] vspace clone roll back change range flags error\n");
                                }
                        }
                        rollback_iter = rollback_searched_end;
                }
        }
        goto free_vspace;
free_vspace:
        (void)vmm_radix_tree_unlock_range_big(
                src_vs, PAGE_SIZE, USER_SPACE_TOP + 1);
free_vspace_no_unlock:
        del_vspace(&dst_vs);
        return e;
}

error_t register_vspace(VSpace* vs, VSpace* root_vs, u64 vspace_id)
{
        if (!vs || !root_vs || vs->registered)
                return -E_IN_PARAM;
        error_t e = REND_SUCCESS;
        lock_cas(&root_vs->vspace_register_lock);
        e = vspace_rb_tree_insert(vs, root_vs);
        unlock_cas(&root_vs->vspace_register_lock);
        if (e == REND_SUCCESS) {
                vs->registered = true;
                vs->root_vs = root_vs;
                vs->vspace_id = vspace_id;
        }
        return e;
}
error_t unregister_vspace(VSpace* vs)
{
        if (!vs)
                return -E_IN_PARAM;
        /*if vs is unregistered, no need to unlink*/
        if (!vs->registered)
                return REND_SUCCESS;

        if (!vs->root_vs)
                return -E_IN_PARAM;

        lock_cas(&vs->root_vs->vspace_register_lock);
        vspace_rb_tree_remove(vs, vs->root_vs);
        unlock_cas(&vs->root_vs->vspace_register_lock);
        vs->registered = false;
        vs->root_vs = NULL;
        return REND_SUCCESS;
}

error_t del_vspace(VSpace** vs)
{
        if (!(*vs))
                return REND_SUCCESS;
        if ((*vs) == (*vs)->root_vs)
                return REND_SUCCESS;

        VSpace* vspace = *vs;

        if (vspace->registered)
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
                error_t root_err = vspace_free_root_page(vspace, map_handler);
                if (root_err != REND_SUCCESS && ret == REND_SUCCESS)
                        ret = root_err;
        }

        asid_free(vspace->asid);

        struct allocator* cpu_kallocator = percpu(kallocator);
        if (cpu_kallocator)
                cpu_kallocator->m_free(cpu_kallocator, (void*)vspace);
        else if (ret == REND_SUCCESS)
                ret = -E_RENDEZVOS;
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
