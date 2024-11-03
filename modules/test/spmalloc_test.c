#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/spmalloc.h>
extern struct allocator* allocator_pool[SHAMPOOS_MAX_CPU_NUMBER];
/*
for it's too complex for us, we first check the chunk
and then check the sp_malloc
*/
static int chunk_test(void)
{
        struct allocator* malloc = allocator_pool[0];
        void* test_alloc = malloc->m_alloc(malloc, 8);
        *((u64*)test_alloc) = 0;
        malloc->m_free(malloc, test_alloc);
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