#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>

int nexus_test(void)
{
        pr_info("sizeof nexus node is 0x%x\n", sizeof(struct nexus_node));
        return 0;
}