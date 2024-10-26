#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/spmalloc.h>

/*
for it's too complex for us, we first check the chunk
and then check the sp_malloc
*/
static int chunk_test(void)
{
        return 0;
}
int spmalloc_test(void)
{
        bool succ = 0;
        if (succ = chunk_test()) {
                return succ;
        }
        return 0;
}