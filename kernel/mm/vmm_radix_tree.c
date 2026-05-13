
#include <rendezvos/mm/vmm_radix_tree.h>
#include <common/bit.h>
#include <common/align.h>
#include <common/string.h>
#include <rendezvos/sync/cas_lock.h>
#include <modules/log/log.h>

#define RADIX_ENTRY_PAGES    1
#define RADIX_ENTRY_SIZE     (RADIX_ENTRY_PAGES * PAGE_SIZE)
#define RADIX_NODE_PAGES     4
#define RADIX_NODE_SIZE      (RADIX_NODE_PAGES * PAGE_SIZE)
#define RADIX_COUNT_PER_PAGE 512

#define RADIX_TREE_LEVEL0 0
#define RADIX_TREE_LEVEL1 1
#define RADIX_TREE_LEVEL2 2
#define RADIX_TREE_LEVEL3 3

#define RADIX_TREE_DIRECTION_INC 1
#define RADIX_TREE_DIRECTION_DEC -1

#define RADIX_KERNEL_MAP_FLAGS                                  \
        (PAGE_ENTRY_GLOBAL | PAGE_ENTRY_READ | PAGE_ENTRY_VALID \
         | PAGE_ENTRY_WRITE)

/* === module base1: some basic helper funcs === */
static const u64 radix_level_step[4] = {HUGE_PAGE_SIZE,
                                        GIGAN_PAGE_SIZE,
                                        MIDDLE_PAGE_SIZE,
                                        PAGE_SIZE};

static inline u64 get_gran_from_level(int level)
{
        if (level < 0 || level > 3)
                return 0;
        return radix_level_step[level];
}

/*radix tree entry*/
static inline bool radix_check_range(vaddr start, vaddr end, int level)
{
        u64 align;
        align = get_gran_from_level(level);
        if (align == 0)
                return false;

        if (!ALIGNED(start, align) || end <= start)
                return false;
        return true;
}

static inline vaddr entry_child(const Radix_entry_t* entry)
{
        return (vaddr)(entry->value & VMM_RADIX_PTR_MASK);
}

static inline bool entry_valid(const Radix_entry_t* entry)
{
        return (entry->value & VMM_RADIX_ENTRY_VALID_MASK) != 0;
}

static inline u64 entry_get_count(u64 entry_bits)
{
        if (entry_bits & VMM_RADIX_ENTRY_HUGE_MASK)
                return RADIX_COUNT_PER_PAGE;
        return (entry_bits & VMM_RADIX_CNT_MASK) >> VMM_RADIX_CNT_SHIFT;
}

static inline u64 entry_set_count(u64 count, u64 entry_bits)
{
        entry_bits = clear_mask_u64(
                entry_bits, VMM_RADIX_CNT_MASK | VMM_RADIX_ENTRY_HUGE_MASK);
        count = MIN(count, RADIX_COUNT_PER_PAGE);
        return entry_bits
               | (((count) << VMM_RADIX_CNT_SHIFT) & VMM_RADIX_CNT_MASK);
}

static void radix_entry_lock(Radix_entry_t* entry)
{
        while (1) {
                u64 loaded = atomic64_load((volatile u64*)&entry->value);
                if (loaded & VMM_RADIX_ENTRY_LOCK_MASK) {
                        arch_cpu_relax();
                        continue;
                }
                u64 desired = loaded | VMM_RADIX_ENTRY_LOCK_MASK;
                if (atomic64_cas((volatile u64*)&entry->value, loaded, desired)
                    == loaded)
                        return;
        }
}
static void radix_entry_unlock(Radix_entry_t* entry)
{
        while (1) {
                u64 loaded = atomic64_load((volatile u64*)&entry->value);
                u64 desired = loaded & ~VMM_RADIX_ENTRY_LOCK_MASK;
                if (atomic64_cas((volatile u64*)&entry->value, loaded, desired)
                    == loaded)
                        return;
        }
}
enum RADIX_ENTRY_UPDATE_FLAGS {
        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK = (1U << 0),
        RADIX_ENTRY_UPDATE_FLAG_VALID_SET = (1U << 1),
        RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR = (1U << 2),
        RADIX_ENTRY_UPDATE_FLAG_COUNT_DELTA = (1U << 3),
        RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET = (1U << 4),
        RADIX_ENTRY_UPDATE_FLAG_UPDATE_PTR = (1U << 5),
        RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR = (1U << 6)
};

static error_t radix_entry_update(Radix_entry_t* entry, u64 update_flags,
                                  vaddr update_page_addr, int count_delta)
{
        u64 value = entry->value;
        if ((update_flags & RADIX_ENTRY_UPDATE_FLAG_VALID_SET)
            && (update_flags & RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR)) {
                return -E_IN_PARAM;
        }
        if ((update_flags & RADIX_ENTRY_UPDATE_FLAG_UPDATE_PTR)
            && update_page_addr == 0) {
                return -E_IN_PARAM;
        }
        if ((update_flags & RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR)
            && update_page_addr != 0) {
                return -E_IN_PARAM;
        }

        if ((update_flags & RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR)
            && (update_flags & RADIX_ENTRY_UPDATE_FLAG_UPDATE_PTR)) {
                return -E_IN_PARAM;
        }
        if (update_flags & RADIX_ENTRY_UPDATE_FLAG_UPDATE_PTR) {
                value = clear_mask_u64(value, VMM_RADIX_PTR_MASK);
                value |= (update_page_addr & VMM_RADIX_PTR_MASK);
        }
        if (update_flags & RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR) {
                value = clear_mask_u64(value, VMM_RADIX_PTR_MASK);
                /*if clear ptr , the count must be set to 0*/
                update_flags = clear_mask_u64(
                        update_flags, RADIX_ENTRY_UPDATE_FLAG_COUNT_DELTA);
                update_flags |= RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET;
        }
        if (update_flags & RADIX_ENTRY_UPDATE_FLAG_VALID_SET) {
                value |= VMM_RADIX_ENTRY_VALID_MASK;
        } else if (update_flags & RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR) {
                value = clear_mask_u64(value, VMM_RADIX_ENTRY_VALID_MASK);
        }

        if (update_flags & RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET) {
                value = entry_set_count(0, value);
        } else if (update_flags & RADIX_ENTRY_UPDATE_FLAG_COUNT_DELTA) {
                u64 get_count = entry_get_count(value);
                if (count_delta > 0) {
                        get_count += count_delta;
                        if (count_delta > RADIX_COUNT_PER_PAGE)
                                return -E_REND_OVERFLOW;
                } else if (count_delta < 0) {
                        if ((u64)(-count_delta) > get_count) {
                                return -E_REND_OVERFLOW;
                        }
                        get_count -= (u64)(-count_delta);
                }
                value = entry_set_count(get_count, value);
        }

        if (update_flags & RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK) {
                value = clear_mask_u64(value, VMM_RADIX_ENTRY_LOCK_MASK);
                value = set_mask_u64(value, VMM_RADIX_ENTRY_LOCK_MASK);
        }

        entry->value = value;
        return REND_SUCCESS;
}
static inline void radix_node_clear(Radix_node_t* node)
{
        node->flags = 0;
        node->owner = tp_new_none();
        INIT_LIST_HEAD(&node->rmap_list);
}

/*radix tree node*/

/* === module base2: the level walk structure and algorithms === */
/* walk base*/

static inline Radix_entry_t* radix_l0_entry(Radix_entry_t* root, vaddr va)
{
        return &root[L0_INDEX(va)];
}

static inline Radix_entry_t* radix_l1_entry(Radix_entry_t* l0_entry, vaddr va)
{
        Radix_entry_t* l1_table = (Radix_entry_t*)entry_child(l0_entry);
        if (!l1_table)
                return NULL;
        return &l1_table[L1_INDEX(va)];
}

static inline Radix_entry_t* radix_l2_entry(Radix_entry_t* l1_entry, vaddr va)
{
        Radix_entry_t* l2_table = (Radix_entry_t*)entry_child(l1_entry);
        if (!l2_table)
                return NULL;
        return &l2_table[L2_INDEX(va)];
}

static inline Radix_node_t* radix_l3_node(Radix_entry_t* l2_entry, vaddr va)
{
        Radix_node_t* l3_node = (Radix_node_t*)entry_child(l2_entry);
        if (!l3_node)
                return NULL;
        return &l3_node[L3_INDEX(va)];
}
static bool radix_l3_overlap_insert(const Radix_node_t* node)
{
        if (node->flags & (PAGE_ENTRY_VALID | PAGE_ENTRY_LAZY))
                return true;
        if (!tp_is_none(node->owner))
                return true;
        return false;
}
static bool radix_l3_undeletable(const Radix_node_t* node)
{
        return (node->flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID) != 0;
}

