#include <modules/test/test.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/buddy_pmm.h>
#define PPN_TEST_CASE_NUM 10

extern struct buddy buddy_pmm;
int pmm_test(void)
{
        i64 alloc_ppn[PPN_TEST_CASE_NUM];

        for (int i = 0, pg_size = 1; i < PPN_TEST_CASE_NUM; ++i, pg_size *= 2) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(pg_size, ZONE_NORMAL);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              pg_size,
                              alloc_ppn[i]);
        }
        for (int i = 0, pg_size = 1; i < PPN_TEST_CASE_NUM; ++i, pg_size *= 2) {
                if (buddy_pmm.pmm_free(alloc_ppn[i], pg_size)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(i * 2 + 3, ZONE_NORMAL);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              i * 2 + 3,
                              alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                if (buddy_pmm.pmm_free(alloc_ppn[i], i * 2 + 3)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(i * 2 + 3, ZONE_NORMAL);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              i * 2 + 3,
                              alloc_ppn[i]);
                if (buddy_pmm.pmm_free(alloc_ppn[i], i * 2 + 3)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        // try to alloc all the memory,then try to alloc will lead to an error
        // if no such an error, the boundary conditions is error
        while (buddy_pmm.zone->zone_total_avaliable_pages) {
                i64 tmp = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
                alloc_ppn[buddy_pmm.zone->zone_total_avaliable_pages
                          % PPN_TEST_CASE_NUM] = tmp;

                if (tmp <= 0) {
                        pr_error("[ ERROR ] pmm alloc error %d\n",
                                 tmp) goto pmm_test_error;
                }
        }
        if (buddy_pmm.pmm_alloc(1, ZONE_NORMAL) != -E_RENDEZVOS) {
                pr_error("alloc boundary error\n");
                goto pmm_test_error;
        }
        // then we free some of the pages
        // and try to alloc again
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                if (buddy_pmm.pmm_free(alloc_ppn[i], 1)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = buddy_pmm.pmm_alloc(1, ZONE_NORMAL);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              1,
                              alloc_ppn[i]);
        }
        if (buddy_pmm.pmm_alloc(1, ZONE_NORMAL) != -E_RENDEZVOS) {
                pr_error("alloc boundary error\n");
                goto pmm_test_error;
        }
        return 0;
pmm_test_error:
        return -E_REND_TEST;
}