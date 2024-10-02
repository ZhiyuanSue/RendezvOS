#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>

extern struct nexus_node* nexus_root;
int nexus_test(void)
{
        nexus_print(nexus_root);
        return 0;
}