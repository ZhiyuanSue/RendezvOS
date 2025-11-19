#include <modules/test/test.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/pmm.h>
#define PPN_TEST_CASE_NUM 10

int pmm_test(void)
{
        i64 alloc_ppn[PPN_TEST_CASE_NUM];
        size_t alloced_page_number;

        for (int i = 0, pg_size = 1; i < PPN_TEST_CASE_NUM; ++i, pg_size *= 2) {
                alloc_ppn[i] = mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                        mem_zones[ZONE_NORMAL].pmm,
                        pg_size,
                        &alloced_page_number);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              pg_size,
                              alloc_ppn[i]);
        }
        for (int i = 0, pg_size = 1; i < PPN_TEST_CASE_NUM; ++i, pg_size *= 2) {
                if (mem_zones[ZONE_NORMAL].pmm->pmm_free(
                            mem_zones[ZONE_NORMAL].pmm, alloc_ppn[i], pg_size)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                        mem_zones[ZONE_NORMAL].pmm,
                        i * 2 + 3,
                        &alloced_page_number);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              i * 2 + 3,
                              alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                if (mem_zones[ZONE_NORMAL].pmm->pmm_free(
                            mem_zones[ZONE_NORMAL].pmm,
                            alloc_ppn[i],
                            i * 2 + 3)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                        mem_zones[ZONE_NORMAL].pmm,
                        i * 2 + 3,
                        &alloced_page_number);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              i * 2 + 3,
                              alloc_ppn[i]);
                if (mem_zones[ZONE_NORMAL].pmm->pmm_free(
                            mem_zones[ZONE_NORMAL].pmm,
                            alloc_ppn[i],
                            i * 2 + 3)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        // try to alloc all the memory,then try to alloc will lead to an error
        // if no such an error, the boundary conditions is error
        while (mem_zones[ZONE_NORMAL].pmm->total_avaliable_pages) {
                i64 tmp = mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                        mem_zones[ZONE_NORMAL].pmm, 1, &alloced_page_number);
                alloc_ppn[mem_zones[ZONE_NORMAL].pmm->total_avaliable_pages
                          % PPN_TEST_CASE_NUM] = tmp;

                if (tmp <= 0) {
                        pr_error("[ ERROR ] pmm alloc error %d\n",
                                 tmp) goto pmm_test_error;
                }
        }
        if (mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                    mem_zones[ZONE_NORMAL].pmm, 1, &alloced_page_number)
            != -E_RENDEZVOS) {
                pr_error("alloc boundary error\n");
                goto pmm_test_error;
        }
        // then we free some of the pages
        // and try to alloc again
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                if (mem_zones[ZONE_NORMAL].pmm->pmm_free(
                            mem_zones[ZONE_NORMAL].pmm, alloc_ppn[i], 1)) {
                        pr_error("free error\n");
                        goto pmm_test_error;
                } else
                        debug("free ppn 0x%x success\n", alloc_ppn[i]);
        }
        for (int i = 0; i < PPN_TEST_CASE_NUM; ++i) {
                alloc_ppn[i] = mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                        mem_zones[ZONE_NORMAL].pmm, 1, &alloced_page_number);
                if (alloc_ppn[i] == -E_RENDEZVOS) {
                        pr_error("alloc error\n");
                        goto pmm_test_error;
                } else
                        debug("try to get %x pages ,and alloc ppn 0x%x\n",
                              1,
                              alloc_ppn[i]);
        }
        if (mem_zones[ZONE_NORMAL].pmm->pmm_alloc(
                    mem_zones[ZONE_NORMAL].pmm, 1, &alloced_page_number)
            != -E_RENDEZVOS) {
                pr_error("alloc boundary error\n");
                goto pmm_test_error;
        }
        return 0;
pmm_test_error:
        return -E_REND_TEST;
}