/*new table's free and alloc logic*/
static error_t free_radix_table(VSpace* vs, struct pmm* pmm,
                                struct map_handler* handler,
                                vaddr page_vaddr_start, vaddr page_vaddr_end,
                                size_t free_page_number)
{
        error_t res = REND_SUCCESS;
        error_t tmpres;
        for (vaddr page_vaddr_back_iter = page_vaddr_start;
             page_vaddr_back_iter < page_vaddr_end;
             page_vaddr_back_iter += PAGE_SIZE) {
                ppn_t ppn = unmap(vs, VPN(page_vaddr_back_iter), 0, handler);
                if (invalid_ppn(ppn)) {
                        res = -E_RENDEZVOS;
                }
        }
        tmpres = pmm->pmm_free(pmm,
                               PPN(KERNEL_VIRT_TO_PHY(page_vaddr_start)),
                               free_page_number);
        if (tmpres != REND_SUCCESS)
                res = tmpres;
        return res;
}
static error_t radix_alloc_level_table(VSpace* vs, struct pmm* pmm,
                                       struct map_handler* handler,
                                       vaddr* out_page_vaddr, int level)
{
        if (!vs || !pmm || !handler) {
                return -E_IN_PARAM;
        }

        size_t need_alloc_pages = 0, alloced_pages;
        if (level == RADIX_TREE_LEVEL0 || level == RADIX_TREE_LEVEL1
            || level == RADIX_TREE_LEVEL2) {
                need_alloc_pages = 1;
        } else if (level == RADIX_TREE_LEVEL3) {
                need_alloc_pages = 4;
        } else {
                return -E_IN_PARAM;
        }

        ppn_t ppn = pmm->pmm_alloc(pmm, need_alloc_pages, &alloced_pages);
        if (invalid_ppn(ppn) || alloced_pages != need_alloc_pages) {
                return -E_RENDEZVOS;
        }

        vaddr page_vaddr_start = KERNEL_PHY_TO_VIRT(PADDR(ppn));
        vaddr page_vaddr_end = page_vaddr_start + need_alloc_pages * PAGE_SIZE;
        vaddr page_vaddr_iter = page_vaddr_start;
        for (; page_vaddr_iter < page_vaddr_end; page_vaddr_iter += PAGE_SIZE) {
                if (map(vs,
                        PPN(KERNEL_VIRT_TO_PHY(page_vaddr_iter)),
                        VPN(page_vaddr_iter),
                        3,
                        (ENTRY_FLAGS_t)RADIX_KERNEL_MAP_FLAGS,
                        handler)
                    != REND_SUCCESS) {
                        goto map_fail;
                }
        }

        if (level == RADIX_TREE_LEVEL3) {
                memset((void*)page_vaddr_start, 0, RADIX_NODE_SIZE);
                for (int i = 0; i < RADIX_COUNT_PER_PAGE; i++) {
                        INIT_LIST_HEAD(&(
                                ((Radix_node_t*)page_vaddr_start)[i].rmap_list));
                }
        } else {
                memset((void*)page_vaddr_start, 0, RADIX_ENTRY_SIZE);
        }
        *out_page_vaddr = page_vaddr_start;
        return REND_SUCCESS;
map_fail:
        /* it might fail, but fail path have another fail should not report, it
         * must be a serious problem,for example,OOM without any handler*/
        (void)free_radix_table(vs,
                               pmm,
                               handler,
                               page_vaddr_start,
                               page_vaddr_iter,
                               need_alloc_pages);
        return -E_RENDEZVOS;
}
static error_t free_level_table(VSpace* vs, struct pmm* pmm,
                                struct map_handler* handler, vaddr page_vaddr,
                                int level)
{
        if (!vs || !pmm || !handler) {
                return -E_IN_PARAM;
        }
        size_t need_alloc_pages = 0;
        if (level == RADIX_TREE_LEVEL0 || level == RADIX_TREE_LEVEL1
            || level == RADIX_TREE_LEVEL2) {
                need_alloc_pages = 1;
        } else if (level == RADIX_TREE_LEVEL3) {
                need_alloc_pages = 4;
        } else {
                return -E_IN_PARAM;
        }
        return free_radix_table(vs,
                                pmm,
                                handler,
                                page_vaddr,
                                page_vaddr + need_alloc_pages * PAGE_SIZE,
                                need_alloc_pages);
}

/*walk radix tree*/
typedef struct {
        /*static part*/
        Radix_entry_t* root;
        /*range is [range_start_vaddr,range_end_vaddr), only range_start_vaddr
         * must be aligned*/
        vaddr range_start_vaddr;
        vaddr range_end_vaddr;
        int direction;
        int walk_level;
        /*dynamic part*/
        vaddr current_vaddr;
        Radix_entry_t* curr_l0_entry;
        Radix_entry_t* curr_l1_entry;
        Radix_entry_t* curr_l2_entry;
        Radix_node_t* curr_l3_node;
} radix_tree_level_walk_t;

static void radix_tree_level_walk_init(radix_tree_level_walk_t* walk_iter,
                                       Radix_entry_t* root,
                                       vaddr range_start_vaddr,
                                       vaddr range_end_vaddr, int level,
                                       int direction)
{
        if (!walk_iter || !root)
                return;
        if (direction != RADIX_TREE_DIRECTION_INC
            && direction != RADIX_TREE_DIRECTION_DEC)
                return;
        memset(walk_iter, 0, sizeof(radix_tree_level_walk_t));
        walk_iter->root = root;
        walk_iter->direction = direction;
        walk_iter->walk_level = level;
        walk_iter->range_start_vaddr = range_start_vaddr;
        walk_iter->range_end_vaddr = range_end_vaddr;
        /*start calculate*/
        /*calculate where to start*/
        vaddr start_addr;
        if (direction == RADIX_TREE_DIRECTION_INC) {
                start_addr = range_start_vaddr;
        } else {
                u64 step = get_gran_from_level(level);
                start_addr = ROUND_DOWN((range_end_vaddr - 1), step);
                if (!(start_addr >= range_start_vaddr
                      && start_addr < range_end_vaddr))
                        return;
        }

        walk_iter->current_vaddr = start_addr;

        /*calculate radix tree entry/node*/
        walk_iter->curr_l0_entry =
                radix_l0_entry(root, walk_iter->current_vaddr);

        if (level >= 1 && walk_iter->curr_l0_entry)
                walk_iter->curr_l1_entry = radix_l1_entry(
                        walk_iter->curr_l0_entry, walk_iter->current_vaddr);

        if (level >= 2 && walk_iter->curr_l1_entry)
                walk_iter->curr_l2_entry = radix_l2_entry(
                        walk_iter->curr_l1_entry, walk_iter->current_vaddr);

        if (level >= 3 && walk_iter->curr_l2_entry)
                walk_iter->curr_l3_node = radix_l3_node(
                        walk_iter->curr_l2_entry, walk_iter->current_vaddr);
}

static bool
radix_tree_level_walk_check(const radix_tree_level_walk_t* walk_iter)
{
        if (!walk_iter)
                return false;
        if (!walk_iter->root)
                return false;
        if (walk_iter->direction != RADIX_TREE_DIRECTION_INC
            && walk_iter->direction != RADIX_TREE_DIRECTION_DEC)
                return false;
        if (walk_iter->walk_level < 0 || walk_iter->walk_level > 3)
                return false;
        if (walk_iter->range_start_vaddr > walk_iter->range_end_vaddr)
                return false;
        if (walk_iter->current_vaddr < walk_iter->range_start_vaddr
            || walk_iter->current_vaddr >= walk_iter->range_end_vaddr)
                return false;
        return true;
}

/* for prev/next iter walk, no table will return NULL, but goto the bound also
 * will return NULL, the upper must check the current vaddr reach
 * range_start_vaddr/range_end_vaddr*/
