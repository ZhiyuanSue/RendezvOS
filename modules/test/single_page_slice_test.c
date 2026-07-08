#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/mm/page_slice.h>
#include <rendezvos/mm/kmalloc.h>
#include <rendezvos/smp/percpu.h>
#include <common/stddef.h>
#include <common/rand.h>
#include <common/string.h>

/*
 * Backing kva: upper layer kmalloc → insert(flags=0) → slice m_free on remove.
 * Radix shells: slice internal.  See ps_clear_leaf_entry in page_slice.c.
 *
 * Fixed regression + rand64 fuzz (multiple fixed seeds).
 *
 * INDEX3 = a third index radix level (height 4 on slice->root).  Logical max
 * span is ~32M pages (~128 TiB byte cap in header).  Dense INDEX3 fuzz would
 * allocate huge radix + many leaves; we only use a sparse fuzz (cap mapped
 * pages) and fixed anchor pgoffs at 0 / INDEX2 / INDEX3 boundaries.
 */

#define PS_TEST_PGOFF_INDEX2 \
        (PAGE_SLICE_LEAF_CAPACITY * PAGE_SLICE_INDEX_CAPACITY)
#define PS_TEST_SLICE_INDEX2 ((PS_TEST_PGOFF_INDEX2 + 1ULL) * PAGE_SIZE)

#define PS_TEST_PGOFF_INDEX3 \
        (PAGE_SLICE_LEAF_CAPACITY * PAGE_SLICE_INDEX_CAPACITY \
         * PAGE_SLICE_INDEX_CAPACITY)
#define PS_TEST_SLICE_INDEX3 ((PS_TEST_PGOFF_INDEX3 + 1ULL) * PAGE_SIZE)

#define PS_FUZZ_SHADOW_MAX         512u
#define PS_FUZZ_OPS_SMALL          4096u
#define PS_FUZZ_OPS_INDEX2         2048u
#define PS_FUZZ_OPS_INDEX3          512u
#define PS_FUZZ_INDEX3_MAX_MAPPED    32u
#define PS_FUZZ_VERIFY_INTERVAL     256u

#define PS_FUZZ_SMALL_SEED_COUNT    5u
#define PS_FUZZ_INDEX2_SEED_COUNT   3u
#define PS_FUZZ_INDEX3_SEED_COUNT   3u

static const u64 ps_fuzz_small_seeds[PS_FUZZ_SMALL_SEED_COUNT] = {
        0x5047465aULL, /* "PGFZ" */
        0x736d6631ULL, /* "smf1" */
        0x736d6632ULL,
        0xa5a5a5a5a5a5a5a5ULL,
        0x0123456789abcdefULL,
};

static const u64 ps_fuzz_index2_seeds[PS_FUZZ_INDEX2_SEED_COUNT] = {
        0x5047465a494e5832ULL, /* PGFZ^INX2 mix */
        0x696e7832615f7631ULL, /* "inx2a_v1" */
        0x696e7832615f7632ULL,
};

static const u64 ps_fuzz_index3_seeds[PS_FUZZ_INDEX3_SEED_COUNT] = {
        0x5047465a494e5833ULL,
        0x696e78335f737031ULL, /* sparse v1 */
        0x696e78335f737032ULL,
};

struct ps_fuzz_shadow {
        u64 pgoff;
        vaddr kva;
        bool mapped;
};

static struct page_slice* ps_test_slice;
static struct ps_fuzz_shadow ps_fuzz_shadow[PS_FUZZ_SHADOW_MAX];

static void ps_test_dump_slice(const char* step)
{
        struct page_slice* slice = ps_test_slice;

        if (!slice) {
                pr_error("[page_slice_test] %s: slice=NULL\n", step);
                return;
        }
        pr_error("[page_slice_test] %s: size=%llu pages=%llu mapped=%llu "
                 "height=%u root_empty=%d root=0x%llx live=%u\n",
                 step,
                 (u64)slice->size,
                 (u64)page_slice_page_count(slice),
                 (u64)slice->mapped_entries,
                 (unsigned)page_slice_stored_height(slice),
                 (int)page_slice_root_empty(slice),
                 (u64)slice->root,
                 (unsigned)page_slice_root_live(slice));
}

static void ps_test_dump_index_entry(const char* label, u64 slot,
                                     page_slice_index_entry_t entry)
{
        pr_error("[page_slice_test] %s slot=%llu raw=0x%llx ptr=0x%llx "
                 "h=%u live=%u tp_none=%d idx_empty=%d leaf=%d index=%d\n",
                 label,
                 (u64)slot,
                 (u64)entry,
                 (u64)page_slice_index_entry_get_ptr(entry),
                 (unsigned)page_slice_index_entry_height(entry),
                 (unsigned)page_slice_index_entry_live(entry),
                 (int)ps_entry_is_none(entry),
                 (int)page_slice_index_entry_empty(entry),
                 (int)page_slice_index_entry_points_to_leaf(entry),
                 (int)page_slice_index_entry_points_to_index(entry));
}

