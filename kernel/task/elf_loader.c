#include <rendezvos/task/elf_loader.h>
#include <modules/log/log.h>

error_t elf_Phdr_64_load_handle(vaddr elf_start, Elf64_Phdr *phdr_ptr,
                                VSpace *vs)
{
        /*
                TODO: we should add a data structure to record the used
           user space, which will be used for clean. and it might affect the
           nexus
        */
        print_elf_ph64(phdr_ptr);

        vaddr ph_start = phdr_ptr->p_vaddr;
        u64 offset = phdr_ptr->p_offset;

        vaddr aligned_start = ROUND_DOWN(ph_start, PAGE_SIZE);
        u64 aligned_offset = ROUND_DOWN(offset, PAGE_SIZE);

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
        void *page_ptr = get_free_page(page_num,
                                       ZONE_NORMAL,
                                       aligned_start,
                                       percpu(nexus_root),
                                       vs,
                                       page_flags);
        if (!page_ptr)
                return -E_RENDEZVOS;

        memcpy((void *)(ph_start),
               (void *)(elf_start + phdr_ptr->p_offset),
               phdr_ptr->p_filesz);
        /*bss*/
        if (phdr_ptr->p_memsz > phdr_ptr->p_filesz) {
                /*need to fill in the 0*/
                vaddr bss_start = ph_start + phdr_ptr->p_filesz;
                vaddr bss_end = ph_start + phdr_ptr->p_memsz;
                u64 bss_size = bss_end - bss_start;
        }
        return 0;
}
error_t elf_Phdr_64_dynamic_handle(vaddr elf_start, Elf64_Phdr *phdr_ptr,
                                   VSpace *vs)
{
        print_elf_ph64(phdr_ptr);
        return 0;
}
error_t run_elf_program(vaddr elf_start, vaddr elf_end, VSpace *vs)
{
        pr_info("start gen task from elf start %x end %x vs %x\n",
                elf_start,
                elf_end,
                vs);
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
                if (phdr_ptr->p_type == PT_LOAD)
                        elf_Phdr_64_load_handle(elf_start, phdr_ptr, vs);
        }
        for_each_program_header_64(elf_start)
        {
                /*handle DYNAMIC*/
                if (phdr_ptr->p_type == PT_DYNAMIC)
                        elf_Phdr_64_dynamic_handle(elf_start, phdr_ptr, vs);
        }
        /*alloc the user stack for this thread*/
        int page_num = thread_ustack_page_num;
        ENTRY_FLAGS_t page_flags = PAGE_ENTRY_USER | PAGE_ENTRY_VALID
                                   | PAGE_ENTRY_WRITE | PAGE_ENTRY_READ;
        vaddr user_sp = get_free_page(page_num,
                                      ZONE_NORMAL,
                                      USER_SPACE_TOP - page_num * PAGE_SIZE,
                                      percpu(nexus_root),
                                      vs,
                                      page_flags)
                        + page_num * PAGE_SIZE;

        arch_drop_to_user(user_sp, entry_addr);
        return 0;
}
/*we must load all the elf file into kernel memory before we use this function*/
error_t gen_task_from_elf(vaddr elf_start, vaddr elf_end)
{
        error_t e = 0;
        Tcb_Base *elf_task = new_task();
        if (!elf_task) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }

        elf_task->pid = get_new_pid();
        /*vspace part*/
        elf_task->vs = new_vspace();
        if (!elf_task->vs) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }
        paddr new_vs_paddr = new_vs_root(0, &percpu(Map_Handler));
        if (!new_vs_paddr) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }
        set_vspace_root_addr(elf_task->vs, new_vs_paddr);
        struct nexus_node *new_vs_nexus_root =
                nexus_create_vspace_root_node(nexus_root, elf_task->vs);
        init_vspace(elf_task->vs, elf_task->pid, new_vs_nexus_root);
        /*--- end vspace part ---*/
        add_task_to_manager(percpu(core_tm), elf_task);

        Thread_Base *elf_thread = create_thread(
                (void *)run_elf_program, 3, elf_start, elf_end, elf_task->vs);
        thread_set_flags(THREAD_FLAG_USER, elf_thread);
        if (!elf_thread) {
                pr_error("[Error] create elf_thread fail\n");
                return -E_RENDEZVOS;
        }
        e = thread_join(elf_task, elf_thread);
gen_task_from_elf_error:
        return e;
}