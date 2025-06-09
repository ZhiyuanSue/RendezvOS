#include <rendezvos/task/elf_loader.h>
#include <modules/log/log.h>

error_t load_elf_Phdr_64(Elf64_Phdr *phdr_ptr)
{
        /*
                TODO: we should add a data structure to record the used
           user space, which will be used for clean. and it might affect the
           nexus
        */
        print_elf_ph64(phdr_ptr);

        return 0;
}
error_t load_elf_program(vaddr elf_start, vaddr elf_end)
{
        pr_info("start gen task from elf start %x end %x\n",
                elf_start,
                elf_end);
        if (!check_elf_header(elf_start)) {
                pr_error("[ERROR] bad elf file\n");
                return -E_RENDEZVOS;
        }
        if (get_elf_class(elf_start) == ELFCLASS32) {
                pr_error("[Error] Rendezvos not support elf32 file running\n");
                return -E_RENDEZVOS;
        } else if (get_elf_class(elf_start) == ELFCLASS64) {
                for_each_program_header_64(elf_start)
                {
                        load_elf_Phdr_64(phdr_ptr);
                }
        }
        return 0;
}
error_t gen_task_from_elf(vaddr elf_start, vaddr elf_end)
{
        error_t e = 0;
        Tcb_Base *elf_task = new_task();
        if (!elf_task) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }

        elf_task->pid = get_new_pid();
        elf_task->vs = new_vspace();
        if (!elf_task->vs) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }
        paddr new_root = new_vs_root(0, &percpu(Map_Handler));
        if (!new_root) {
                e = -E_RENDEZVOS;
                goto gen_task_from_elf_error;
        }
        init_vspace(elf_task->vs, new_root, elf_task->pid);
        add_task_to_manager(percpu(core_tm), elf_task);

        Thread_Base *elf_thread =
                create_thread((void *)load_elf_program, 2, elf_start, elf_end);
        thread_set_flags(THREAD_FLAG_USER, elf_thread);
        if (!elf_thread) {
                pr_error("[Error] create elf_thread fail\n");
                return -E_RENDEZVOS;
        }
        e = thread_join(elf_task, elf_thread);
gen_task_from_elf_error:
        return e;
}