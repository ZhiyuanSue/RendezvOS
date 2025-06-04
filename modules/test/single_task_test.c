#include <modules/test/test.h>
#include <modules/elf/elf.h>
#include <rendezvos/mm/mm.h>
#include <rendezvos/task/tcb.h>

extern u64 _num_app;
void gen_task_from_elf(vaddr elf_start, Thread_Base *test_thread)
{
        pr_info("start gen task from elf\n");
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

                test_task->pid = get_new_pid();
                test_task->vs = new_vspace();
                init_vspace(test_task->vs,
                            new_vs_root(0, &percpu(Map_Handler)),
                            test_task->pid);

                Thread_Base *test_thread = create_thread(
                        (void *)gen_task_from_elf, app_start_ptr, app_end_ptr);
                if (!test_thread) {
                        pr_error("[Error] create test_thread fail\n");
                        return -E_REND_TEST;
                }
                thread_join(test_task, test_thread);
                percpu(core_tm)->schedule(percpu(core_tm));
        }
        return 0;
}