static void ps_test_dump_pgoff_ctx(struct page_slice* slice, u64 pgoff)
{
        page_slice_index_entry_t* index_page;
        u64 slot_l0;
        u8 stored_h;
        u8 need_h;

        if (!slice) {
                pr_error("[page_slice_test] pgoff ctx: slice=NULL pgoff=%llu\n",
                         (u64)pgoff);
                return;
        }

        slot_l0 = PAGE_SLICE_INDEX_ENTRY(pgoff, 0);
        stored_h = page_slice_stored_height(slice);
        need_h = pgoff < PAGE_SLICE_LEAF_CAPACITY ?
                         PS_HEIGHT_LEAF :
                         (PAGE_SLICE_INDEX_ENTRY(pgoff, 2) ?
                                  PS_HEIGHT_INDEX3 :
                                  (PAGE_SLICE_INDEX_ENTRY(pgoff, 1) ?
                                           PS_HEIGHT_INDEX2 :
                                           PS_HEIGHT_INDEX1));
        pr_error("[page_slice_test] pgoff=%llu need_h=%u stored_h=%u slot_l0=%llu "
                 "leaf_idx=%llu\n",
                 (u64)pgoff,
                 (unsigned)need_h,
                 (unsigned)stored_h,
                 (u64)slot_l0,
                 (u64)PAGE_SLICE_LEAF_IDX(pgoff));

        if (!page_slice_root_is_index(slice)) {
                pr_error("[page_slice_test] pgoff ctx: root is not index "
                         "(is_leaf=%d root_empty=%d)\n",
                         (int)page_slice_root_is_leaf(slice),
                         (int)page_slice_root_empty(slice));
                return;
        }

        index_page = page_slice_root_get_index(slice);
        if (!index_page) {
                pr_error("[page_slice_test] pgoff ctx: root index ptr NULL\n");
                return;
        }

        ps_test_dump_index_entry("pgoff target", slot_l0, index_page[slot_l0]);
        /* slot 0 is where ps_raise_height used to leave a tagged-null ghost */
        ps_test_dump_index_entry("root_index[0]", 0, index_page[0]);
        if (slot_l0 != 1)
                ps_test_dump_index_entry("root_index[1]", 1, index_page[1]);
}

static void ps_test_fail_at(const char* step)
{
        pr_error("[page_slice_test] FAIL step=%s\n", step);
        ps_test_dump_slice(step);
}

#define PS_TEST_CHECK(step, cond)                     \
        do {                                          \
                if (!(cond)) {                        \
                        ps_test_fail_at(step);        \
                        goto fail;                    \
                }                                     \
        } while (0)

static vaddr ps_test_kmalloc_page(struct allocator* alloc)
{
        void* page = alloc->m_alloc(alloc, PAGE_SIZE);

        if (!page)
                return 0;
        *(u64*)page = (u64)(vaddr)page;
        return (vaddr)page;
}

static int ps_test_expect_lookup(struct page_slice* slice, u64 pgoff, vaddr kva)
{
        struct page_slice_entry* entry = page_slice_lookup(slice, pgoff);

        if (!entry) {
                pr_error("[page_slice_test] lookup pgoff %llu -> NULL\n",
                         (u64)pgoff);
                return -E_REND_TEST;
        }
        if (entry->kernel_virtual_address != kva) {
                pr_error("[page_slice_test] lookup pgoff %llu kva 0x%llx "
                          "expected 0x%llx\n",
                         (u64)pgoff,
                         (u64)entry->kernel_virtual_address,
                         (u64)kva);
                return -E_REND_TEST;
        }
        return REND_SUCCESS;
}

static int ps_test_expect_no_lookup(struct page_slice* slice, u64 pgoff)
{
        if (page_slice_lookup(slice, pgoff)) {
                pr_error("[page_slice_test] lookup pgoff %llu should be NULL\n",
                         (u64)pgoff);
                return -E_REND_TEST;
        }
        return REND_SUCCESS;
}

static int ps_test_bind(struct page_slice* slice, u64 pgoff,
                        struct allocator* alloc, const char* step)
{
        vaddr kva = ps_test_kmalloc_page(alloc);
        error_t err;

        if (!kva) {
                pr_error("[page_slice_test] %s: kmalloc failed pgoff %llu\n",
                         step,
                         (u64)pgoff);
                return -E_REND_TEST;
        }
        err = page_slice_insert_page(slice, pgoff, kva, 0);
        if (err != REND_SUCCESS) {
                pr_error("[page_slice_test] %s: insert pgoff %llu err=%d "
                         "kva=0x%llx\n",
                         step,
                         (u64)pgoff,
                         (int)err,
                         (u64)kva);
                alloc->m_free(alloc, (void*)kva);
                return -E_REND_TEST;
        }
        return ps_test_expect_lookup(slice, pgoff, kva);
}

