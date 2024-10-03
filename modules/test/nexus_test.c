#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>

extern struct nexus_node* nexus_root;
int nexus_test(void)
{
        /*after the nexus init, we try to print it first*/
        nexus_print(nexus_root);
        return 0;
}