static radix_tree_level_walk_t*
radix_tree_level_walk(radix_tree_level_walk_t* walk_iter)
{
        if (!walk_iter)
                return NULL;
        vaddr next_addr;
        Radix_entry_t* next_l0_entry = walk_iter->curr_l0_entry;
        Radix_entry_t* next_l1_entry = walk_iter->curr_l1_entry;
        Radix_entry_t* next_l2_entry = walk_iter->curr_l2_entry;
        Radix_node_t* next_l3_node = walk_iter->curr_l3_node;
        i64 walk_step = 0;
        bool is_next_2m_bound = false, is_next_1g_bound = false,
             is_next_512g_bound = false;
        /*calculate the walk step*/
        walk_step = (i64)get_gran_from_level(walk_iter->walk_level);
        if (walk_step == 0)
                return NULL;
        walk_step *= walk_iter->direction;
        /*calculate the next next addr and check, it might fail because of the
         * out of bound, or might overflow as corner case*/

        /*here remember, the i64 walk_step will change to the u64, and then add
         * with walk_iter->current_vaddr, which might overflow, and will be
         * checked by the following check*/
        next_addr = walk_iter->current_vaddr + walk_step;

        if (walk_iter->direction == RADIX_TREE_DIRECTION_INC) {
                if (next_addr >= walk_iter->range_end_vaddr)
                        return NULL;
                /*overflow case check*/
                if (next_addr < walk_iter->current_vaddr)
                        return NULL;
        } else if (walk_iter->direction == RADIX_TREE_DIRECTION_DEC) {
                if (next_addr < walk_iter->range_start_vaddr)
                        return NULL;
                /*overflow case check*/
                if (next_addr > walk_iter->current_vaddr)
                        return NULL;
        }

        /*calculate whether the next addr and curr addr is at different
         * 2m/1g/512g*/
        if (L0_INDEX(walk_iter->current_vaddr) != L0_INDEX(next_addr))
                is_next_512g_bound = true;

        /*update the next_l0/l1/l2_entry/curr_l3_node according to the is
         * 2m/1g/512g bound, it might fail because of the uncle table is not
         * exit.*/

        if (walk_iter->walk_level == 0 || is_next_512g_bound) {
                next_l0_entry = radix_l0_entry(walk_iter->root, next_addr);
                if (!next_l0_entry)
                        return NULL;
        }
        /*for level0 ,it's just an array, and no more calculate is need*/
        if (walk_iter->walk_level == 0)
                goto l0_bypass;
        /*this calculate was before the update the
         * next_l0/l1/l2_entry/curr_l3_node, but move to here for l0 bypass*/
        if (L1_INDEX(walk_iter->current_vaddr) != L1_INDEX(next_addr))
                is_next_1g_bound = true;

        if (walk_iter->walk_level == 1 || is_next_1g_bound
            || is_next_512g_bound) {
                Radix_entry_t* base_l0 =
                        (is_next_512g_bound || walk_iter->walk_level == 1) ?
                                next_l0_entry :
                                walk_iter->curr_l0_entry;
                if (!base_l0)
                        return NULL;
                next_l1_entry = radix_l1_entry(base_l0, next_addr);
                if (!next_l1_entry)
                        return NULL;
        }
        if (walk_iter->walk_level == 1)
                goto l1_bypass;

        /*this calculate was before the update the
         * next_l0/l1/l2_entry/curr_l3_node, but move to here for l0/l1 bypass*/
        if (L2_INDEX(walk_iter->current_vaddr) != L2_INDEX(next_addr))
                is_next_2m_bound = true;

        if (walk_iter->walk_level == 2 || is_next_2m_bound || is_next_1g_bound
            || is_next_512g_bound) {
                Radix_entry_t* base_l1 = (is_next_1g_bound || is_next_512g_bound
                                          || walk_iter->walk_level == 2) ?
                                                 next_l1_entry :
                                                 walk_iter->curr_l1_entry;
                if (!base_l1)
                        return NULL;
                next_l2_entry = radix_l2_entry(base_l1, next_addr);
                if (!next_l2_entry)
                        return NULL;
        }
        if (walk_iter->walk_level == 2)
                goto l2_bypass;

        if (!is_next_2m_bound && !is_next_1g_bound && !is_next_512g_bound
            && walk_iter->curr_l2_entry != NULL) {
                i64 delta_idx = (i64)L3_INDEX(next_addr)
                                - (i64)L3_INDEX(walk_iter->current_vaddr);
                next_l3_node = walk_iter->curr_l3_node + delta_idx;
        } else {
                Radix_entry_t* base_l2 = (is_next_2m_bound || is_next_1g_bound
                                          || is_next_512g_bound) ?
                                                 next_l2_entry :
                                                 walk_iter->curr_l2_entry;
                if (!base_l2)
                        return NULL;
                next_l3_node = radix_l3_node(base_l2, next_addr);
                if (!next_l3_node)
                        return NULL;
        }

        /*update the current current entry/node/addr, for all the check has
         * passed*/
        walk_iter->curr_l3_node = next_l3_node;
l2_bypass:
        walk_iter->curr_l2_entry = next_l2_entry;
l1_bypass:
        walk_iter->curr_l1_entry = next_l1_entry;
l0_bypass:
        walk_iter->current_vaddr = next_addr;
        walk_iter->curr_l0_entry = next_l0_entry;
        return walk_iter;
}
/*
 * First 4Ki leaf in [start,end) with radix_l3_overlap_insert.
 *
 * @pre Caller **must** already hold @ref vmm_radix_tree_lock_range_big on the
 * L0 shards covering this walk. Search-shaped helpers cannot use
 * @ref vmm_radix_tree_lock_range_small (that contract assumes a uniformly
 * present/absent band). Without big, L0 table stability is not guaranteed.
 *
 * L2 lines are updated under @c radix_entry_lock(l2e). Big lock alone does not
 * exclude concurrent L2 holders (see long comment in @c clone_vspace); this
 * helper locks the chosen L2 before reading @c l2e->value and L3 leaves, then
 * unlocks. **Do not delete or shorten the clone_vspace radix lock rationale
 * without maintainer review** — it documents the same L0 vs L2 window.
 *
 * @par Predicate
 * First qualifying L0/L1/L2 in VA order (VALID + child + non-zero Phase 5
 * count); first overlapping L3 under that locked L2 band.
 *
 * @note Planned full-interval enumeration (e.g. clone): same **big** + **per
 * 2Mi band** pattern (lock L2, scan that band's L3 slots, unlock); walk L2
 * bands in VA order rather than a standalone L3-only walker. Re-locking a
 * band touched earlier by find-first is expected and correct.
 *
 * @return First qualifying @c Radix_node_t*, or NULL. On success @p *out_va is
 *         set and L2 is unlocked; see @ref vmm_radix_tree_find_first_occupied_leaf
 *         in the header for big-lock / clone_vspace lifetime of the pointer.
 */
Radix_node_t* vmm_radix_tree_find_first_occupied_leaf(VSpace* vs, vaddr start,
                                                      vaddr end, int direction,
                                                      vaddr* out_va)
{
        if (!vs || !vs->root_radix || !out_va)
                return NULL;
        if (end <= start)
                return NULL;
        if (direction != RADIX_TREE_DIRECTION_INC
            && direction != RADIX_TREE_DIRECTION_DEC)
                return NULL;

        Radix_entry_t* root = (Radix_entry_t*)vs->root_radix;

        const vaddr l0_step = (vaddr)radix_level_step[RADIX_TREE_LEVEL0];
        const vaddr l1_step = (vaddr)radix_level_step[RADIX_TREE_LEVEL1];
        const vaddr l2_step = (vaddr)radix_level_step[RADIX_TREE_LEVEL2];
        const vaddr l3_step = (vaddr)radix_level_step[RADIX_TREE_LEVEL3];

        vaddr first_page = ROUND_DOWN(start, l3_step);
        vaddr last_page = ROUND_DOWN((end - 1), l3_step);
        if (last_page < first_page)
                return NULL;

        vaddr l0_start = ROUND_DOWN(first_page, l0_step);
        vaddr l0_end = ROUND_DOWN(last_page, l0_step);

        Radix_entry_t* l0e = NULL;
        vaddr huge_start = 0;
        vaddr huge_end = 0;
        vaddr l0_iter;

        for (l0_iter = (direction > 0 ? l0_start : l0_end);;
             l0_iter = (vaddr)((i64)l0_iter + direction * (i64)l0_step)) {
                if (direction > 0) {
                        if (l0_iter > l0_end)
                                return NULL;
                } else {
                        if (l0_iter < l0_start)
                                return NULL;
                }
                huge_start = MAX(first_page, l0_iter);
                huge_end = MIN(last_page, l0_iter + l0_step - l3_step);
                l0e = radix_l0_entry(root, l0_iter);
                if (l0e && entry_valid(l0e) && entry_child(l0e)
                    && entry_get_count(l0e->value) != 0)
                        break;
        }

        Radix_entry_t* l1e = NULL;
        vaddr giga_start = 0;
        vaddr giga_end = 0;
        vaddr l1_start = ROUND_DOWN(huge_start, l1_step);
        vaddr l1_end = ROUND_DOWN(huge_end, l1_step);
        vaddr l1_iter;

        for (l1_iter = (direction > 0 ? l1_start : l1_end);;
             l1_iter = (vaddr)((i64)l1_iter + direction * (i64)l1_step)) {
                if (direction > 0) {
                        if (l1_iter > l1_end)
                                return NULL;
                } else {
                        if (l1_iter < l1_start)
                                return NULL;
                }
                giga_start = MAX(huge_start, l1_iter);
                giga_end = MIN(huge_end, l1_iter + l1_step - l3_step);
                l1e = radix_l1_entry(l0e, l1_iter);
                if (l1e && entry_valid(l1e) && entry_child(l1e)
                    && entry_get_count(l1e->value) != 0)
                        break;
        }

        Radix_entry_t* l2e = NULL;
        vaddr mid_start = 0;
        vaddr mid_end = 0;
        vaddr l2_start = ROUND_DOWN(giga_start, l2_step);
        vaddr l2_end = ROUND_DOWN(giga_end, l2_step);
        vaddr l2_iter;

        for (l2_iter = (direction > 0 ? l2_start : l2_end);;
             l2_iter = (vaddr)((i64)l2_iter + direction * (i64)l2_step)) {
                if (direction > 0) {
                        if (l2_iter > l2_end)
                                return NULL;
                } else {
                        if (l2_iter < l2_start)
                                return NULL;
                }
                mid_start = MAX(giga_start, l2_iter);
                mid_end = MIN(giga_end, l2_iter + l2_step - l3_step);
                Radix_entry_t* cand = radix_l2_entry(l1e, l2_iter);
                if (!cand)
                        continue;
                radix_entry_lock(cand);
                if (entry_valid(cand) && entry_child(cand)
                    && entry_get_count(cand->value) != 0) {
                        l2e = cand;
                        break;
                }
                radix_entry_unlock(cand);
        }

        if (!l2e)
                return NULL;

        for (vaddr l3_iter = (direction > 0 ? mid_start : mid_end);;
             l3_iter = (vaddr)((i64)l3_iter + direction * (i64)l3_step)) {
                if (direction > 0) {
                        if (l3_iter > mid_end) {
                                radix_entry_unlock(l2e);
                                return NULL;
                        }
                } else {
                        if (l3_iter < mid_start) {
                                radix_entry_unlock(l2e);
                                return NULL;
                        }
                }
                Radix_node_t* leaf = radix_l3_node(l2e, l3_iter);
                if (leaf && radix_l3_overlap_insert(leaf)) {
                        *out_va = l3_iter;
                        radix_entry_unlock(l2e);
                        return leaf;
                }
        }
        radix_entry_unlock(l2e);
        return NULL;
}
bool vmm_radix_tree_find_first_occupied_interval(VSpace* vs, vaddr search_start,
                                                 vaddr search_end,
                                                 vaddr* interval_start_out,
                                                 vaddr* interval_end_out)
{
        if (!vs || !vs->root_radix || !interval_start_out || !interval_end_out)
                return false;
        if (search_end <= search_start)
                return false;

        vaddr first_vaddr;
        Radix_node_t* first_leaf = vmm_radix_tree_find_first_occupied_leaf(
                vs,
                search_start,
                search_end,
                RADIX_TREE_DIRECTION_INC,
                &first_vaddr);
        if (!first_leaf)
                return false;

        const ENTRY_FLAGS_t run_flags = first_leaf->flags;

        Radix_entry_t* root = (Radix_entry_t*)vs->root_radix;
        vaddr vaddr_iter = first_vaddr + PAGE_SIZE;

        if (vaddr_iter >= search_end)
                goto out;
        const vaddr l2_step = (vaddr)radix_level_step[RADIX_TREE_LEVEL2];
        vaddr l2_range_last = ROUND_DOWN(vaddr_iter, l2_step);

        radix_tree_level_walk_t l2_walk;
        radix_tree_level_walk_init(&l2_walk,
                                   root,
                                   l2_range_last,
                                   search_end,
                                   RADIX_TREE_LEVEL2,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l2_walk))
                goto out;
        do {
                Radix_entry_t* l2_entry = l2_walk.curr_l2_entry;
                vaddr l2_range_start =
                        ROUND_DOWN(l2_walk.current_vaddr, l2_step);
                vaddr l2_range_end = MIN(search_end, l2_range_start + l2_step);

                if (vaddr_iter >= l2_range_end || !l2_entry)
                        break;

                vaddr before_band = vaddr_iter;
                radix_entry_lock(l2_entry);
                for (vaddr l3_iter = vaddr_iter;
                     l3_iter < l2_range_end
                     && ROUND_DOWN(l3_iter, l2_step) == l2_range_start;
                     l3_iter += PAGE_SIZE) {
                        Radix_node_t* leaf = radix_l3_node(l2_entry, l3_iter);
                        if (!leaf || !radix_l3_overlap_insert(leaf)
                            || leaf->flags != run_flags)
                                break;
                        vaddr_iter = l3_iter + (vaddr)PAGE_SIZE;
                }
                radix_entry_unlock(l2_entry);

                if (vaddr_iter == before_band)
                        break;
        } while (radix_tree_level_walk(&l2_walk));
