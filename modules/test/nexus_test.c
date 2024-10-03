#include <modules/test/test.h>
#include <shampoos/mm/nexus.h>
#include <modules/log/log.h>
#define NR_MAX_TEST NEXUS_PER_PAGE*4
extern struct nexus_node* nexus_root;
void* test_ptrs[NR_MAX_TEST];
int nexus_test(void)
{
        /*after the nexus init, we try to print it first*/
        nexus_print(nexus_root);
        for(int i=0;i<NR_MAX_TEST;i++){
                int page_num=1;
                if(i%2)
                        page_num=MIDDLE_PAGE_SIZE/PAGE_SIZE;
                test_ptrs[i]=get_free_page(page_num,ZONE_NORMAL,nexus_root);
        }
        nexus_print(nexus_root);
        for(int i=0;i<NR_MAX_TEST;i++){
                if(test_ptrs[i] && i%2)
                        free_pages(test_ptrs[i],nexus_root);
        }
        nexus_print(nexus_root);
        for(int i=0;i<NR_MAX_TEST;i++){
                if(test_ptrs[i] && !(i%2))
                        free_pages(test_ptrs[i],nexus_root);
        }
        nexus_print(nexus_root);
        return 0;
}