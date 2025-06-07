#include <modules/test/test.h>
#include <modules/elf/elf.h>
#include <rendezvos/mm/mm.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/elf_loader.h>

extern u64 _num_app;
int task_test(void)
{
        pr_info("%x apps\n", _num_app);
        u64 *app_start_ptr, *app_end_ptr;
        for (int i = 0; i < _num_app; i++) {
                app_start_ptr =
                        (u64 *)((vaddr)(&_num_app) + (i * 2 + 1) * sizeof(u64));
                app_end_ptr =
                        (u64 *)((vaddr)(&_num_app) + (i * 2 + 2) * sizeof(u64));
                u64 app_start = *(app_start_ptr);
                u64 app_end = *(app_end_ptr);

                error_t e = gen_task_from_elf(app_start, app_end);
                if (e)
                        continue;
                schedule(percpu(core_tm));
        }
        return 0;
}