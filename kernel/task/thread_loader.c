#include <rendezvos/task/thread_loader.h>
#include <modules/log/log.h>
#include <rendezvos/smp/percpu.h>

/* Owner ref drop: may trigger free_vspace_ref -> del_vspace. */
static void elf_task_release_vspace_ref(Tcb_Base *elf_task)
{
        if (!elf_task || !elf_task->vs)
                return;
        VS_Common *vs = elf_task->vs;
        elf_task->vs = NULL;
        if (vs_common_is_table_vspace(vs) && vs != &root_vspace)
                ref_put(&vs->refcount, free_vspace_ref);
}

/*
 * User root paddr allocated but nexus failed to register vspace: del_vspace
 * cannot nexus_delete. Free user root frames first, then owner ref_put.
 */
static void elf_task_teardown_user_root_after_nexus_fail(Tcb_Base *elf_task)
{
        if (!elf_task || !elf_task->vs)
                return;
        VS_Common *vs = elf_task->vs;
        if (vs->vspace_root_addr) {
                error_t user_err =
                        vspace_free_user_pt(vs, &percpu(Map_Handler));
                if (user_err != REND_SUCCESS)
                        pr_error(
                                "[ Error ] vspace_free_user_pt cleanup failed e=%d\n",
                                (int)user_err);
                if (vs->vspace_root_addr) {
                        error_t root_err =
                                vspace_free_root_page(vs, &percpu(Map_Handler));
                        if (root_err != REND_SUCCESS)
                                pr_error(
                                        "[ Error ] vspace_free_root_page cleanup failed e=%d\n",
                                        (int)root_err);
                }
        }
        elf_task_release_vspace_ref(elf_task);
}

static void elf_task_delete_tcb(Tcb_Base *elf_task)
{
        if (!elf_task)
                return;
        if (delete_task(elf_task) != REND_SUCCESS)
                pr_error("[ Error ] delete_task cleanup failed\n");
}

