
#include <rendezvos/mm/vmm_radix_tree.h>
#include <rendezvos/mm/nexus_base.h>
#include <common/bit.h>
#include <common/align.h>
#include <common/string.h>
#include <rendezvos/sync/cas_lock.h>

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

static error_t radix_entry_update(Radix_entry_t* entry,
                                  enum RADIX_ENTRY_UPDATE_FLAGS update_flags,
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
                        if (count_delta > 512)
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
        if (node->flags & (PAGE_ENTRY_VALID | PAGE_ENTRY_NEXUS_LAZY))
                return true;
        if (node->vs_ptr != NULL)
                return true;
        return false;
}
static bool radix_l3_deletable(const Radix_node_t* node)
{
        return (node->flags & (PAGE_ENTRY_VALID | PAGE_ENTRY_NEXUS_LAZY)) != 0;
}

/*new table's free and alloc logic*/
static error_t free_radix_table(VS_Common* vs, struct pmm* pmm,
                                struct map_handler* handler,
                                vaddr page_vaddr_start, vaddr page_vaddr_end,
                                size_t free_page_number)
{
        for (vaddr page_vaddr_back_iter = page_vaddr_start;
             page_vaddr_back_iter < page_vaddr_end;
             page_vaddr_back_iter += PAGE_SIZE) {
                (void)unmap(vs, VPN(page_vaddr_back_iter), 0, handler);
        }
        (void)pmm->pmm_free(pmm,
                            PPN(KERNEL_VIRT_TO_PHY(page_vaddr_start)),
                            free_page_number);
        return REND_SUCCESS;
}
static error_t radix_alloc_level_table(VS_Common* vs, struct pmm* pmm,
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
        (void)free_radix_table(vs,
                               pmm,
                               handler,
                               page_vaddr_start,
                               page_vaddr_iter,
                               need_alloc_pages);
        return -E_RENDEZVOS;
}
static error_t free_level_table(VS_Common* vs, struct pmm* pmm,
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
        (void)free_radix_table(vs,
                               pmm,
                               handler,
                               page_vaddr,
                               page_vaddr + need_alloc_pages * PAGE_SIZE,
                               need_alloc_pages);
        return REND_SUCCESS;
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
        if (!walk_iter || !root
            || !radix_check_range(range_start_vaddr, range_end_vaddr, level))
                return;
        if (direction != RADIX_TREE_DIRECTION_INC
            && direction != RADIX_TREE_DIRECTION_DEC)
                return;
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
                start_addr = ROUND_DOWN(range_end_vaddr - 1, step);
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
        else
                walk_iter->curr_l1_entry = NULL;

        if (level >= 2 && walk_iter->curr_l1_entry)
                walk_iter->curr_l2_entry = radix_l2_entry(
                        walk_iter->curr_l1_entry, walk_iter->current_vaddr);
        else
                walk_iter->curr_l2_entry = NULL;

        if (level >= 3 && walk_iter->curr_l2_entry)
                walk_iter->curr_l3_node = radix_l3_node(
                        walk_iter->curr_l2_entry, walk_iter->current_vaddr);
        else
                walk_iter->curr_l3_node = NULL;
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
        vaddr last = ROUND_DOWN(walk->range_end_vaddr - 1, step);
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
        vaddr last = ROUND_DOWN(walk_iter->range_end_vaddr - 1, step);
        return first == last;
}

/*module base3: the range lock and unlock realize*/
typedef enum {
        RADIX_RL_INSERT = 0,
        RADIX_RL_DELETE,
        /* No L2 occupancy adjustment (mprotect-style metadata / query). */
        RADIX_RL_QUERY_OR_CHANGE,
} radix_lock_acquire_kind_t;

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
static inline void
radix_count_trans_to_parent(radix_lock_acquire_kind_t kind,
                            const radix_tree_level_walk_t* walk,
                            int partition_level, u64 old_count, int delta,
                            int* parent_head_delta, int* parent_tail_delta)
{
        bool insert_should_trans =
                (kind == RADIX_RL_INSERT && old_count == 0 && delta != 0);
        bool delete_should_trans = (kind == RADIX_RL_DELETE && old_count != 0
                                    && (i64)old_count + delta == 0);

        if (radix_tree_walk_is_level_single(walk, partition_level)
            || radix_tree_walk_is_level_first(walk, partition_level)) {
                if (insert_should_trans) {
                        *parent_head_delta += 1;
                } else if (delete_should_trans) {
                        *parent_head_delta -= 1;
                }
                return;
        }
        if (radix_tree_walk_is_level_last(walk, partition_level)) {
                if (insert_should_trans) {
                        *parent_tail_delta += 1;
                } else if (delete_should_trans) {
                        *parent_tail_delta -= 1;
                }
        }
}
static void radix_lock_rollback_entry(VS_Common* vs, struct pmm* pmm,
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
        free_level_table(
                vs, pmm, handler, child_page_addr, walk->walk_level + 1);
        entry->value = 0;
}