static int ps_test_unbind(struct page_slice* slice, u64 pgoff,
                          const char* step)
{
        error_t err = page_slice_remove_page(slice, pgoff);

        if (err != REND_SUCCESS) {
                pr_error("[page_slice_test] %s: remove pgoff %llu err=%d\n",
                         step,
                         (u64)pgoff,
                         (int)err);
                return -E_REND_TEST;
        }
        return ps_test_expect_no_lookup(slice, pgoff);
}

static int ps_test_root_empty(struct page_slice* slice, const char* step)
{
        if (!page_slice_root_empty(slice)) {
                pr_error("[page_slice_test] %s: root not empty\n", step);
                return -E_REND_TEST;
        }
        if (slice->mapped_entries != 0) {
                pr_error("[page_slice_test] %s: mapped_entries=%llu\n",
                         step,
                         (u64)slice->mapped_entries);
                return -E_REND_TEST;
        }
        return REND_SUCCESS;
}

static void ps_fuzz_shadow_clear(void)
{
        memset(ps_fuzz_shadow, 0, sizeof(ps_fuzz_shadow));
}

static u64 ps_fuzz_shadow_count(void)
{
        u64 count = 0;
        u32 i;

        for (i = 0; i < PS_FUZZ_SHADOW_MAX; i++) {
                if (ps_fuzz_shadow[i].mapped)
                        count++;
        }
        return count;
}

static void ps_test_dump_shadow_summary(const char* label)
{
        u32 i;
        u32 shown = 0;

        pr_error("[page_slice_test] %s shadow mapped=%llu\n",
                 label,
                 (u64)ps_fuzz_shadow_count());
        for (i = 0; i < PS_FUZZ_SHADOW_MAX; i++) {
                if (!ps_fuzz_shadow[i].mapped)
                        continue;
                pr_error("[page_slice_test] %s shadow[%u] pgoff=%llu kva=0x%llx\n",
                         label,
                         (unsigned)i,
                         (u64)ps_fuzz_shadow[i].pgoff,
                         (u64)ps_fuzz_shadow[i].kva);
                if (++shown >= 8) {
                        pr_error("[page_slice_test] %s shadow ... truncated\n",
                                 label);
                        break;
                }
        }
}

static bool ps_fuzz_pgoff_used(u64 pgoff, u32 skip_slot)
{
        u32 i;

        for (i = 0; i < PS_FUZZ_SHADOW_MAX; i++) {
                if (i == skip_slot)
                        continue;
                if (ps_fuzz_shadow[i].mapped
                    && ps_fuzz_shadow[i].pgoff == pgoff)
                        return true;
        }
        return false;
}

static int ps_fuzz_find_shadow_pgoff(u64 pgoff)
{
        u32 i;

        for (i = 0; i < PS_FUZZ_SHADOW_MAX; i++) {
                if (ps_fuzz_shadow[i].mapped
                    && ps_fuzz_shadow[i].pgoff == pgoff)
                        return (int)i;
        }
        return -1;
}

static const char* ps_fuzz_err_name(error_t err)
{
        switch (err) {
        case REND_SUCCESS:
                return "SUCCESS";
        case -E_RENDEZVOS:
                return "E_RENDEZVOS";
        case -E_IN_PARAM:
                return "E_IN_PARAM";
        case -E_REND_AGAIN:
                return "E_REND_AGAIN";
        case -E_REND_NOFOUND:
                return "E_REND_NOFOUND";
        case -E_REND_OVERFLOW:
                return "E_REND_OVERFLOW";
        case -E_REND_NO_MEM:
                return "E_REND_NO_MEM";
        default:
                return "unknown";
        }
}

/*
 * Pick a pgoff not tracked in shadow.  Random first; if the slice is nearly
 * full, fall back to a linear scan instead of returning a colliding pgoff.
 */
