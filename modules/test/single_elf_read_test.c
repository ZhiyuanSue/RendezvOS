/*I don't think the multi cpu elf read test is need*/
#include <modules/test/test.h>
#include <modules/elf/elf.h>

extern u64 _num_app;
void elf_read(vaddr elf_start)
{
        if(!check_elf_header(elf_start)){
                pr_error("[ERROR] bad elf file\n");
        }
}
int elf_read_test(void)
{
        pr_info("%x\n", _num_app);
        u64 *app_arr = &_num_app;
        u64 *app_start_ptr, *app_end_ptr;
        for (int i = 0; i < _num_app; i++) {
                app_start_ptr = app_arr + i * 2 + 1;
                app_end_ptr = app_arr + i * 2 + 2;
                u64 app_start = *(app_start_ptr); 
                u64 app_end = *(app_end_ptr); 
                pr_info("app %d start:%x end:%x\n", i, app_start, app_end);
                elf_read((vaddr)app_start);
        }
        return 0;
}