static error_t radix_range_lock_acquire(VS_Common* vs,
                                        struct map_handler* handler,
                                        Radix_entry_t* root, vaddr start,
                                        vaddr end,
                                        radix_lock_acquire_kind_t kind)
{
        error_t err = REND_SUCCESS;
        radix_tree_level_walk_t l0_walk_iter, l1_walk_iter, l2_walk_iter,
                l3_walk_iter;
        struct pmm* pmm = vs->pmm;
        enum RADIX_ENTRY_UPDATE_FLAGS update_flags =
                RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
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
                if (kind == RADIX_RL_INSERT) {
                        if (!entry_valid(l0_entry)
                            && entry_child(l0_entry) == 0) {
                                vaddr l1_table_vaddr;
                                err = radix_alloc_level_table(
                                        vs,
                                        pmm,
                                        handler,
                                        &l1_table_vaddr,
                                        RADIX_TREE_LEVEL1);
                                if (err != REND_SUCCESS) {
                                        radix_entry_unlock(l0_entry);
                                        goto phase1_clean_prev;
                                }
                                radix_entry_update(l0_entry,
                                                   update_flags,
                                                   l1_table_vaddr,
                                                   0);
                        }
                } else {
                        if (!entry_valid(l0_entry)) {
                                err = -E_REND_NOFOUND;
                                radix_entry_unlock(l0_entry);
                                goto phase1_clean_prev;
                        }
                }
        } while (radix_tree_level_walk(&l0_walk_iter));

        /*Phase 2, alloc l2 table if need*/
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
                if (kind == RADIX_RL_INSERT) {
                        if (!entry_valid(l1_entry)
                            && entry_child(l1_entry) == 0) {
                                vaddr l2_table_vaddr;
                                err = radix_alloc_level_table(
                                        vs,
                                        pmm,
                                        handler,
                                        &l2_table_vaddr,
                                        RADIX_TREE_LEVEL2);
                                if (err != REND_SUCCESS) {
                                        goto phase2_clean_prev;
                                }
                                radix_entry_update(l1_entry,
                                                   update_flags,
                                                   l2_table_vaddr,
                                                   0);
                        }
                } else {
                        if (!entry_valid(l1_entry)) {
                                err = -E_REND_NOFOUND;
                                goto phase2_clean_prev;
                        }
                }
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
                if (kind == RADIX_RL_INSERT) {
                        if (!entry_valid(l2_entry)
                            && entry_child(l2_entry) == 0) {
                                vaddr l3_table_vaddr;
                                err = radix_alloc_level_table(
                                        vs,
                                        pmm,
                                        handler,
                                        &l3_table_vaddr,
                                        RADIX_TREE_LEVEL3);
                                if (err != REND_SUCCESS) {
                                        radix_entry_unlock(l2_entry);
                                        goto phase3_clean_prev;
                                }
                                radix_entry_update(l2_entry,
                                                   update_flags,
                                                   l3_table_vaddr,
                                                   0);
                        }
                } else {
                        if (!entry_valid(l2_entry)) {
                                err = -E_REND_NOFOUND;
                                radix_entry_unlock(l2_entry);
                                goto phase3_clean_prev;
                        }
                }
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
                } else {
                        if (!radix_l3_deletable(l3_node)) {
                                err = -E_REND_NOFOUND;
                                goto phase3_clean_all;
                        }
                        l3_node->flags = 0;
                        l3_node->vs_ptr = NULL;
                        INIT_LIST_HEAD(&l3_node->rmap_list);
                }
        } while (radix_tree_level_walk(&l3_walk_iter));

        /*Phase 5, update the valid and count for level 0/1/2, no fail possible
         * at this phase begin, so we can reuse the level 0/1/2 walk iters*/
        int l1_head_delta = 0, l1_tail_delta = 0;
        int l0_head_delta = 0, l0_tail_delta = 0;

        radix_tree_level_walk_init(&l2_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)MIDDLE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL2,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l2_entry = l2_walk_iter.curr_l2_entry;
                u64 l2_old_count = entry_get_count(l2_entry->value);
                if (kind == RADIX_RL_QUERY_OR_CHANGE) {
                        continue;
                }

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
                                (void)free_level_table(vs,
                                                       pmm,
                                                       handler,
                                                       entry_child(l2_entry),
                                                       RADIX_TREE_LEVEL3);
                                update_flags =
                                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                        | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET
                                        | RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR;
                        }
                }
                radix_entry_update(l2_entry, update_flags, 0, delta);
                radix_count_trans_to_parent(kind,
                                            &l2_walk_iter,
                                            RADIX_TREE_LEVEL1,
                                            l2_old_count,
                                            delta,
                                            &l1_head_delta,
                                            &l1_tail_delta);
        } while (radix_tree_level_walk(&l2_walk_iter));

        radix_tree_level_walk_init(&l1_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)GIGAN_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL1,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l1_entry = l1_walk_iter.curr_l1_entry;
                u64 l1_old_count = entry_get_count(l1_entry->value);

                if (kind == RADIX_RL_QUERY_OR_CHANGE) {
                        continue;
                }
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
                                (void)free_level_table(vs,
                                                       pmm,
                                                       handler,
                                                       entry_child(l1_entry),
                                                       RADIX_TREE_LEVEL2);
                                update_flags =
                                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                        | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET
                                        | RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR;
                        }
                }
                radix_entry_update(l1_entry, update_flags, 0, delta);

                radix_count_trans_to_parent(kind,
                                            &l1_walk_iter,
                                            RADIX_TREE_LEVEL0,
                                            l1_old_count,
                                            delta,
                                            &l0_head_delta,
                                            &l0_tail_delta);
        } while (radix_tree_level_walk(&l1_walk_iter));

        radix_tree_level_walk_init(&l0_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)HUGE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL0,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l0_entry = l0_walk_iter.curr_l0_entry;
                u64 l0_old_count = entry_get_count(l0_entry->value);

                if (kind == RADIX_RL_QUERY_OR_CHANGE) {
                        continue;
                }
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
                                (void)free_level_table(vs,
                                                       pmm,
                                                       handler,
                                                       entry_child(l0_entry),
                                                       RADIX_TREE_LEVEL1);
                                update_flags =
                                        RADIX_ENTRY_UPDATE_FLAG_INHERIT_LOCK
                                        | RADIX_ENTRY_UPDATE_FLAG_VALID_CLEAR
                                        | RADIX_ENTRY_UPDATE_FLAG_COUNT_RESET
                                        | RADIX_ENTRY_UPDATE_FLAG_CLEAR_PTR;
                        }
                }
                radix_entry_update(l0_entry, update_flags, 0, delta);
        } while (radix_tree_level_walk(&l0_walk_iter));
        /*Phase 6, unlock the l0, and now only level 2 lock is hold*/
        radix_tree_level_walk_init(&l0_walk_iter,
                                   root,
                                   ROUND_DOWN(start, (vaddr)HUGE_PAGE_SIZE),
                                   end,
                                   RADIX_TREE_LEVEL0,
                                   RADIX_TREE_DIRECTION_INC);
        do {
                Radix_entry_t* l0_entry = l0_walk_iter.curr_l0_entry;
                radix_entry_unlock(l0_entry);
        } while (radix_tree_level_walk(&l0_walk_iter));
        return err;

phase3_clean_all:
        radix_lock_rollback_entry(vs, pmm, handler, kind, &l2_walk_iter);
phase3_clean_prev:
        l2_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
        while (radix_tree_level_walk(&l2_walk_iter)) {
                radix_lock_rollback_entry(
                        vs, pmm, handler, kind, &l2_walk_iter);
        }
phase2_clean_all:
        radix_lock_rollback_entry(vs, pmm, handler, kind, &l1_walk_iter);
phase2_clean_prev:
        l1_walk_iter.direction = RADIX_TREE_DIRECTION_DEC;
        while (radix_tree_level_walk(&l1_walk_iter)) {
                radix_lock_rollback_entry(
                        vs, pmm, handler, kind, &l1_walk_iter);
        }
phase1_clean_all:
        radix_lock_rollback_entry(vs, pmm, handler, kind, &l0_walk_iter);
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