static bool ps_fuzz_pick_pgoff(u64* rng, u64 max_pages, u32 slot, u64* pgoff_out)
{
        static const u64 bounds[] = {
                0,
                1,
                127,
                128,
                129,
                PAGE_SLICE_LEAF_CAPACITY - 1,
                PAGE_SLICE_LEAF_CAPACITY,
                PAGE_SLICE_LEAF_CAPACITY + 1,
                PS_TEST_PGOFF_INDEX2 - 1,
                PS_TEST_PGOFF_INDEX2,
        };
        u32 try;
        u64 pgoff;
        u64 scan;

        if (!pgoff_out || max_pages == 0)
                return false;

        *rng = rand64(*rng);
        if (((*rng) >> 40) % 16 == 0) {
                *rng = rand64(*rng);
                pgoff = bounds[(*rng) % (sizeof(bounds) / sizeof(bounds[0]))];
                if (pgoff < max_pages && !ps_fuzz_pgoff_used(pgoff, slot)) {
                        *pgoff_out = pgoff;
                        return true;
                }
        }

        for (try = 0; try < 16; try++) {
                *rng = rand64(*rng);
                pgoff = (*rng) % max_pages;
                if (!ps_fuzz_pgoff_used(pgoff, slot)) {
                        *pgoff_out = pgoff;
                        return true;
                }
        }

        for (scan = 0; scan < max_pages; scan++) {
                if (!ps_fuzz_pgoff_used(scan, slot)) {
                        *pgoff_out = scan;
                        return true;
                }
        }
        return false;
}

static int ps_fuzz_verify_shadow(struct page_slice* slice, const char* tag)
{
        u32 i;
        u64 expect;

        expect = ps_fuzz_shadow_count();
        if (slice->mapped_entries != expect) {
                pr_error("[page_slice_test] %s: mapped_entries=%llu shadow=%llu\n",
                         tag,
                         (u64)slice->mapped_entries,
                         (u64)expect);
                return -E_REND_TEST;
        }

        for (i = 0; i < PS_FUZZ_SHADOW_MAX; i++) {
                if (!ps_fuzz_shadow[i].mapped)
                        continue;
                if (ps_test_expect_lookup(slice,
                                          ps_fuzz_shadow[i].pgoff,
                                          ps_fuzz_shadow[i].kva)
                    != REND_SUCCESS) {
                        pr_error("[page_slice_test] %s: shadow slot %u pgoff %llu\n",
                                 tag,
                                 (unsigned)i,
                                 (u64)ps_fuzz_shadow[i].pgoff);
                        return -E_REND_TEST;
                }
        }
        return REND_SUCCESS;
}

