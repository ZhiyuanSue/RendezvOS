#include <modules/test/test.h>

void BSP_test()
{
#ifdef TEST
        single_cpu_test();
        multi_cpu_test();
#endif
}
void AP_test()
{
#ifdef TEST
        multi_cpu_test();
#endif
}