out:
        *interval_start_out = first_vaddr;
        *interval_end_out = vaddr_iter;
        return true;
}

static inline bool
radix_tree_walk_is_level_first(const radix_tree_level_walk_t* walk, int level)
{
        if (!walk)
                return false;
        u64 step = get_gran_from_level(level);
        if (step == 0)
                return false;
        vaddr first = ROUND_DOWN(walk->range_start_vaddr, step);
        vaddr cur = ROUND_DOWN(walk->current_vaddr, step);
        return cur == first;
}

static inline bool
radix_tree_walk_is_level_last(const radix_tree_level_walk_t* walk, int level)
{
        if (!walk)
                return false;
        u64 step = get_gran_from_level(level);
        if (step == 0)
                return false;
        vaddr last = ROUND_DOWN((walk->range_end_vaddr - 1), step);
        vaddr cur = ROUND_DOWN(walk->current_vaddr, step);
        return cur == last;
}

static inline bool
radix_tree_walk_is_level_single(const radix_tree_level_walk_t* walk_iter,
                                int level)
{
        if (!walk_iter)
                return false;
        u64 step = get_gran_from_level(level);
        if (step == 0)
                return false;
        vaddr first = ROUND_DOWN(walk_iter->range_start_vaddr, step);
        vaddr last = ROUND_DOWN((walk_iter->range_end_vaddr - 1), step);
        return first == last;
}

/*module base3: the range lock and unlock realize*/

static inline int radix_parent_count_delta(radix_lock_acquire_kind_t kind,
                                           const radix_tree_level_walk_t* walk,
                                           int partition_level, int head_delta,
                                           int tail_delta)
{
        if (radix_tree_walk_is_level_single(walk, partition_level)
            || radix_tree_walk_is_level_first(walk, partition_level)) {
                return head_delta;
        }
        if (radix_tree_walk_is_level_last(walk, partition_level)) {
                return tail_delta;
        }
        if (kind == RADIX_RL_INSERT) {
                return RADIX_COUNT_PER_PAGE;
        }
        return -(int)RADIX_COUNT_PER_PAGE;
}
static inline bool
radix_count_trans_to_parent(radix_lock_acquire_kind_t kind,
                            const radix_tree_level_walk_t* walk,
                            int partition_level, u64 old_count, int delta,
                            int* parent_head_delta, int* parent_tail_delta)
{
        bool single_bypass = false;
        bool insert_should_trans =
                (kind == RADIX_RL_INSERT && old_count == 0 && delta != 0);
        bool delete_should_trans = (kind == RADIX_RL_DELETE && old_count != 0
                                    && (i64)old_count + delta == 0);

        if (radix_tree_walk_is_level_single(walk, partition_level)) {
                if (insert_should_trans) {
                        *parent_head_delta += 1;
                } else if (delete_should_trans) {
                        *parent_head_delta -= 1;
                } else {
                        single_bypass = true;
                }
        } else if (radix_tree_walk_is_level_first(walk, partition_level)) {
                if (insert_should_trans) {
                        *parent_head_delta += 1;
                } else if (delete_should_trans) {
                        *parent_head_delta -= 1;
                }
        } else if (radix_tree_walk_is_level_last(walk, partition_level)) {
                if (insert_should_trans) {
                        *parent_tail_delta += 1;
                } else if (delete_should_trans) {
                        *parent_tail_delta -= 1;
                }
        }
        return single_bypass;
}
static void radix_lock_rollback_entry(VSpace* vs, struct pmm* pmm,
                                      struct map_handler* handler,
                                      radix_lock_acquire_kind_t kind,
                                      radix_tree_level_walk_t* walk)
{
        Radix_entry_t* entry;
        vaddr child_page_addr;

        switch (walk->walk_level) {
        case RADIX_TREE_LEVEL2:
                entry = walk->curr_l2_entry;
                if (kind != RADIX_RL_INSERT) {
                        radix_entry_unlock(entry);
                        return;
                }
                if (entry_get_count(entry->value) != 0) {
                        radix_entry_unlock(entry);
                        return;
                }
                child_page_addr = entry_child(entry);
                if (child_page_addr == 0) {
                        radix_entry_unlock(entry);
                        return;
                }
                break;

        case RADIX_TREE_LEVEL1:
                entry = walk->curr_l1_entry;
                if (entry_get_count(entry->value) != 0) {
                        return;
                }
                child_page_addr = entry_child(entry);
                if (child_page_addr == 0) {
                        return;
                }
                break;

        case RADIX_TREE_LEVEL0:
                entry = walk->curr_l0_entry;
                if (kind != RADIX_RL_INSERT) {
                        radix_entry_unlock(entry);
                        return;
                }
                if (entry_get_count(entry->value) != 0) {
                        radix_entry_unlock(entry);
                        return;
                }
                if (L0_INDEX(walk->current_vaddr) >= 256) {
                        radix_entry_unlock(entry);
                        return;
                }
                child_page_addr = entry_child(entry);
                if (child_page_addr == 0) {
                        radix_entry_unlock(entry);
                        return;
                }
                break;

        default:
                return;
        }
        /*if rollback fail, no ptr points to it , it might leakage, but we just
         * print it*/
        if (free_level_table(
                    vs, pmm, handler, child_page_addr, walk->walk_level + 1)
            != REND_SUCCESS) {
                pr_error("[ Error ] free level table fail\n");
        }
        entry->value = 0;
}