static int ps_fuzz_run(struct page_slice* slice, struct allocator* alloc,
                       u64 max_pages, u32 ops, u64 seed, u32 max_mapped,
                       const char* label)
{
        u64 rng = seed;
        u32 op_idx;
        error_t err;

        ps_fuzz_shadow_clear();
        for (op_idx = 0; op_idx < ops; op_idx++) {
                u32 slot;
                u32 kind;
                u64 pgoff;
                vaddr kva;

                rng = rand64(rng);
                slot = (u32)(rng % PS_FUZZ_SHADOW_MAX);
                rng = rand64(rng);
                kind = (u32)(rng % 10);

                if (kind <= 5) {
                        if (ps_fuzz_shadow[slot].mapped) {
                                if (ps_test_expect_lookup(
                                            slice,
                                            ps_fuzz_shadow[slot].pgoff,
                                            ps_fuzz_shadow[slot].kva)
                                    != REND_SUCCESS)
                                        goto fuzz_fail;
                        } else {
                                if (max_mapped > 0
                                    && ps_fuzz_shadow_count() >= max_mapped)
                                        continue;
                                if (!ps_fuzz_pick_pgoff(&rng,
                                                        max_pages,
                                                        slot,
                                                        &pgoff))
                                        continue;
                                kva = ps_test_kmalloc_page(alloc);
                                if (!kva) {
                                        pr_error("[page_slice_test] %s fuzz kmalloc "
                                                 "op=%u slot=%u pgoff=%llu\n",
                                                 label,
                                                 (unsigned)op_idx,
                                                 (unsigned)slot,
                                                 (u64)pgoff);
                                        goto fuzz_fail;
                                }
                                err = page_slice_insert_page(slice, pgoff, kva, 0);
                                if (err != REND_SUCCESS) {
                                        int owner = ps_fuzz_find_shadow_pgoff(pgoff);

                                        pr_error("[page_slice_test] %s fuzz insert "
                                                 "op=%u slot=%u pgoff=%llu err=%d "
                                                 "(%s) shadow_owner=%d\n",
                                                 label,
                                                 (unsigned)op_idx,
                                                 (unsigned)slot,
                                                 (u64)pgoff,
                                                 (int)err,
                                                 ps_fuzz_err_name(err),
                                                 owner);
                                        ps_test_dump_pgoff_ctx(slice, pgoff);
                                        ps_test_dump_shadow_summary(label);
                                        alloc->m_free(alloc, (void*)kva);
                                        goto fuzz_fail;
                                }
                                ps_fuzz_shadow[slot].pgoff = pgoff;
                                ps_fuzz_shadow[slot].kva = kva;
                                ps_fuzz_shadow[slot].mapped = true;
                        }
                } else if (kind <= 8) {
                        if (ps_fuzz_shadow[slot].mapped) {
                                pgoff = ps_fuzz_shadow[slot].pgoff;
                                err = page_slice_remove_page(slice, pgoff);
                                if (err != REND_SUCCESS) {
                                        pr_error("[page_slice_test] %s fuzz remove "
                                                 "op=%u slot=%u pgoff=%llu err=%d\n",
                                                 label,
                                                 (unsigned)op_idx,
                                                 (unsigned)slot,
                                                 (u64)pgoff,
                                                 (int)err);
                                        goto fuzz_fail;
                                }
                                ps_fuzz_shadow[slot].mapped = false;
                                ps_fuzz_shadow[slot].pgoff = 0;
                                ps_fuzz_shadow[slot].kva = 0;
                        }
                } else if (ps_fuzz_verify_shadow(slice, label) != REND_SUCCESS) {
                        goto fuzz_fail;
                }

                if ((op_idx + 1) % PS_FUZZ_VERIFY_INTERVAL == 0
                    && ps_fuzz_verify_shadow(slice, label) != REND_SUCCESS)
                        goto fuzz_fail;
        }

        if (ps_fuzz_verify_shadow(slice, label) != REND_SUCCESS)
                goto fuzz_fail;

        {
                u32 i;

                for (i = 0; i < PS_FUZZ_SHADOW_MAX; i++) {
                        if (!ps_fuzz_shadow[i].mapped)
                                continue;
                        err = page_slice_remove_page(slice,
                                                     ps_fuzz_shadow[i].pgoff);
                        if (err != REND_SUCCESS) {
                                pr_error("[page_slice_test] %s fuzz drain remove "
                                         "slot=%u pgoff=%llu err=%d\n",
                                         label,
                                         (unsigned)i,
                                         (u64)ps_fuzz_shadow[i].pgoff,
                                         (int)err);
                                goto fuzz_fail;
                        }
                        ps_fuzz_shadow[i].mapped = false;
                }
        }
        if (ps_test_root_empty(slice, label) != REND_SUCCESS)
                goto fuzz_fail;

        pr_info("[page_slice_test] %s fuzz passed ops=%u seed=0x%llx final_rng=0x%llx "
                "mapped=%llu\n",
                label,
                (unsigned)ops,
                (u64)seed,
                (u64)rng,
                (u64)ps_fuzz_shadow_count());
        return REND_SUCCESS;

fuzz_fail:
        pr_error("[page_slice_test] %s fuzz FAIL op=%u seed=0x%llx rng=0x%llx\n",
                 label,
                 (unsigned)op_idx,
                 (u64)seed,
                 (u64)rng);
        ps_test_dump_shadow_summary(label);
        ps_test_fail_at(label);
        return -E_REND_TEST;
}

static int ps_test_dual_branch(struct page_slice* slice,
                               struct allocator* alloc)
{
        vaddr kva_index2;

        PS_TEST_CHECK("dual bind 0",
                      ps_test_bind(slice, 0, alloc, "dual bind 0") == 0);
        PS_TEST_CHECK("dual bind index2",
                      ps_test_bind(slice,
                                   PS_TEST_PGOFF_INDEX2,
                                   alloc,
                                   "dual bind index2")
                              == 0);
        PS_TEST_CHECK("dual height index2",
                      page_slice_stored_height(slice) >= PS_HEIGHT_INDEX2);

        kva_index2 = page_slice_lookup(slice, PS_TEST_PGOFF_INDEX2)
                             ->kernel_virtual_address;
        PS_TEST_CHECK("dual unbind 0",
                      ps_test_unbind(slice, 0, "dual unbind 0") == 0);
        PS_TEST_CHECK("dual lookup index2 after unbind 0",
                      ps_test_expect_lookup(slice, PS_TEST_PGOFF_INDEX2, kva_index2)
                              == 0);
        PS_TEST_CHECK("dual height after unbind 0",
                      page_slice_stored_height(slice) >= PS_HEIGHT_INDEX2);

        PS_TEST_CHECK("dual unbind index2",
                      ps_test_unbind(slice, PS_TEST_PGOFF_INDEX2,
                                     "dual unbind index2")
                              == 0);
        PS_TEST_CHECK("dual root empty",
                      ps_test_root_empty(slice, "dual root empty") == 0);
        return REND_SUCCESS;

fail:
        return -E_REND_TEST;
}

static int ps_test_set_size_shrink_index2(struct page_slice** slice,
                                          struct allocator* alloc)
{
        vaddr kva0;

