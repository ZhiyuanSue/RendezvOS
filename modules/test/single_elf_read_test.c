/*I don't think the multi cpu elf read test is need*/
#include <modules/test/test.h>

extern u64 _num_app;
int elf_read_test(void)
{
        pr_info("%x\n", _num_app);
        return 0;
}