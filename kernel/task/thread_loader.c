#include <rendezvos/task/thread_loader.h>
#include <modules/log/log.h>

vaddr generate_user_stack(VSpace *vs, elf_task_set_user_stack_func func)
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
        if (func) {
                /*the kernel might pass argc and argv to the task,
                for some system like linux, it might pass the Auxiliary Vector
                and other things, we use a callback function to deal with it*/
                func(&user_sp);
        } else {
                /*even we put nothing on the stack ,we should put an argc on it
                 * (typically should be 1, but of course we do not put an argv
                 * list, so just set 0 as default),otherwise a page fault will
                 * happen*/
                user_sp -= 8;
                *((u64 *)user_sp) = 0;
        }
        return user_sp;
}
error_t elf_Phdr_64_load_handle(vaddr elf_start, Elf64_Phdr *phdr_ptr,
                                VSpace *vs)
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
                                   VSpace *vs)
{
        if (!vs || !elf_start || !phdr_ptr) {
                return -E_IN_PARAM;
        }
        print_elf_ph64(phdr_ptr);
        return REND_SUCCESS;
}
error_t run_elf_program(vaddr elf_start, vaddr elf_end, VSpace *vs)
{
        pr_info("start gen task from elf start %x end %x vs %x\n",
                elf_start,
                elf_end,
                vs);
        if (!elf_start || !elf_end || !vs) {
                return -E_IN_PARAM;
        }
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
        for_each_program_header_64(elf_start)
        {
                /*handle LOAD*/
                if (phdr_ptr->p_type == PT_LOAD) {
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

        Thread_Base *current_thread = percpu(core_tm)->current_thread;
        arch_drop_to_user(current_thread->kstack_bottom, entry_addr);
        return REND_SUCCESS;
}
/*we must load all the elf file into kernel memory before we use this function*/
error_t gen_task_from_elf(Thread_Base **elf_thread_ptr,
                          size_t append_tcb_info_len,
                          size_t append_thread_info_len, vaddr elf_start,
                          vaddr elf_end, elf_task_set_user_stack_func func)
{
        if (!elf_thread_ptr || !elf_start || !elf_end) {
                return -E_IN_PARAM;
        }
        error_t e = REND_SUCCESS;
        Tcb_Base *elf_task =
                new_task_structure(percpu(kallocator), append_tcb_info_len);
        if (!elf_task) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }

        elf_task->pid = get_new_pid();
        /*--- vspace part ---*/
        elf_task->vs = new_vspace();
        if (!elf_task->vs) {
                e = -E_RENDEZVOS;
                goto new_vspace_error;
        }
        paddr new_vs_paddr = new_vs_root(0, &percpu(Map_Handler));
        if (!new_vs_paddr) {
                e = -E_RENDEZVOS;
                goto new_vs_root_error;
        }
        set_vspace_root_addr(elf_task->vs, new_vs_paddr);
        struct nexus_node *new_vs_nexus_root =
                nexus_create_vspace_root_node(nexus_root, elf_task->vs);
        if (!new_vs_nexus_root) {
                goto nexus_create_vspace_root_node_error;
        }
        init_vspace(elf_task->vs, elf_task->pid, new_vs_nexus_root);
        /*--- end vspace part ---*/
        e = add_task_to_manager(percpu(core_tm), elf_task);
        if (e) {
                goto add_task_to_manager_error;
        }

        Thread_Base *elf_thread = create_thread((void *)run_elf_program,
                                                append_thread_info_len,
                                                3,
                                                elf_start,
                                                elf_end,
                                                elf_task->vs);
        if (!elf_thread) {
                e = -E_RENDEZVOS;
                goto create_thread_error;
        }
        vaddr user_sp = generate_user_stack(elf_task->vs, func);
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
add_task_to_manager_error:
        nexus_delete_vspace(new_vs_nexus_root, elf_task->vs);
nexus_create_vspace_root_node_error:
        unset_vspace_root_addr(elf_task->vs);
        del_vs_root(new_vs_paddr, &percpu(Map_Handler));
new_vs_root_error:
        del_vspace(&elf_task->vs);
new_vspace_error:
        delete_task(elf_task);
gen_task_from_elf_error:
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
        error_t e = thread_join(tm->current_task, func_t);
        return e;
}