        PS_TEST_CHECK("shrink2 bind 0",
                      ps_test_bind(*slice, 0, alloc, "shrink2 bind 0") == 0);
        PS_TEST_CHECK("shrink2 bind index2-1",
                      ps_test_bind(*slice,
                                   PS_TEST_PGOFF_INDEX2 - 1,
                                   alloc,
                                   "shrink2 bind index2-1")
                              == 0);
        PS_TEST_CHECK("shrink2 bind index2",
                      ps_test_bind(*slice,
                                   PS_TEST_PGOFF_INDEX2,
                                   alloc,
                                   "shrink2 bind index2")
                              == 0);

        kva0 = page_slice_lookup(*slice, 0)->kernel_virtual_address;

        PS_TEST_CHECK("shrink2 set_size 128",
                      page_slice_set_size(slice, PAGE_SIZE * 128)
                              == REND_SUCCESS);
        ps_test_slice = *slice;
        PS_TEST_CHECK("shrink2 lookup 0",
                      ps_test_expect_lookup(*slice, 0, kva0) == 0);
        PS_TEST_CHECK("shrink2 index2 gone",
                      ps_test_expect_no_lookup(*slice, PS_TEST_PGOFF_INDEX2)
                              == 0);
        PS_TEST_CHECK("shrink2 index2-1 gone",
                      ps_test_expect_no_lookup(*slice, PS_TEST_PGOFF_INDEX2 - 1)
                              == 0);
        PS_TEST_CHECK("shrink2 unbind 0",
                      ps_test_unbind(*slice, 0, "shrink2 unbind 0") == 0);
        PS_TEST_CHECK("shrink2 root empty",
                      ps_test_root_empty(*slice, "shrink2 root empty") == 0);
        return REND_SUCCESS;

fail:
        return -E_REND_TEST;
}

static int ps_test_index3_triple_branch(struct page_slice* slice,
                                        struct allocator* alloc)
{
        vaddr kva_index2;
        vaddr kva_index3;

        PS_TEST_CHECK("index3 triple bind 0",
                      ps_test_bind(slice, 0, alloc, "index3 triple bind 0") == 0);
        PS_TEST_CHECK("index3 triple bind index2",
                      ps_test_bind(slice,
                                   PS_TEST_PGOFF_INDEX2,
                                   alloc,
                                   "index3 triple bind index2")
                              == 0);
        PS_TEST_CHECK("index3 triple bind index3",
                      ps_test_bind(slice,
                                   PS_TEST_PGOFF_INDEX3,
                                   alloc,
                                   "index3 triple bind index3")
                              == 0);
        PS_TEST_CHECK("index3 triple height",
                      page_slice_stored_height(slice) == PS_HEIGHT_INDEX3);

        kva_index2 = page_slice_lookup(slice, PS_TEST_PGOFF_INDEX2)
                             ->kernel_virtual_address;
        kva_index3 = page_slice_lookup(slice, PS_TEST_PGOFF_INDEX3)
                             ->kernel_virtual_address;

        PS_TEST_CHECK("index3 triple unbind 0",
                      ps_test_unbind(slice, 0, "index3 triple unbind 0") == 0);
        PS_TEST_CHECK("index3 triple lookup index2",
                      ps_test_expect_lookup(slice, PS_TEST_PGOFF_INDEX2, kva_index2)
                              == 0);
        PS_TEST_CHECK("index3 triple lookup index3",
                      ps_test_expect_lookup(slice, PS_TEST_PGOFF_INDEX3, kva_index3)
                              == 0);
        PS_TEST_CHECK("index3 triple unbind index2",
                      ps_test_unbind(slice,
                                     PS_TEST_PGOFF_INDEX2,
                                     "index3 triple unbind index2")
                              == 0);
        PS_TEST_CHECK("index3 triple unbind index3",
                      ps_test_unbind(slice,
                                     PS_TEST_PGOFF_INDEX3,
                                     "index3 triple unbind index3")
                              == 0);
        PS_TEST_CHECK("index3 triple root empty",
                      ps_test_root_empty(slice, "index3 triple root empty")
                              == 0);
        return REND_SUCCESS;

fail:
        return -E_REND_TEST;
}

