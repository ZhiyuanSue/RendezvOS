/*I don't think the multi cpu elf read test is need*/
#include <modules/test/test.h>

extern u64 _num_app;
int elf_read_test(void)
{
        pr_info("%x\n", _num_app);
        u64* app_arr = &_num_app;
        u64 *app_start, *app_end;
        for (int i = 0; i < _num_app; i++) {
                app_start = app_arr + i * 2 + 1;
                app_end = app_arr + i * 2 + 2;
                pr_info("app %d start:%x end:%x\n", i, app_start, app_end);
        }
        return 0;
}