#include <modules/test/test.h>
#include <modules/elf/elf.h>
#include <rendezvos/mm/mm.h>
#include <rendezvos/task/tcb.h>

extern u64 _num_app;
void gen_task_from_elf(vaddr elf_start, Thread_Base *test_thread)
{
        if (!check_elf_header(elf_start)) {
                pr_error("[ERROR] bad elf file\n");
                return;
        }
}
int task_test(void)
{
        pr_info("%x\n", _num_app);
        u64 *app_start_ptr, *app_end_ptr;
        for (int i = 0; i < _num_app; i++) {
                app_start_ptr =
                        (u64 *)((vaddr)(&_num_app) + (i * 2 + 1) * sizeof(u64));
                app_end_ptr =
                        (u64 *)((vaddr)(&_num_app) + (i * 2 + 2) * sizeof(u64));
                u64 app_start = *(app_start_ptr);

                Tcb_Base *test_task = new_task();
                Thread_Base *test_thread = new_thread();
                add_thread_to_task(test_task, test_thread);
                add_task_to_manager(percpu(core_tm), test_task);
                add_thread_to_manager(percpu(core_tm), test_thread);

                gen_task_from_elf((vaddr)app_start, test_thread);
        }
        return 0;
}