static error_t radix_lock_ensure_path(Radix_entry_t* entry,
                                      radix_tree_level_walk_t* walk_iter,
                                      VSpace* vs, struct map_handler* handler,
                                      struct pmm* pmm,
                                      radix_lock_acquire_kind_t kind, int level,
                                      u64 update_flags)
{
        error_t err;
        if (kind == RADIX_RL_INSERT) {
                if (level == RADIX_TREE_LEVEL0
                    && L0_INDEX(walk_iter->current_vaddr) >= 256)
                        return REND_SUCCESS;
                if (!entry_valid(entry) && entry_child(entry) == 0) {
                        vaddr next_table_vaddr;
                        err = radix_alloc_level_table(
                                vs, pmm, handler, &next_table_vaddr, level + 1);
                        if (err != REND_SUCCESS) {
                                if (level != RADIX_TREE_LEVEL1)
                                        radix_entry_unlock(entry);
                                return err;
                        }
                        radix_entry_update(
                                entry, update_flags, next_table_vaddr, 0);
                }
        } else {
                if (!entry_valid(entry)) {
                        err = -E_REND_NOFOUND;
                        if (level != RADIX_TREE_LEVEL1)
                                radix_entry_unlock(entry);
                        return err;
                }
        }
        return REND_SUCCESS;
}

static error_t radix_range_lock_acquire(VSpace* vs, struct map_handler* handler,
                                        Radix_entry_t* root, vaddr start,
                                        vaddr end,
                                        radix_lock_acquire_kind_t kind)
{
        error_t err = REND_SUCCESS;
        radix_tree_level_walk_t l0_walk_iter, l1_walk_iter, l2_walk_iter,
                l3_walk_iter;
        struct pmm* pmm = vs->pmm;
        u64 update_flags = RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                           | RADIX_ENTRY_UPDATE_FLAG_UPDATE_PTR
                           | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                           | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET;
        /*Phase 1, try hold L0 lock and alloc l1 table if need*/
        radix_tree_level_walk_init(&l0_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)HUGE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL0,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l0_walk_iter))
                return -E_IN_PARAM;
        do {
                Radix_entry_t* l0_entry = l0_walk_iter.curr_l0_entry;
                radix_entry_lock(l0_entry);
                err = radix_lock_ensure_path(l0_entry,
                                             &l0_walk_iter,
                                             vs,
                                             handler,
                                             pmm,
                                             kind,
                                             RADIX_TREE_LEVEL0,
                                             update_flags);
                if (err != REND_SUCCESS)
                        goto phase1_clean_prev;
        } while (radix_tree_level_walk(&l0_walk_iter));

        /*Phase 2, alloc l2 table if need, we do not lock the L1 nodes*/
        radix_tree_level_walk_init(&l1_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)GIGAN_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL1,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l1_walk_iter)) {
                l0_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
                goto phase1_clean_all;
        }
        do {
                Radix_entry_t* l1_entry = l1_walk_iter.curr_l1_entry;
                err = radix_lock_ensure_path(l1_entry,
                                             &l1_walk_iter,
                                             vs,
                                             handler,
                                             pmm,
                                             kind,
                                             RADIX_TREE_LEVEL1,
                                             update_flags);
                if (err != REND_SUCCESS)
                        goto phase2_clean_prev;
        } while (radix_tree_level_walk(&l1_walk_iter));

        /*Phase 3, try hold l2 lock and alloc l3 table if need*/
        radix_tree_level_walk_init(&l2_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)MIDDLE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL2,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l2_walk_iter))
                goto phase2_clean_all;
        do {
                Radix_entry_t* l2_entry = l2_walk_iter.curr_l2_entry;
                radix_entry_lock(l2_entry);
                err = radix_lock_ensure_path(l2_entry,
                                             &l2_walk_iter,
                                             vs,
                                             handler,
                                             pmm,
                                             kind,
                                             RADIX_TREE_LEVEL2,
                                             update_flags);
                if (err != REND_SUCCESS)
                        goto phase3_clean_prev;
        } while (radix_tree_level_walk(&l2_walk_iter));

        /*Phase 4, try see whether the l3 node is overlap or not exist*/
        radix_tree_level_walk_init(&l3_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk_iter))
                goto phase3_clean_all;
        do {
                Radix_node_t* l3_node = l3_walk_iter.curr_l3_node;
                if (kind == RADIX_RL_INSERT) {
                        if (radix_l3_overlap_insert(l3_node)) {
                                err = -E_REND_OVERFLOW;
                                goto phase3_clean_all;
                        }
                } else if (kind == RADIX_RL_DELETE) {
                        if (radix_l3_undeletable(l3_node)) {
                                err = -E_REND_NOFOUND;
                                goto phase3_clean_all;
                        }
                        /*we think we have use unbind first*/
                        radix_node_clear(l3_node);
                }
        } while (radix_tree_level_walk(&l3_walk_iter));

        /*Phase 5, update the valid and count for level 0/1/2, no fail possible
         * at this phase begin, so we can reuse the level 0/1/2 walk iters*/
        if (kind == RADIX_RL_QUERY_OR_CHANGE) {
                goto phase6;
        }

        int l1_head_delta = 0, l1_tail_delta = 0;
        int l0_head_delta = 0, l0_tail_delta = 0;
        bool skip_l1_phase = true;
        bool skip_l0_phase = true;

        radix_tree_level_walk_init(&l2_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)MIDDLE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL2,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l2_entry = l2_walk_iter.curr_l2_entry;
                u64 l2_old_count = entry_get_count(l2_entry->value);

                vaddr overlap_start = MAX(start, l2_walk_iter.current_vaddr);
                vaddr overlap_end = MIN(
                        end,
                        (l2_walk_iter.current_vaddr + (vaddr)MIDDLE_PAGE_SIZE));
                /*the walk promise that there must have an overlap range*/
                u64 pages_in_overlap =
                        (VPN(overlap_end - 1) - VPN(overlap_start) + 1);
                update_flags = RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                               | RADIX_ENTRY_UPDATE_FLAG_COUNT_DELTA;
                int delta;
                if (kind == RADIX_RL_INSERT) {
                        if (entry_child(l2_entry) != 0
                            && !entry_valid(l2_entry)) {
                                update_flags |=
                                        RADIX_ENTRY_UPDATE_FLAG_VALID_SET;
                        }
                        delta = (int)pages_in_overlap;
                } else {
                        delta = -(int)pages_in_overlap;
                        if (l2_old_count != 0
                            && (i64)l2_old_count + delta == 0) {
                                if (free_level_table(vs,
                                                     pmm,
                                                     handler,
                                                     entry_child(l2_entry),
                                                     RADIX_TREE_LEVEL3)
                                    != REND_SUCCESS) {
                                        pr_error(
                                                "[ Error ] free level table fail at phase 5 level 2\n");
                                }
                                update_flags =
                                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                        | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET
                                        | RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR;
                        }
                }
                radix_entry_update(l2_entry, update_flags, 0, delta);
                skip_l1_phase =
                        skip_l1_phase
                        && radix_count_trans_to_parent(kind,
                                                       &l2_walk_iter,
                                                       RADIX_TREE_LEVEL1,
                                                       l2_old_count,
                                                       delta,
                                                       &l1_head_delta,
                                                       &l1_tail_delta);
        } while (radix_tree_level_walk(&l2_walk_iter));

        if (skip_l1_phase)
                goto phase6;

        radix_tree_level_walk_init(&l1_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)GIGAN_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL1,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l1_entry = l1_walk_iter.curr_l1_entry;
                u64 l1_old_count = entry_get_count(l1_entry->value);

                int delta = radix_parent_count_delta(kind,
                                                     &l1_walk_iter,
                                                     RADIX_TREE_LEVEL1,
                                                     l1_head_delta,
                                                     l1_tail_delta);
                enum RADIX_ENTRY_UPDATE_FLAGS update_flags =
                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_DELTA;
                if (kind == RADIX_RL_INSERT && entry_child(l1_entry) != 0
                    && !entry_valid(l1_entry)) {
                        update_flags |= RADIX_ENTRY_UPDATE_FLAG_VALID_SET;
                } else {
                        if (l1_old_count != 0
                            && (i64)l1_old_count + delta == 0) {
                                if (free_level_table(vs,
                                                     pmm,
                                                     handler,
                                                     entry_child(l1_entry),
                                                     RADIX_TREE_LEVEL2)
                                    != REND_SUCCESS) {
                                        pr_error(
                                                "[ Error ] free level table fail at phase 5 level 1\n");
                                }
                                update_flags =
                                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                        | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET
                                        | RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR;
                        }
                }
                radix_entry_update(l1_entry, update_flags, 0, delta);

                skip_l0_phase =
                        skip_l0_phase
                        && radix_count_trans_to_parent(kind,
                                                       &l1_walk_iter,
                                                       RADIX_TREE_LEVEL0,
                                                       l1_old_count,
                                                       delta,
                                                       &l0_head_delta,
                                                       &l0_tail_delta);
        } while (radix_tree_level_walk(&l1_walk_iter));

        if (skip_l0_phase)
                goto phase6;

        radix_tree_level_walk_init(&l0_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)HUGE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL0,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l0_entry = l0_walk_iter.curr_l0_entry;
                u64 l0_old_count = entry_get_count(l0_entry->value);

                int delta = radix_parent_count_delta(kind,
                                                     &l0_walk_iter,
                                                     RADIX_TREE_LEVEL0,
                                                     l0_head_delta,
                                                     l0_tail_delta);
                enum RADIX_ENTRY_UPDATE_FLAGS update_flags =
                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_DELTA;
                if (kind == RADIX_RL_INSERT && entry_child(l0_entry) != 0
                    && !entry_valid(l0_entry)) {
                        update_flags |= RADIX_ENTRY_UPDATE_FLAG_VALID_SET;
                } else {
                        if (l0_old_count != 0 && (i64)l0_old_count + delta == 0
                            && L0_INDEX(l0_walk_iter.current_vaddr) < 256) {
                                if (free_level_table(vs,
                                                     pmm,
                                                     handler,
                                                     entry_child(l0_entry),
                                                     RADIX_TREE_LEVEL1)
                                    != REND_SUCCESS) {
                                        pr_error(
                                                "[ Error ] free level table fail at phase 5 level 0\n");
                                }
                                update_flags =
                                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                        | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET
                                        | RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR;
                        }
                }
                radix_entry_update(l0_entry, update_flags, 0, delta);
        } while (radix_tree_level_walk(&l0_walk_iter));