int page_slice_test(void)
{
        struct allocator* alloc = percpu(kallocator);
        struct page_slice* slice = NULL;
        vaddr pin_page = 0;
        u64 pgoff;
        error_t err;

        ps_test_slice = NULL;
        if (!alloc)
                return -E_REND_TEST;

        slice = page_slice_create(0, PAGE_SIZE * 256);
        ps_test_slice = slice;
        PS_TEST_CHECK("create", slice != NULL);

        PS_TEST_CHECK("bind pgoff 0",
                      ps_test_bind(slice, 0, alloc, "bind pgoff 0") == 0);
        PS_TEST_CHECK("get_size", page_slice_get_size(slice) == PAGE_SIZE * 256);
        PS_TEST_CHECK("mapped 1", slice->mapped_entries == 1);
        PS_TEST_CHECK("height leaf after pgoff 0",
                      page_slice_stored_height(slice) == PS_HEIGHT_LEAF);

        {
                struct page_slice_entry* e0 = page_slice_lookup(slice, 0);
                vaddr kva0;

                PS_TEST_CHECK("lookup pgoff 0 dup", e0 != NULL);
                kva0 = e0->kernel_virtual_address;
                err = page_slice_insert_page(slice, 0, kva0, 0);
                PS_TEST_CHECK("reinsert same kva", err == REND_SUCCESS);
                err = page_slice_insert_page(slice, 0, kva0 + PAGE_SIZE, 0);
                PS_TEST_CHECK("reinsert diff kva", err == -E_REND_AGAIN);
        }

        PS_TEST_CHECK("bind pgoff 127",
                      ps_test_bind(slice, 127, alloc, "bind pgoff 127") == 0);
        PS_TEST_CHECK("height leaf with 127",
                      page_slice_stored_height(slice) == PS_HEIGHT_LEAF);

        pgoff = PAGE_SLICE_LEAF_CAPACITY;
        PS_TEST_CHECK("bind pgoff 128",
                      ps_test_bind(slice, pgoff, alloc, "bind pgoff 128") == 0);
        PS_TEST_CHECK("height index1",
                      page_slice_stored_height(slice) >= PS_HEIGHT_INDEX1);

        pgoff = PAGE_SLICE_LEAF_CAPACITY + 1;
        PS_TEST_CHECK("bind pgoff 129",
                      ps_test_bind(slice, pgoff, alloc, "bind pgoff 129") == 0);

        err = page_slice_set_size(&slice, PS_TEST_SLICE_INDEX2);
        ps_test_slice = slice;
        PS_TEST_CHECK("set_size index2", err == REND_SUCCESS && slice != NULL);

        PS_TEST_CHECK("bind pgoff index2",
                      ps_test_bind(slice,
                                   PS_TEST_PGOFF_INDEX2,
                                   alloc,
                                   "bind pgoff index2")
                              == 0);
        PS_TEST_CHECK("height index2",
                      page_slice_stored_height(slice) >= PS_HEIGHT_INDEX2);

        PS_TEST_CHECK("unbind index2",
                      ps_test_unbind(slice, PS_TEST_PGOFF_INDEX2,
                                     "unbind index2")
                              == 0);
        PS_TEST_CHECK("unbind 129",
                      ps_test_unbind(slice, PAGE_SLICE_LEAF_CAPACITY + 1,
                                     "unbind 129")
                              == 0);
        PS_TEST_CHECK("unbind 128",
                      ps_test_unbind(slice, PAGE_SLICE_LEAF_CAPACITY,
                                     "unbind 128")
                              == 0);
        PS_TEST_CHECK("unbind 127",
                      ps_test_unbind(slice, 127, "unbind 127") == 0);
        PS_TEST_CHECK("unbind 0",
                      ps_test_unbind(slice, 0, "unbind 0") == 0);
        PS_TEST_CHECK("root empty after unbind all",
                      ps_test_root_empty(slice, "root empty after unbind all")
                              == 0);

        PS_TEST_CHECK("bind pgoff 40",
                      ps_test_bind(slice, 40, alloc, "bind pgoff 40") == 0);
        err = page_slice_set_size(&slice, PAGE_SIZE * 40);
        ps_test_slice = slice;
        PS_TEST_CHECK("set_size shrink 40", err == REND_SUCCESS && slice != NULL);
        PS_TEST_CHECK("get_size 40", page_slice_get_size(slice) == PAGE_SIZE * 40);
        PS_TEST_CHECK("lookup 40 gone",
                      ps_test_expect_no_lookup(slice, 40) == 0);
        PS_TEST_CHECK("root empty after shrink",
                      ps_test_root_empty(slice, "root empty after shrink") == 0);

        err = page_slice_set_size(&slice, 0);
        ps_test_slice = slice;
        PS_TEST_CHECK("destroy via set_size 0", err == REND_SUCCESS && slice == NULL);

        slice = page_slice_create(0, PS_TEST_SLICE_INDEX2);
        ps_test_slice = slice;
        PS_TEST_CHECK("dual branch create", slice != NULL);
        PS_TEST_CHECK("dual branch",
                      ps_test_dual_branch(slice, alloc) == REND_SUCCESS);
        err = page_slice_destroy(&slice);
        ps_test_slice = slice;
        PS_TEST_CHECK("dual branch destroy", err == REND_SUCCESS && slice == NULL);

        slice = page_slice_create(0, PS_TEST_SLICE_INDEX2);
        ps_test_slice = slice;
        PS_TEST_CHECK("set_size shrink2 create", slice != NULL);
        PS_TEST_CHECK("set_size shrink index2",
                      ps_test_set_size_shrink_index2(&slice, alloc)
                              == REND_SUCCESS);
        err = page_slice_destroy(&slice);
        ps_test_slice = slice;
        PS_TEST_CHECK("set_size shrink2 destroy", err == REND_SUCCESS && slice == NULL);

        slice = page_slice_create(0, PAGE_SIZE * 256);
        ps_test_slice = slice;
        PS_TEST_CHECK("fuzz small create", slice != NULL);
        PS_TEST_CHECK("index1 slot0 after high pgoff",
                      ps_test_bind(slice, 166, alloc, "bind pgoff 166") == 0);
        PS_TEST_CHECK("index1 slot0 low pgoff",
                      ps_test_bind(slice, 95, alloc, "bind pgoff 95") == 0);
        PS_TEST_CHECK("unbind pgoff 95",
                      ps_test_unbind(slice, 95, "unbind pgoff 95") == 0);
        PS_TEST_CHECK("unbind pgoff 166",
                      ps_test_unbind(slice, 166, "unbind pgoff 166") == 0);
        PS_TEST_CHECK("index1 slot0 root empty",
                      ps_test_root_empty(slice, "index1 slot0 root empty") == 0);
        {
                u32 seed_i;

                for (seed_i = 0; seed_i < PS_FUZZ_SMALL_SEED_COUNT; seed_i++) {
                        PS_TEST_CHECK("fuzz small multi-seed",
                                      ps_fuzz_run(slice,
                                                  alloc,
                                                  256,
                                                  PS_FUZZ_OPS_SMALL,
                                                  ps_fuzz_small_seeds[seed_i],
                                                  0,
                                                  "fuzz_small")
                                              == REND_SUCCESS);
                }
        }
        err = page_slice_destroy(&slice);
        ps_test_slice = slice;
        PS_TEST_CHECK("fuzz small destroy", err == REND_SUCCESS && slice == NULL);

        slice = page_slice_create(0, PS_TEST_SLICE_INDEX2);
        ps_test_slice = slice;
        PS_TEST_CHECK("fuzz index2 create", slice != NULL);
        {
                u32 seed_i;

                for (seed_i = 0; seed_i < PS_FUZZ_INDEX2_SEED_COUNT; seed_i++) {
                        PS_TEST_CHECK("fuzz index2 multi-seed",
                                      ps_fuzz_run(slice,
                                                  alloc,
                                                  page_slice_page_count(slice),
                                                  PS_FUZZ_OPS_INDEX2,
                                                  ps_fuzz_index2_seeds[seed_i],
                                                  0,
                                                  "fuzz_index2")
                                              == REND_SUCCESS);
                }
        }
        err = page_slice_destroy(&slice);
        ps_test_slice = slice;
        PS_TEST_CHECK("fuzz index2 destroy", err == REND_SUCCESS && slice == NULL);

        slice = page_slice_create(0, PS_TEST_SLICE_INDEX3);
        ps_test_slice = slice;
        PS_TEST_CHECK("create index3", slice != NULL);
        PS_TEST_CHECK("index3 triple branch",
                      ps_test_index3_triple_branch(slice, alloc) == REND_SUCCESS);
        {
                u32 seed_i;

                for (seed_i = 0; seed_i < PS_FUZZ_INDEX3_SEED_COUNT; seed_i++) {
                        PS_TEST_CHECK("fuzz index3 sparse multi-seed",
                                      ps_fuzz_run(slice,
                                                  alloc,
                                                  page_slice_page_count(slice),
                                                  PS_FUZZ_OPS_INDEX3,
                                                  ps_fuzz_index3_seeds[seed_i],
                                                  PS_FUZZ_INDEX3_MAX_MAPPED,
                                                  "fuzz_index3")
                                              == REND_SUCCESS);
                }
        }

        pin_page = ps_test_kmalloc_page(alloc);
        PS_TEST_CHECK("pin alloc", pin_page != 0);
        err = page_slice_insert_page(slice, 0, pin_page, PAGE_SLICE_FLAG_PIN);
        PS_TEST_CHECK("pin insert", err == REND_SUCCESS);
        PS_TEST_CHECK("pin unbind",
                      ps_test_unbind(slice, 0, "pin unbind") == 0);
        alloc->m_free(alloc, (void*)pin_page);
        pin_page = 0;

        err = page_slice_destroy(&slice);
        ps_test_slice = slice;
        PS_TEST_CHECK("destroy", err == REND_SUCCESS && slice == NULL);

        pr_info("[page_slice_test] all steps passed\n");
        return REND_SUCCESS;

fail:
        page_slice_destroy(&slice);
        ps_test_slice = slice;
        if (pin_page)
                alloc->m_free(alloc, (void*)pin_page);
        return -E_REND_TEST;
}