vaddr generate_user_stack(VS_Common *vs)
{
        if (!vs) {
                return 0;
        }
        /*alloc the user stack for this thread*/
        int page_num = thread_ustack_page_num;
        ENTRY_FLAGS_t page_flags = PAGE_ENTRY_USER | PAGE_ENTRY_VALID
                                   | PAGE_ENTRY_WRITE | PAGE_ENTRY_READ;
        vaddr get_free_page_vaddr =
                (vaddr)get_free_page(page_num,
                                     USER_SPACE_TOP - page_num * PAGE_SIZE,
                                     percpu(nexus_root),
                                     vs,
                                     page_flags);
        if (!get_free_page_vaddr) {
                return 0;
        }
        vaddr user_sp = get_free_page_vaddr + page_num * PAGE_SIZE - 8;
        return user_sp;
}
error_t elf_Phdr_64_load_handle(vaddr elf_start, Elf64_Phdr *phdr_ptr,
                                VS_Common *vs)
{
        if (!vs || !phdr_ptr || !elf_start) {
                return -E_IN_PARAM;
        }
        /*
                TODO: we should add a data structure to record the used
           user space, which will be used for clean. and it might affect the
           nexus
        */
        print_elf_ph64(phdr_ptr);

        vaddr ph_start = phdr_ptr->p_vaddr;
        u64 offset = phdr_ptr->p_offset;

        vaddr aligned_start = ROUND_DOWN(ph_start, PAGE_SIZE);
        // u64 aligned_offset = ROUND_DOWN(offset, PAGE_SIZE);

        u64 map_length = ph_start + phdr_ptr->p_memsz - aligned_start;
        u64 page_num = ROUND_UP(map_length, PAGE_SIZE) / PAGE_SIZE;

        /*page flags*/
        ENTRY_FLAGS_t page_flags = PAGE_ENTRY_USER | PAGE_ENTRY_VALID;
        if (phdr_ptr->p_flags & PF_X) {
                page_flags |= PAGE_ENTRY_EXEC;
        }
        if (phdr_ptr->p_flags & PF_W) {
                page_flags |= PAGE_ENTRY_WRITE;
        }
        if (phdr_ptr->p_flags & PF_R) {
                page_flags |= PAGE_ENTRY_READ;
        }

        /*using the nexus to map*/
        void *page_ptr = get_free_page(
                page_num, aligned_start, percpu(nexus_root), vs, page_flags);
        if (!page_ptr)
                return -E_RENDEZVOS;

        memcpy((void *)(ph_start),
               (void *)(elf_start + offset),
               phdr_ptr->p_filesz);
        /*bss*/
        if (phdr_ptr->p_memsz > phdr_ptr->p_filesz) {
                /*need to fill in the 0*/
                vaddr bss_start = ph_start + phdr_ptr->p_filesz;
                vaddr bss_end = ph_start + phdr_ptr->p_memsz;
                u64 bss_size = bss_end - bss_start;
                memset((void *)bss_start, 0, bss_size);
        }
        return REND_SUCCESS;
}
error_t elf_Phdr_64_dynamic_handle(vaddr elf_start, Elf64_Phdr *phdr_ptr,
                                   VS_Common *vs)
{
        if (!vs || !elf_start || !phdr_ptr) {
                return -E_IN_PARAM;
        }
        print_elf_ph64(phdr_ptr);
        return REND_SUCCESS;
}
error_t run_elf_program(vaddr elf_start, vaddr elf_end, VS_Common *vs,
                        elf_init_handler_t elf_init)
{
        pr_info("start gen task from elf start %lx end %lx vs %lx\n",
                elf_start,
                elf_end,
                vs);
        if (!elf_start || !elf_end || !vs) {
                return -E_IN_PARAM;
        }
        Thread_Base *elf_thread = get_cpu_current_thread();
        error_t e = -E_RENDEZVOS;
        if (!check_elf_header(elf_start)) {
                pr_error("[ERROR] bad elf file\n");
                return -E_RENDEZVOS;
        }
        if (get_elf_class(elf_start) != ELFCLASS64) {
                pr_error("[Error] Rendezvos not support elf32 file running\n");
                return -E_RENDEZVOS;
        }
        Elf64_Ehdr *elf_header = (Elf64_Ehdr *)elf_start;
        vaddr entry_addr = elf_header->e_entry;
        vaddr max_load_end = 0;
        for_each_program_header_64(elf_start)
        {
                /*handle LOAD*/
                if (phdr_ptr->p_type == PT_LOAD) {
                        vaddr end = (vaddr)phdr_ptr->p_vaddr
                                    + (vaddr)phdr_ptr->p_memsz;
                        if (end > max_load_end)
                                max_load_end = end;
                        e = elf_Phdr_64_load_handle(elf_start, phdr_ptr, vs);
                        if (e) {
                                pr_error("[ Error ]elf load handle fail\n");
                                return e;
                        }
                }
        }
        for_each_program_header_64(elf_start)
        {
                /*handle DYNAMIC*/
                if (phdr_ptr->p_type == PT_DYNAMIC) {
                        e = elf_Phdr_64_dynamic_handle(elf_start, phdr_ptr, vs);
                        if (e) {
                                pr_error("[ Error ] elf dynamic handle fail\n");
                                return e;
                        }
                }
        }
        vaddr user_sp = arch_get_thread_user_sp(&elf_thread->ctx);
        user_sp -= 8;
        *((u64 *)user_sp) = 0;
        arch_set_thread_user_sp(&elf_thread->ctx, user_sp);
        if (elf_init) {
                elf_load_info_t info = {
                        .elf_start = elf_start,
                        .elf_end = elf_end,
                        .entry_addr = entry_addr,
                        .max_load_end = ROUND_UP(max_load_end, PAGE_SIZE),
                        .user_sp = user_sp,
                        .phnum = elf_header->e_phnum,
                        .phentsize = elf_header->e_phentsize,
                };
                elf_init(&elf_thread->ctx, &info);
        }

        Thread_Base *current_thread = percpu(core_tm)->current_thread;
        arch_drop_to_user(current_thread->kstack_bottom, entry_addr);
        return REND_SUCCESS;
}
/*we must load all the elf file into kernel memory before we use this function*/
error_t gen_task_from_elf(Thread_Base **elf_thread_ptr,
                          size_t append_tcb_info_len,
                          size_t append_thread_info_len, vaddr elf_start,
                          vaddr elf_end, elf_init_handler_t elf_init)
{
        if (!elf_start || !elf_end) {
                return -E_IN_PARAM;
        }
        error_t e = REND_SUCCESS;
        Tcb_Base *elf_task =
                new_task_structure(percpu(kallocator), append_tcb_info_len);
        if (!elf_task)
                return -E_RENDEZVOS;

        elf_task->pid = get_new_id(&pid_manager);
        /*--- vspace part ---*/
        elf_task->vs = new_vspace();
        if (!elf_task->vs) {
                e = -E_RENDEZVOS;
                elf_task_delete_tcb(elf_task);
                return e;
        }
        paddr new_vs_paddr = new_vs_root(0, &percpu(Map_Handler));
        if (!new_vs_paddr) {
                e = -E_RENDEZVOS;
                elf_task_release_vspace_ref(elf_task);
                elf_task_delete_tcb(elf_task);
                return e;
        }
        set_vspace_root_addr(elf_task->vs, new_vs_paddr);
        /* Per-vspace nexus root node (one page); not the same as per-CPU
         * nexus_root. */
        struct nexus_node *new_vs_vspace_node =
                nexus_create_vspace_root_node(percpu(nexus_root), elf_task->vs);
        if (!new_vs_vspace_node) {
                e = -E_RENDEZVOS;
                elf_task_teardown_user_root_after_nexus_fail(elf_task);
                elf_task_delete_tcb(elf_task);
                return e;
        }
        init_vspace(elf_task->vs, elf_task->pid, new_vs_vspace_node);
        /*--- end vspace part ---*/
        e = add_task_to_manager(percpu(core_tm), elf_task);
        if (e) {
                elf_task_release_vspace_ref(elf_task);
                elf_task_delete_tcb(elf_task);
                return e;
        }

        Thread_Base *elf_thread = create_thread((void *)run_elf_program,
                                                append_thread_info_len,
                                                4,
                                                elf_start,
                                                elf_end,
                                                elf_task->vs,
                                                elf_init);
        if (!elf_thread) {
                e = -E_RENDEZVOS;
                goto create_thread_error;
        }
        vaddr user_sp = generate_user_stack(elf_task->vs);
        if (!user_sp) {
                e = -E_RENDEZVOS;
                goto generate_user_stack_error;
        }
        arch_set_thread_user_sp(&elf_thread->ctx, user_sp);

        thread_set_flags(elf_thread, THREAD_FLAG_USER);
        if (!elf_thread) {
                pr_error("[Error] create elf_thread fail\n");
                return -E_RENDEZVOS;
        }
        if (elf_thread_ptr)
                *elf_thread_ptr = elf_thread;
        e = thread_join(elf_task, elf_thread);
        if (e) {
                goto thread_join_error;
        }
        return REND_SUCCESS;
thread_join_error:
        del_thread_from_manager(elf_thread);
        del_thread_from_task(elf_thread);
generate_user_stack_error:
        del_thread_structure(elf_thread);
create_thread_error:
        if (del_task_from_manager(elf_task) != REND_SUCCESS) {
                pr_error(
                        "fail to delete task from task manager, please check\n");
        }
        elf_task_release_vspace_ref(elf_task);
        elf_task_delete_tcb(elf_task);
        return e;
}
error_t gen_thread_from_func(Thread_Base **func_thread_ptr, kthread_func thread,
                             char *thread_name, Task_Manager *tm, void *arg)
{
        if (!thread_name || !tm) {
                return -E_IN_PARAM;
        }
        Thread_Base *func_t;
        func_t = create_thread((void *)thread, 0, 1, arg);
        if (!func_t) {
                pr_error("[Error] create kernel thread fail\n");
                return -E_RENDEZVOS;
        }
        thread_set_name(thread_name, func_t);
        if (func_thread_ptr)
                *func_thread_ptr = func_t;
        error_t e = thread_join(tm->root_task, func_t);
        return e;
}