phase6:
        l0_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
        do {
                Radix_entry_t* l0_entry = l0_walk_iter.curr_l0_entry;
                radix_entry_unlock(l0_entry);
        } while (radix_tree_level_walk(&l0_walk_iter));
        return err;

phase3_clean_all:
        radix_lock_rollback_entry(vs, pmm, handler, kind, &l2_walk_iter);
        goto phase3_clean_prev;
phase3_clean_prev:
        l2_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
        while (radix_tree_level_walk(&l2_walk_iter)) {
                radix_lock_rollback_entry(
                        vs, pmm, handler, kind, &l2_walk_iter);
        }
phase2_clean_all:
        radix_lock_rollback_entry(vs, pmm, handler, kind, &l1_walk_iter);
        goto phase2_clean_prev;
phase2_clean_prev:
        l1_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
        while (radix_tree_level_walk(&l1_walk_iter)) {
                radix_lock_rollback_entry(
                        vs, pmm, handler, kind, &l1_walk_iter);
        }
phase1_clean_all:
        radix_lock_rollback_entry(vs, pmm, handler, kind, &l0_walk_iter);
        goto phase1_clean_prev;
phase1_clean_prev:
        l0_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
        while (radix_tree_level_walk(&l0_walk_iter)) {
                radix_lock_rollback_entry(
                        vs, pmm, handler, kind, &l0_walk_iter);
        }
        return err;
}

static error_t radix_range_lock_release(Radix_entry_t* root, vaddr start,
                                        vaddr end)
{
        radix_tree_level_walk_t l2_walk_iter;
        radix_tree_level_walk_init(&l2_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)MIDDLE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL2,
                                   RADIX_TREE_DIRECTION_DEC);
        if (radix_tree_level_walk_check(&l2_walk_iter)) {
                do {
                        Radix_entry_t* l2_entry = l2_walk_iter.curr_l2_entry;
                        radix_entry_unlock(l2_entry);
                } while (radix_tree_level_walk(&l2_walk_iter));
        } else {
                /*Impossible, if fail , we should return error*/
                return -E_RENDEZVOS;
        }
        return REND_SUCCESS;
}

/* === module apis === */

bool vmm_radix_tree_calculate_end_check(vaddr vaddr_start, size_t page_number,
                                        vaddr* vaddr_end_out)
{
        if (page_number == 0 || !vaddr_end_out)
                return false;
        if (page_number > (size_t)(U64_MAX / PAGE_SIZE))
                return false;
        vaddr vaddr_end = vaddr_start + (vaddr)page_number * PAGE_SIZE;
        if (vaddr_end < vaddr_start)
                return false;
        *vaddr_end_out = vaddr_end;
        return radix_check_range(
                vaddr_start, *vaddr_end_out, RADIX_TREE_LEVEL3);
}

static Radix_entry_t* radix_root_from_vs(VSpace* vs)
{
        if (!vs)
                return NULL;
        return (Radix_entry_t*)vs->root_radix;
}

/* === Public API === */
/* === range lock and unlock === */

error_t vmm_radix_tree_lock_range_big(struct map_handler* handler, VSpace* vs,
                                      vaddr vaddr_start, vaddr vaddr_end)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        radix_tree_level_walk_t l0_walk_iter;

        if (!root || !vs || !vs->pmm || !handler)
                return -E_IN_PARAM;

        radix_tree_level_walk_init(&l0_walk_iter,
                                   root,
                                   ROUND_DOWN(vaddr_start,
                                              (vaddr)HUGE_PAGE_SIZE),
                                   vaddr_end,
                                   RADIX_TREE_LEVEL0,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l0_walk_iter))
                return -E_IN_PARAM;
        do {
                radix_entry_lock(l0_walk_iter.curr_l0_entry);
        } while (radix_tree_level_walk(&l0_walk_iter));
        return REND_SUCCESS;
}

error_t vmm_radix_tree_unlock_range_big(VSpace* vs, vaddr vaddr_start,
                                        vaddr vaddr_end)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        radix_tree_level_walk_t l0_walk_iter;

        if (!root)
                return -E_IN_PARAM;
        radix_tree_level_walk_init(&l0_walk_iter,
                                   root,
                                   ROUND_DOWN(vaddr_start,
                                              (vaddr)HUGE_PAGE_SIZE),
                                   vaddr_end,
                                   RADIX_TREE_LEVEL0,
                                   RADIX_TREE_DIRECTION_DEC);
        if (!radix_tree_level_walk_check(&l0_walk_iter))
                return -E_IN_PARAM;
        do {
                radix_entry_unlock(l0_walk_iter.curr_l0_entry);
        } while (radix_tree_level_walk(&l0_walk_iter));
        return REND_SUCCESS;
}

error_t vmm_radix_tree_lock_range_small(struct map_handler* handler, VSpace* vs,
                                        vaddr vaddr_start, vaddr vaddr_end,
                                        radix_lock_acquire_kind_t kind)
{
        Radix_entry_t* root = radix_root_from_vs(vs);

        if (!root || !vs || !vs->pmm || !handler)
                return -E_IN_PARAM;
        if (kind < 0 || kind > (int)RADIX_RL_QUERY_OR_CHANGE)
                return -E_IN_PARAM;
        return radix_range_lock_acquire(
                vs, handler, root, vaddr_start, vaddr_end, kind);
}

error_t vmm_radix_tree_unlock_range_small(VSpace* vs, vaddr vaddr_start,
                                          vaddr vaddr_end)
{
        Radix_entry_t* root = radix_root_from_vs(vs);

        if (!root)
                return -E_IN_PARAM;
        return radix_range_lock_release(root, vaddr_start, vaddr_end);
}

/* === range ops apis === */

error_t vmm_radix_tree_insert_range(struct map_handler* handler, VSpace* vs,
                                    tagged_ptr_t owner_info, vaddr vaddr_start,
                                    ENTRY_FLAGS_t flags, vaddr vaddr_end)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        if (!root || !vs || !vs->pmm || !handler)
                return -E_IN_PARAM;

        error_t err = REND_SUCCESS;

        /*
         * LAZY shadow records intent before PTE commit; it must not set
         * PAGE_ENTRY_VALID — leaf_bind_range rejects VALID|LAZY and then sets
         * VALID after rmap + caller's map().
         */
        ENTRY_FLAGS_t lazy_leaf_flags = entry_flags_rm_sw_flags(flags)
                                        | (ENTRY_FLAGS_t)PAGE_ENTRY_LAZY;
        lazy_leaf_flags = (ENTRY_FLAGS_t)clear_mask_u64((u64)lazy_leaf_flags,
                                                        (u64)PAGE_ENTRY_VALID);
        radix_tree_level_walk_t l3_walk;

        radix_tree_level_walk_init(&l3_walk,
                                   root,
                                   vaddr_start,
                                   vaddr_end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk)) {
                err = -E_IN_PARAM;
                goto insert_range_out;
        }
        do {
                Radix_node_t* leaf = l3_walk.curr_l3_node;
                leaf->flags = lazy_leaf_flags;
                leaf->owner = owner_info;
        } while (radix_tree_level_walk(&l3_walk));

insert_range_out:
        return err;
}

static error_t radix_leaf_link_rmap(struct pmm* pmm, ppn_t ppn,
                                    Radix_node_t* leaf)
{
        if (!pmm || !pmm->zone || invalid_ppn(ppn) || !leaf)
                return -E_IN_PARAM;
        ZonePageCursor cur;
        Page* p_ptr = zone_page_cursor_init(&cur, pmm->zone, ppn);
        if (!p_ptr)
                return -E_IN_PARAM;

        pmm_zone_lock(pmm->zone);
        list_add_head(&leaf->rmap_list, &p_ptr->rmap_list);
        pmm_zone_unlock(pmm->zone);
        return REND_SUCCESS;
}

static void radix_leaf_unlink_rmap(struct pmm* pmm, ppn_t ppn,
                                   Radix_node_t* leaf)
{
        if (!pmm || !pmm->zone || invalid_ppn(ppn) || !leaf)
                return;
        ZonePageCursor cur;
        if (!zone_page_cursor_init(&cur, pmm->zone, ppn))
                return;
        pmm_zone_lock(pmm->zone);
        if (!list_node_is_detached(&leaf->rmap_list))
                list_del_init(&leaf->rmap_list);
        pmm_zone_unlock(pmm->zone);
}

error_t vmm_radix_tree_leaf_bind_range(struct map_handler* handler, VSpace* vs,
                                       vaddr vaddr_start, ppn_t ppn_first,
                                       vaddr vaddr_end,
                                       ENTRY_FLAGS_t leaf_flags)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        size_t page_number;
        if (!root || !vs || !vs->pmm || !vs->pmm->zone || !handler)
                return -E_IN_PARAM;
        if (invalid_ppn(ppn_first))
                return -E_IN_PARAM;
        page_number = (size_t)((vaddr_end - vaddr_start) / PAGE_SIZE);
        ppn_t ppn_last = ppn_first + (ppn_t)page_number - 1;
        if (ppn_last < ppn_first || invalid_ppn(ppn_last))
                return -E_IN_PARAM;

        error_t err = REND_SUCCESS;

        radix_tree_level_walk_t l3_walk;

        radix_tree_level_walk_init(&l3_walk,
                                   root,
                                   vaddr_start,
                                   vaddr_end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk)) {
                err = -E_IN_PARAM;
                goto leaf_bind_range_out;
        }

        size_t page_index = 0;
        do {
                Radix_node_t* leaf = l3_walk.curr_l3_node;
                if (!leaf || !(leaf->flags & PAGE_ENTRY_LAZY)
                    || (leaf->flags & (ENTRY_FLAGS_t)PAGE_ENTRY_VALID)) {
                        err = -E_IN_PARAM;
                        goto leaf_bind_range_rollback;
                }

                ppn_t cur_ppn = ppn_first + (ppn_t)page_index;
                err = radix_leaf_link_rmap(vs->pmm, cur_ppn, leaf);
                if (err != REND_SUCCESS)
                        goto leaf_bind_range_rollback;

                leaf->flags = entry_flags_rm_sw_flags(leaf_flags)
                              | (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;
                page_index++;
        } while (radix_tree_level_walk(&l3_walk));

        if (page_index != page_number) {
                err = -E_IN_PARAM;
                goto leaf_bind_range_rollback;
        }

        goto leaf_bind_range_out;

leaf_bind_range_rollback:
        l3_walk.direction = RADIX_TREE_DIRECTION_DEC;
        while (page_index > 0 && radix_tree_level_walk(&l3_walk)) {
                page_index--;
                Radix_node_t* leaf = l3_walk.curr_l3_node;
                ppn_t cur_ppn = ppn_first + (ppn_t)page_index;
                radix_leaf_unlink_rmap(vs->pmm, cur_ppn, leaf);
                leaf->flags = entry_flags_rm_sw_flags(leaf_flags)
                              | (ENTRY_FLAGS_t)PAGE_ENTRY_LAZY;
        }

leaf_bind_range_out:
        return err;
}

error_t vmm_radix_tree_leaf_unbind_range(struct map_handler* handler,
                                         VSpace* vs, vaddr vaddr_start,
                                         ppn_t ppn_first, vaddr vaddr_end)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        size_t page_number;
        if (!root || !vs || !vs->pmm || !vs->pmm->zone || !handler)
                return -E_IN_PARAM;
        if (invalid_ppn(ppn_first))
                return -E_IN_PARAM;
        page_number = (size_t)((vaddr_end - vaddr_start) / PAGE_SIZE);
        ppn_t ppn_last = ppn_first + (ppn_t)page_number - 1;
        if (ppn_last < ppn_first || invalid_ppn(ppn_last))
                return -E_IN_PARAM;

        error_t err = REND_SUCCESS;

        radix_tree_level_walk_t l3_walk;

        radix_tree_level_walk_init(&l3_walk,
                                   root,
                                   vaddr_start,
                                   vaddr_end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk)) {
                err = -E_IN_PARAM;
                goto leaf_unbind_range_out;
        }
        size_t validated = 0;
        do {
                Radix_node_t* leaf = l3_walk.curr_l3_node;
                if (!leaf || !(leaf->flags & PAGE_ENTRY_VALID)) {
                        err = -E_IN_PARAM;
                        goto leaf_unbind_range_out;
                }
                validated++;
        } while (radix_tree_level_walk(&l3_walk));
        if (validated != page_number) {
                err = -E_IN_PARAM;
                goto leaf_unbind_range_out;
        }

        /* Pass 1 ended on the last page; flip to DEC and unbind high PPN first.
         */
        l3_walk.direction = RADIX_TREE_DIRECTION_DEC;
        size_t remain = page_number;
        do {
                Radix_node_t* leaf = l3_walk.curr_l3_node;
                ppn_t cur_ppn = ppn_first + (ppn_t)(remain - 1);
                radix_leaf_unlink_rmap(vs->pmm, cur_ppn, leaf);
                leaf->flags = entry_flags_rm_sw_flags(leaf->flags)
                              & ~(ENTRY_FLAGS_t)PAGE_ENTRY_VALID;
                leaf->flags |= (ENTRY_FLAGS_t)PAGE_ENTRY_LAZY;
                remain--;
        } while (remain != 0 && radix_tree_level_walk(&l3_walk));

leaf_unbind_range_out:
        return err;
}

error_t vmm_radix_tree_leaf_bind(struct map_handler* handler, VSpace* vs,
                                 vaddr vaddr_start, ppn_t ppn,
                                 ENTRY_FLAGS_t leaf_flags)
{
        return vmm_radix_tree_leaf_bind_range(handler,
                                              vs,
                                              vaddr_start,
                                              ppn,
                                              vaddr_start + (vaddr)PAGE_SIZE,
                                              leaf_flags);
}

error_t vmm_radix_tree_leaf_unbind(struct map_handler* handler, VSpace* vs,
                                   vaddr vaddr_start, ppn_t ppn)
{
        return vmm_radix_tree_leaf_unbind_range(
                handler, vs, vaddr_start, ppn, vaddr_start + (vaddr)PAGE_SIZE);
}

error_t vmm_radix_tree_change_leaf_ppn(struct map_handler* handler, VSpace* vs,
                                       vaddr vaddr_start, vaddr vaddr_end,
                                       ppn_t old_ppn, ppn_t new_ppn,
                                       ENTRY_FLAGS_t leaf_flags)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        if (!root || !vs || !vs->pmm || !vs->pmm->zone || !handler
            || invalid_ppn(old_ppn) || invalid_ppn(new_ppn))
                return -E_IN_PARAM;

        error_t err = REND_SUCCESS;

        radix_tree_level_walk_t l3_walk;

        radix_tree_level_walk_init(&l3_walk,
                                   root,
                                   vaddr_start,
                                   vaddr_end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk)) {
                err = -E_IN_PARAM;
                goto change_leaf_ppn_out;
        }

        Radix_node_t* leaf = l3_walk.curr_l3_node;
        if (!leaf || !(leaf->flags & PAGE_ENTRY_VALID)) {
                err = -E_IN_PARAM;
                goto change_leaf_ppn_out;
        }

        ENTRY_FLAGS_t pte_flags = entry_flags_rm_sw_flags(leaf_flags);

        if (old_ppn != new_ppn) {
                ZonePageCursor cur_old, cur_new;
                if (!zone_page_cursor_init(&cur_old, vs->pmm->zone, old_ppn)
                    || !zone_page_cursor_init(
                            &cur_new, vs->pmm->zone, new_ppn)) {
                        err = -E_IN_PARAM;
                        goto change_leaf_ppn_out;
                }

                radix_leaf_unlink_rmap(vs->pmm, old_ppn, leaf);
                err = radix_leaf_link_rmap(vs->pmm, new_ppn, leaf);
                if (err != REND_SUCCESS) {
                        (void)radix_leaf_link_rmap(vs->pmm, old_ppn, leaf);
                        goto change_leaf_ppn_out;
                }
        }

        leaf->flags = pte_flags | (ENTRY_FLAGS_t)PAGE_ENTRY_VALID;

change_leaf_ppn_out:
        return err;
}

error_t vmm_radix_tree_change_leaf_ppn_flag(struct map_handler* handler,
                                            VSpace* vs, vaddr vaddr_start,
                                            vaddr vaddr_end, ppn_t old_ppn,
                                            ppn_t new_ppn,
                                            ENTRY_FLAGS_t new_flag)
{
        return vmm_radix_tree_change_leaf_ppn(
                handler, vs, vaddr_start, vaddr_end, old_ppn, new_ppn, new_flag);
}

error_t vmm_radix_tree_change_range_flag(struct map_handler* handler,
                                         VSpace* vs, vaddr vaddr_start,
                                         vaddr vaddr_end,
                                         ENTRY_FLAGS_t new_flags)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        if (!root || !vs || !vs->pmm || !handler)
                return -E_IN_PARAM;

        error_t err = REND_SUCCESS;

        radix_tree_level_walk_t l3_walk;

        radix_tree_level_walk_init(&l3_walk,
                                   root,
                                   vaddr_start,
                                   vaddr_end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk)) {
                err = -E_IN_PARAM;
                goto change_range_flag_out;
        }
        do {
                l3_walk.curr_l3_node->flags = new_flags;
        } while (radix_tree_level_walk(&l3_walk));

change_range_flag_out:
        return err;
}

error_t vmm_radix_tree_query_leaf(struct map_handler* handler, VSpace* vs,
                                  vaddr vaddr_start, vaddr vaddr_end,
                                  ENTRY_FLAGS_t* out_flags,
                                  tagged_ptr_t* out_owner)
{
        Radix_entry_t* root = radix_root_from_vs(vs);
        if (!root || !vs || !vs->pmm || !handler || (!out_flags && !out_owner))
                return -E_IN_PARAM;

        error_t err = REND_SUCCESS;

        radix_tree_level_walk_t l3_walk;

        radix_tree_level_walk_init(&l3_walk,
                                   root,
                                   vaddr_start,
                                   vaddr_end,
                                   RADIX_TREE_LEVEL3,
                                   RADIX_TREE_DIRECTION_INC);
        if (!radix_tree_level_walk_check(&l3_walk)) {
                if (out_flags)
                        *out_flags = (ENTRY_FLAGS_t)0;
                if (out_owner)
                        *out_owner = tp_new_none();
                err = -E_IN_PARAM;
                goto query_leaf_out;
        }
        if (out_flags)
                *out_flags = l3_walk.curr_l3_node->flags;
        if (out_owner)
                *out_owner = l3_walk.curr_l3_node->owner;

query_leaf_out:
        return err;
}

Radix_entry_t* vmm_radix_tree_init(struct map_handler* handler, VSpace* vs)
{
        if (!vs || !vs->pmm || !handler)
                return NULL;
        if (vs->root_radix)
                return (Radix_entry_t*)vs->root_radix;

        vaddr table_vaddr;
        if (radix_alloc_level_table(
                    vs, vs->pmm, handler, &table_vaddr, RADIX_TREE_LEVEL0)
            != REND_SUCCESS)
                return NULL;
        vs->root_radix = (void*)table_vaddr;
        return (Radix_entry_t*)vs->root_radix;
}

error_t vmm_radix_tree_destroy(struct map_handler* handler, VSpace* vs)
{
        if (!vs || !vs->pmm || !handler)
                return -E_IN_PARAM;
        Radix_entry_t* root = radix_root_from_vs(vs);
        if (!root)
                return REND_SUCCESS;
        struct pmm* pmm = vs->pmm;
        /*only destroy the low half tree */
        const vaddr low_half_end = (vaddr)256 * radix_level_step[0];
        for (vaddr l0_iter = 0; l0_iter < low_half_end;
             l0_iter += radix_level_step[0]) {
                Radix_entry_t* l0_entry = radix_l0_entry(root, l0_iter);
                if (!entry_valid(l0_entry))
                        continue;
                vaddr l1_table_vaddr = entry_child(l0_entry);
                for (vaddr l1_iter = l0_iter;
                     l1_iter < l0_iter + radix_level_step[0];
                     l1_iter += radix_level_step[1]) {
                        Radix_entry_t* l1_entry =
                                radix_l1_entry(l0_entry, l1_iter);
                        if (!l1_entry || !entry_valid(l1_entry))
                                continue;
                        vaddr l2_table_vaddr = entry_child(l1_entry);
                        for (vaddr l2_iter = l1_iter;
                             l2_iter < l1_iter + radix_level_step[1];
                             l2_iter += radix_level_step[2]) {
                                Radix_entry_t* l2_entry =
                                        radix_l2_entry(l1_entry, l2_iter);
                                if (!l2_entry || !entry_valid(l2_entry))
                                        continue;
                                vaddr l3_table_vaddr = entry_child(l2_entry);
                                if (pmm->zone) {
                                        pmm_zone_lock(pmm->zone);
                                        for (vaddr l3_iter = l2_iter;
                                             l3_iter
                                             < l2_iter + radix_level_step[2];
                                             l3_iter += radix_level_step[3]) {
                                                Radix_node_t* leaf =
                                                        radix_l3_node(l2_entry,
                                                                      l3_iter);
                                                if (!leaf)
                                                        continue;
                                                if (!list_node_is_detached(
                                                            &leaf->rmap_list))
                                                        list_del_init(
                                                                &leaf->rmap_list);
                                        }
                                        pmm_zone_unlock(pmm->zone);
                                }
                                (void)free_level_table(vs,
                                                       pmm,
                                                       handler,
                                                       l3_table_vaddr,
                                                       RADIX_TREE_LEVEL3);
                                l2_entry->value = 0;
                        }
                        (void)free_level_table(vs,
                                               pmm,
                                               handler,
                                               l2_table_vaddr,
                                               RADIX_TREE_LEVEL2);
                        l1_entry->value = 0;
                }
                (void)free_level_table(
                        vs, pmm, handler, l1_table_vaddr, RADIX_TREE_LEVEL1);
                l0_entry->value = 0;
        }
        vaddr root_vaddr = (vaddr)root;
        (void)free_level_table(vs, pmm, handler, root_vaddr, RADIX_TREE_LEVEL0);
        vs->root_radix = (void*)NULL;
        return REND_SUCCESS;
}
/* shared high half l1 pages */
static ppn_t kernel_radix_l1_table_base_ppn;

error_t
vmm_radix_tree_bootstrap_shared_kernel_high_half(struct map_handler* handler,
                                                 VSpace* vs)
{
        if (!invalid_ppn(kernel_radix_l1_table_base_ppn))
                return REND_SUCCESS;
        if (!vs || !vs->pmm || !handler)
                return -E_IN_PARAM;
        struct pmm* pmm = vs->pmm;
        size_t alloced = 0;
        ppn_t ppn = pmm->pmm_alloc(pmm, 256, &alloced);
        if (invalid_ppn(ppn) || alloced != 256)
                return -E_RENDEZVOS;
        ppn_t table_base_ppn = ppn;
        vaddr table_base_vaddr = KERNEL_PHY_TO_VIRT(PADDR(table_base_ppn));
        for (u64 table_page_index = 0; table_page_index < 256;
             table_page_index++) {
                ppn_t page_ppn =
                        (ppn_t)((i64)table_base_ppn + (i64)table_page_index);
                vaddr page_vaddr =
                        table_base_vaddr + (vaddr)table_page_index * PAGE_SIZE;
                error_t map_status = map(vs,
                                         page_ppn,
                                         VPN(page_vaddr),
                                         3,
                                         (ENTRY_FLAGS_t)RADIX_KERNEL_MAP_FLAGS,
                                         handler);
                if (map_status != REND_SUCCESS) {
                        for (u64 rollback_index = 0;
                             rollback_index < table_page_index;
                             rollback_index++) {
                                vaddr unmap_table_vaddr =
                                        table_base_vaddr
                                        + (vaddr)rollback_index * PAGE_SIZE;
                                (void)unmap(
                                        vs, VPN(unmap_table_vaddr), 0, handler);
                        }
                        (void)pmm->pmm_free(pmm, table_base_ppn, 256);
                        return map_status;
                }
                memset((void*)page_vaddr, 0, PAGE_SIZE);
        }
        kernel_radix_l1_table_base_ppn = table_base_ppn;
        return REND_SUCCESS;
}

error_t
vmm_radix_tree_install_shared_kernel_high_half(struct map_handler* handler,
                                               VSpace* vs)
{
        (void)handler;
        Radix_entry_t* root = radix_root_from_vs(vs);
        if (!root || invalid_ppn(kernel_radix_l1_table_base_ppn))
                return -E_IN_PARAM;

        /* L0 slots [256,512): 512*HUGE == 2^48 (vaddr is u64, no wrap). */
        const vaddr high_half_begin = (vaddr)256 * radix_level_step[0];
        const vaddr high_half_end = (vaddr)512 * radix_level_step[0];
        for (vaddr vaddr_iter = high_half_begin; vaddr_iter < high_half_end;
             vaddr_iter += radix_level_step[0]) {
                Radix_entry_t* l0_entry = radix_l0_entry(root, vaddr_iter);
                ppn_t l1_table_ppn =
                        (ppn_t)((i64)kernel_radix_l1_table_base_ppn
                                + (i64)(L0_INDEX(vaddr_iter) - 256u));
                vaddr expected_l1_table_vaddr =
                        KERNEL_PHY_TO_VIRT(PADDR(l1_table_ppn));

                radix_entry_lock(l0_entry);
                if (entry_valid(l0_entry)) {
                        if (entry_child(l0_entry) != expected_l1_table_vaddr) {
                                radix_entry_unlock(l0_entry);
                                return -E_IN_PARAM;
                        }
                        radix_entry_unlock(l0_entry);
                        continue;
                }
                (void)radix_entry_update(
                        l0_entry,
                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                | RADIX_ENTRY_UPDATE_FLAG_UPDATE_PTR
                                | RADIX_ENTRY_UPDATE_FLAG_VALID_SET
                                | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET,
                        expected_l1_table_vaddr,
                        0);
                radix_entry_unlock(l0_entry);
        }
        return REND_SUCCESS;
}