# RendezvOS 内存系统设计文档

本文档描述 RendezvOS 内存子系统的分层设计与实现，与当前代码保持一致。整体分为：物理内存管理（含架构相关的启动布局与 zone/buddy）、虚拟内存与页表自映射、每 CPU 虚拟页分配器 Nexus、以及在此基础上实现的内核对象分配器（kmalloc）。

---

## 1. 总览与分层

内存子系统自上而下分为：

| 层次 | 组件 | 职责 |
|------|------|------|
| 上层 | 内核对象分配器 (kmalloc) | 按固定大小档位分配小对象，整页需求转发给 Nexus |
| 中层 | Nexus（每 CPU） | 虚拟页的分配/释放与映射记录，替代 Linux 式 mm zone |
| 底层 | Map Handler + 页表 | 按需映射页表项，采用页表自映射方案，非一次性全映射 |
| 底层 | PMM（Zone + Buddy） | 物理页框的 zone 划分与 buddy 分配 |

设计要点：

- **物理内存**：从引导信息获取可用区间，按架构预留 kernel/percpu/pmm 等区域，再按 zone 划分，zone 内用 buddy 管理。
- **虚拟内存**：内核采用恒等映射（identity mapping）；修改页表时通过固定的“自映射”虚拟区临时映射 L0/L1/L2/L3，无需预先映射全部页表页。
- **Nexus**：每个 CPU 一个 Nexus，负责从 PMM 要物理页、建立映射、并在红黑树中记录 (vaddr, len, flags)，供内核与用户态虚拟页分配使用。
- **kmalloc**：每个 CPU 一个 allocator，从本 CPU 的 Nexus 取页，按 12 个档位用 chunk/object 管理，跨 CPU 释放通过无锁队列转发到目标 allocator。

以下按物理内存、虚拟内存、Nexus、内核对象分配器的顺序展开。

---

## 2. 物理内存管理（PMM）

### 2.1 可用物理内存的获取（与架构相关）

可用物理内存由“内存区间”列表描述，抽象如下（`include/rendezvos/mm/pmm.h`、`include/common/mm.h`）：

```c
struct region {
        u64 addr;
        u64 len;
};

struct memory_regions {
        u64 region_count;
        struct region memory_regions[RENDEZVOS_MAX_MEMORY_REGIONS];
        error_t (*memory_regions_insert)(paddr addr, u64 len);
        void (*memory_regions_delete)(int index);
        bool (*memory_regions_entry_empty)(int index);
        int (*memory_regions_reserve_region)(paddr phy_start, paddr phy_end);
        int (*memory_regions_reserve_region_with_length)(size_t length,
                                                         u64 start_alignment,
                                                         paddr *phy_start,
                                                         paddr *phy_end);
        void (*memory_regions_init)(struct memory_regions *m_regions);
};
extern struct memory_regions m_regions;
```

- 每个 `region` 表示一段连续可用物理区间 `[addr, addr+len)`。操作函数用于插入区间、从已有区间中预留出一段（reserve），供 kernel/percpu/PMM 等使用。

**x86_64**（`arch/x86_64/mm/pmm.c`）：

- 通过 Multiboot1/Multiboot2 的 mmap 获取内存映射。
- 仅接受 `addr + len > BIOS_MEM_UPPER` 且 `type == MULTIBOOT_MEMORY_AVAILABLE` 的区间，并调用 `memory_regions_insert` 填入 `m_regions`。
- `arch_init_pmm` 中还会 `reserve_arch_region`（例如 ACPI 表所在区间），最后将 `*next_region_phy_start` 设为内核镜像结束物理地址 `_end`。

**aarch64**（`arch/aarch64/mm/pmm.c`）：

- 从 DTB 解析 `memory` 节点（`device_type = "memory"`），在 `get_mem_prop_and_insert_region` 中把每段 `(addr, mem_len)` 插入 `m_regions`。
- `arch_init_pmm` 将 `*next_region_phy_start` 设为 `arch_setup_info->map_end_virt_addr` 对应的物理地址（即当前 boot 映射结束位置）。

两架构在 `phy_mm_init` 之前都会完成对 `m_regions` 的填充与必要的 arch 预留。

### 2.2 物理内存布局（按架构）

在 `phy_mm_init`（`kernel/mm/pmm.c`）中，会依次保留：内核区、per-CPU 区、PMM 管理数据区，并保证内核 + percpu 落在同一 1G 内（以支持当前 L2 映射方式）。PMM 区紧接在 percpu 之后，包含 L2 表页与各 zone 的 section/page 及 buddy 元数据。

#### x86_64 布局（高地址在上）

```
高地址
  ─────────────────────────────────
  Buddy 静态元数据（各 zone 的 buddy_page 等）
  ─────────────────────────────────  per_cpu part end
  per-CPU 数据
  ─────────────────────────────────  _end（内核结束）
  内核镜像
  ─────────────────────────────────  0x100000
  BIOS 等不可用低端
  ─────────────────────────────────  0x0
低地址
```

#### aarch64 布局（高地址在上）

```
高地址
  ─────────────────────────────────
  Buddy 静态元数据
  ─────────────────────────────────  per_cpu part end
  per-CPU 数据
  ─────────────────────────────────  _end 向上 2M 对齐 + 6M（示意）
  DTB 映射区（DTB 最多 2M，可能未对齐，预留 4M）
  ─────────────────────────────────  _end 向上 2M + 2M
  空闲
  ─────────────────────────────────  _end 向上 2M + 4K
  PL011 UART
  ─────────────────────────────────  _end 向上 2M
  ─────────────────────────────────  _end
  内核镜像
  ─────────────────────────────────  0x40080000 / 0x40000000
  硬件保留等不可用
  ─────────────────────────────────  0x0
低地址
```

aarch64 在 boot 阶段会在 `boot_map_pg_table` 中映射内核、UART、以及 DTB 拷贝区；`map_end_virt_addr` 随这些映射推进，最终作为 `arch_init_pmm` 中“下一段可用物理起始”的参考。

#### 2.2.1 PMM 元数据超出 1G 时的额外映射

内核恒等映射在启动时通常只覆盖“第一段”1G 虚拟空间（即由 boot 的 L1 项指向的单一 L2 表所覆盖的 1G）。内核镜像与 per-CPU 区被约束在同一 1G 内；但 **PMM 管理数据**（section、Page 数组、各 zone 的 buddy 元数据）在物理上紧接在 per-CPU 之后，当机器物理内存很大时，这些元数据所需页数可能很多，**整段 PMM 区的物理地址可能越过第一个 1G**，若不再建映射，则通过 `KERNEL_PHY_TO_VIRT` 访问超出部分的 buddy/section 会缺页。

为此，在预留 PMM 区时不仅预留“数据页”，还预留**用于页表的 L2 表页**，并在 `arch_map_pmm_data_space`（`kernel/mm/pmm.c`）中分三阶段建立映射：

1. **仍在第一个 1G 内的部分**  
   从 `pmm_phy_start_addr` 到 `pmm_phy_start_round_up_1g = ROUND_UP(pmm_phy_start_addr, HUGE_PAGE_SIZE)` 的物理范围（若存在），仍落在 boot 已有的 L2 表（`L2_table`）内，直接对该区间以 2M 为单位调用 `arch_set_L2_entry`，建立恒等映射。

2. **为超出 1G 的虚拟空间增加 L1 项**  
   PMM 区在物理上占据的“1G 对齐段”数决定了需要的额外 L2 表数量。`calculate_pmm_space` 中：`L2_table_pages = total_pages / (HUGE_PAGE_SIZE / PAGE_SIZE) + 1`（即每 1G 数据约一页 L2 表，再加一页余量）。这些 L2 表页在 PMM 预留区的**最前面**分配，物理地址为 `pmm_l2_start .. pmm_l2_start + L2_table_pages * PAGE_SIZE`。  
   对每一页 L2 表（`pmm_l2_start_iter`），在 **L1 表**（`L1_table`）中新增一项：虚拟地址取 1G 对齐的 `pmm_phy_start_round_up_1g_iter`（从 `pmm_phy_start_round_up_1g` 起每次 +1G），物理地址指向该 L2 表页。这样，第一个 1G 之后的每一段 1G 虚拟地址都有对应的 L2 表，且这些 L2 表本身放在 PMM 区开头，已落在第一段 1G 内、可访问。

3. **在每张新 L2 表中建立 2M 映射**  
   对每张上述 L2 表，在其内填 2M 大页表项：将物理区间 `[pmm_phy_start_round_up_1g_iter, pmm_phy_start_round_up_1g_iter + HUGE_PAGE_SIZE)` 与同范围的虚拟地址（恒等映射）建立映射，直到覆盖 `pmm_phy_end`。

效果：PMM 数据区即使跨越多个 1G 物理段，也能通过 `KERNEL_PHY_TO_VIRT` 连续访问；额外开销仅为在 PMM 区头部预留并填写若干 L2 表页，并在 L1 中增加对应项，无需改动 L0 或更多层级。

### 2.3 预留与可用区计算

- **内核 + percpu**：  
  `phy_mm_init` 先调用 `arch_init_pmm` 得到 `per_cpu_phy_start`，再 `reserve_per_cpu_region(&per_cpu_phy_end)`，然后用 `memory_regions_reserve_region(kernel_phy_start, per_cpu_phy_end)` 从 `m_regions` 中扣掉 [kernel_start, per_cpu_end]。
- **PMM 区**：  
  根据“可用物理页数”和“section 数量”算 zone 元数据所需页数，再为每个 zone 算 buddy 所需页数（`pmm_calculate_manage_space`），并加上 L2 表所需页数（`calculate_pmm_space`），最后用 `memory_regions_reserve_region_with_length` 在可用区间中预留一段连续物理页，作为 PMM 数据区（L2 表 + section/page 数组 + 各 zone 的 buddy 管理结构）。
- 为避免 PMM 把“存 L2 表”的 2M 块拆成 4K 使用，会再 reserve 一段 `[pmm_data_phy_end, ROUND_UP(pmm_data_phy_end, MIDDLE_PAGE_SIZE)]`。

可用物理范围与总页数由 `calculate_avaliable_phy_addr_region` 遍历当前 `m_regions` 得到。

### 2.4 Zone 与 Section

Zone 表示一块逻辑物理内存区间，其下可挂多个 Section（zone 内一段连续物理页）；每个物理页对应一个 `Page`。一个 zone 可由多段不连续的物理区间组成，每段一个 section。（`include/rendezvos/mm/pmm.h`）

```c
typedef struct {
        struct list_entry section_list;   /* 链到 zone，串起该 zone 下所有 section */
        u64 zone_id;
        struct pmm *pmm;
        paddr upper_addr, lower_addr;
        size_t zone_total_pages;
        size_t zone_total_sections;
        size_t zone_pmm_manage_pages;
} MemZone;

struct mem_section {
        struct list_entry section_list;
        u64 sec_id;
        MemZone *zone;
        size_t page_count;
        paddr upper_addr, lower_addr;
        Page pages[];   /* 柔性数组：本 section 内所有物理页的 Page 描述 */
};

typedef struct {
        i64 ref_count;
        MemSection *sec;
        struct list_entry rmap_list;   /* 反向映射：链到映射该页的 nexus_node */
} Page;
```

- `split_pmm_zones` 根据可用物理范围为每个 zone 计算与各 memory region 的交集，得到 section 数量与总页数。  
- `generate_zone_data` 在已预留的 PMM 物理区间内依次放置 `MemSection` 及其 `Page pages[]`，并初始化 `Page.sec` 与 `rmap_list`。

**设计意图**：Zone 是高于具体分配器的抽象；**不同 zone 可采用不同分配器**（例如 ZONE_NORMAL 用 buddy，DMA/CMA 等可用线性或专用分配器），以便更灵活地适配硬件与策略，当前实现中仅 ZONE_NORMAL 使用 buddy。若将来支持多 zone（如 DMA），`get_free_page`/`pmm_alloc` 可能需扩展 zone 参数及多 zone 下的加锁策略，预留为 TODO。

### 2.5 Buddy 分配器（每 Zone）

当前仅 ZONE_NORMAL 使用 buddy，实现在 `kernel/mm/buddy_pmm.c`，结构在 `include/rendezvos/mm/buddy_pmm.h`。

#### 2.5.1 设计意图：树状数组与“每阶一个链表”的取舍

本实现把 buddy 设计为**树状数组**：用**一块连续数组**同时表示“逻辑上的 buddy 树”和“各阶空闲链表”的节点，通过**下标与 order 的算术关系**推导父子/伙伴，而不为树单独维护指针，也不为每个 order 再开一套独立链表节点。

- **树落在数组上**  
  - `struct buddy` 内有一个 `struct buddy_page *pages`，指向长度为 `zone->zone_total_pages` 的连续数组，**按 zone 内物理页的线性顺序**排列（即按 section 遍历得到的 index）。  
  - 每个物理页在 zone 内有一个唯一下标 `index`，对应 `pages[index]`。  
  - 一块大小为 2^k 页的“块”在逻辑上对应 buddy 树的一棵子树；在实现上，这块由**连续下标** `[i, i + 2^k)` 组成。块的“代表元”是**块首**，即 `pages[i]`；同一块内其余页在分配或合并过程中**不单独挂到任何链表**，仅通过其 `order == -1` 表示“属于某块的非首页”。  
  - 因此：**树结构是隐式的**——左半块首在 `i`，右半块首在 `i + 2^{k-1}`；伙伴块首的下标由 `i ± 2^k` 得到（见下）。不需要 parent/child 指针。

- **伙伴关系由下标与 ppn 推导**  
  - 释放时要从当前块首的 `index` 和 `order` 找到“同阶伙伴”的块首。伙伴块与当前块在 2^k 对齐意义下相邻：当前块首的 ppn 满足 `(ppn >> k) % 2 == 0` 则伙伴在右侧，`buddy_index = index + (1 << k)`；否则在左侧，`buddy_index = index - (1 << k)`。代码中通过 `(insert_node->ppn >> tmp_order) % 2` 区分左右，从而用**纯算术**得到 `buddy_index`，无需额外指针。

- **每阶只保留一个链表头，节点复用同一数组**  
  - 每个 order 有一个 `struct buddy_bucket`，其中只有一条空闲链表 `avaliable_frame_list`。  
  - **空闲块的“代表元”**（块首的那个 `buddy_page`）挂在该阶的 `avaliable_frame_list` 上，通过其自身的 `page_list` 串起来。也就是说：**链表节点就是 `pages[]` 里的元素**，没有为“每个 order 再开一套”单独的链表节点或额外分配。  
  - 这样做的效果是：**节省了“为每一个 order 都开一条由独立节点构成的链表”的开销**——各阶共享同一块 `buddy_page` 数组，只是不同阶的“块首”会挂到不同阶的桶里；数组既描述块在 zone 内的位置（ppn、下标），又作为空闲链表的节点。

- **代价：每个节点需记录当前 order**  
  - 每个 `struct buddy_page` 需要维护 `order`：若该页是某块的首页且块空闲，则 `order` 为该块的阶（0..BUDDY_MAXORDER）；若该页已分配或是块内非首页，则 `order == -1`。  
  - 这样在分配时可以从桶里取出一个“块首”，根据其 `order` 知道块大小；在释放时可以根据当前页的 index 和其所在块的首页的 order，配合 ppn 的奇偶，正确找到伙伴并合并。  
  - 因此设计上的权衡是：**多一个 order 字段（以及 list 指针）的 per-page 开销，换来无需为树和每阶链表再做一套独立结构**。

#### 2.5.2 数据结构与阶数

阶数 `BUDDY_MAXORDER = 9`，最大块 2^9 页（2MiB）。元数据定义（`include/rendezvos/mm/buddy_pmm.h`）：

```c
#define BUDDY_MAXORDER 9

struct buddy_page {
        struct list_entry page_list;   /* 挂到某阶 bucket 的 avaliable_frame_list */
        ppn_t ppn;
        i64 order;   /* 0..9：该页为某空闲块首且块阶；-1：已分配或块内非首页 */
};

struct buddy_bucket {
        u64 order;
        u64 aval_pages;
        struct list_entry avaliable_frame_list;   /* 该阶空闲块首的 buddy_page 链表 */
};

struct buddy {
        /* PMM_COMMON: pmm_init, pmm_alloc, pmm_free, zone, spin_ptr, total_avaliable_pages, ... */
        u64 buddy_page_number;
        struct buddy_page *pages;   /* 长度为 zone->zone_total_pages 的数组，按 zone 内页序 */
        struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
};
```

- 管理空间页数：`pmm_calculate_manage_space(zone_total_pages)` = `ROUND_UP(zone_total_pages * sizeof(struct buddy_page), PAGE_SIZE) / PAGE_SIZE`。

#### 2.5.3 初始化（`pmm_init`）

- 按 zone 内 section 顺序遍历所有物理页，在 `bp->pages[index]` 中填好 `ppn`（由 section 的 `lower_addr` 与页在 section 内偏移算出），并 `INIT_LIST_HEAD(&bp->pages[index].page_list)`。  
- 先假定所有页都是 order 0 的块：全部挂到 `buckets[0].avaliable_frame_list`，并设 `order = 0`。  
- 再从 order 1 到 BUDDY_MAXORDER 做“自底向上合并”：遍历下标 `index`，若以 `index` 为首、长度为 2^{order-1} 的块与以 `index + (1<<(order-1))` 为首的同长块**在物理上连续**（即两块的 ppn 相邻）且**对 2^order 对齐**（`ALIGNED(bp->pages[index].ppn, (1<<order))`），则把这两块合并：  
  - 从 order-1 桶摘掉这两块的首页，  
  - 将右块首页的 `order` 置为 -1（不再作为块首），  
  - 左块首页的 `order` 加一，并挂到 `buckets[order].avaliable_frame_list`。  
- 这样初始化后，数组中的“块”与 buddy 树的划分一致，且只有每块的首页在对应阶的桶里，其余页 order 为 -1。

#### 2.5.4 分配（`pmm_alloc` / `pmm_alloc_zone`）

- 请求 `page_number` 页，计算 `alloc_order = log2_of_next_power_of_two(page_number)`。  
- 从 `alloc_order` 到 `BUDDY_MAXORDER` 找第一个非空桶，从该桶的 `avaliable_frame_list` 上取下一块首 `del_node`。  
- 若该块阶 `tmp_order` 大于 `alloc_order`，则反复“对半拆”：  
  - 左半块首为 `left_child = del_node`，右半块首为 `right_child = del_node + (1 << (tmp_order - 1))`（**完全由数组下标推导**）。  
  - 将当前大块从 `buckets[tmp_order]` 摘下，把 `left_child`、`right_child` 的 `order` 设为 `tmp_order - 1`，并挂回 `buckets[tmp_order - 1]`，然后 `tmp_order--`；若已等于 `alloc_order` 则停止拆分，将整块中所有页的 `order` 置为 -1，从桶中摘下块首，并更新 `Zone_phy_Page` 对应 `Page` 的 `ref_count`。  
- 返回块首的 `ppn`；调用方得到的是连续物理页的起始 ppn。

#### 2.5.5 释放（`pmm_free` / `pmm_free_one`）

- `pmm_free` 按请求的 `page_number` 得到 `alloc_order`，对块内每个 ppn 调用 `phy_Page_ref_minus`；若某页 `ref_count` 仍 > 0 则不再归还。否则对每个 ppn 调用 `pmm_free_one`。  
- `pmm_free_one`：用 `ppn_Zone_index` 得到该 ppn 在 zone 内的下标，从而得到 `insert_node = &bp->pages[index]`。从 order 0 起尝试与伙伴合并：  
  - 用 `(insert_node->ppn >> tmp_order) % 2` 决定伙伴在左还是右，得到 `buddy_index = index ± (1 << tmp_order)`；  
  - 若伙伴在 zone 内、且与当前块在物理上连续、且伙伴块首的 `order == tmp_order`（即伙伴整块空闲），则从两桶摘下这两块，合并为一块，`insert_node` 更新为合并后块首（`MIN(index, buddy_index)` 对应的页），`tmp_order++`，继续尝试与上一阶伙伴合并；  
  - 若无法合并，则将当前块首的 `order` 设为 `tmp_order`，挂到 `buckets[tmp_order].avaliable_frame_list`，并增加 `total_avaliable_pages`。

PMM 层对外接口为 `pmm_alloc` / `pmm_free`，带 zone 的锁（如 `pmm_spin_lock[zone_id]`）由调用方在需要时使用（如 Nexus、arch_map_pmm_data_space 等）。若从 buddy 取不到页，后续应在 Nexus 或上层做 **swap**（置换），以腾出物理页再分配，当前为 TODO。

---

## 3. 虚拟内存与页表

### 3.1 内核恒等映射

内核使用线性偏移将物理地址映射到虚拟地址：`KERNEL_PHY_TO_VIRT(p) = p + KERNEL_VIRT_OFFSET`，即恒等映射的一段高地址区间。  
因此从 PMM 分配到的物理页，若给内核用，可直接用 `KERNEL_PHY_TO_VIRT(PADDR(ppn))` 访问，前提是该物理范围已在页表中建立 2M 或 4K 映射。  
Boot 阶段各架构在启动代码中建立内核镜像、percpu、PMM 数据区等的映射；运行期通过 `map` 在对应 level 填 L2/L3 表项，维持这段恒等映射的连续性。

### 3.2 页表自映射方案（非一次性全映射）

修改页表时，需要读写 L0/L1/L2/L3 表页，但这些表页本身未必已映射。本设计采用“自映射”的固定虚拟区，在修改时临时把当前要改的那一级表页映射到固定虚拟地址，改完即可，无需一开始就映射所有页表页。

**固定虚拟区与每 CPU Map Handler**（`include/rendezvos/mm/map_handler.h`）：

```c
#define map_pages 0xFFFFFFFFFFE00000   /* 最后 2MiB，映射用页的虚拟基址 */

struct map_handler {
        u64 cpu_id;
        vaddr map_vaddr[4];      /* 本 CPU 的 4 个临时映射虚址，对应 L0/L1/L2/L3 访问 */
        ppn_t handler_ppn[4];    /* 从 PMM 要的 4 页，用于 map() 时创建新页表页 */
        struct pmm *pmm;
};
```

- 每个 CPU 的 4 个虚址为 `map_pages + cpu_id * 4 * PAGE_SIZE`。**使用 4 个 entry 而非单 entry 反复复用**，是为了避免单页多次映射不同表页带来的 **TLB flush 开销**；每级固定占一页，修改时各自映射一次即可。
- 2MiB 空间共 512 个 4K 页，每 CPU 占 4 页，故 **最多支持 128 个 CPU**；map_handler 与 per-CPU 的 allocator/nexus 数量受此上限约束。`init_map()` 为每 CPU 分配 4 个 ppn 填入 `handler_ppn[]`，并设定上述 `map_vaddr[]`。

**自映射的建立**（`kernel/mm/map_handler.c` 中 `sys_init_map`，仅在 BSP 执行一次）：

- 在**当前内核页表**的 L0 中，将 `map_pages` 对应项指向一张 L1 表（该 L1 表物理地址为 `MAP_L1_table` 的物理地址）。
- 该 L1 表中，`map_pages` 对应项指向 `MAP_L2_table`。
- 该 L2 表中，`map_pages` 对应项指向 `MAP_L3_table`。
- 即：访问 `map_pages` 起 4K 对齐的地址，依次通过 L0→L1→L2→L3，最终落在 `MAP_L3_table` 的某个 PTE 上。

`MAP_L1_table`、`MAP_L2_table`、`MAP_L3_table` 在 `kernel/mm/map_util_page.S` 中定义为 3 个 4K 的零页（boot 阶段或早期分配），物理上连续，用于根页表到 L3 的这条“自映射链”。

**修改页表时的用法**（`map()` 内）：

- 通过 `util_map(下一级表物理地址, handler->map_vaddr[level])` 把“下一级表页”映射到当前 CPU 的 `map_vaddr[level]`（即写入 `MAP_L3_table` 的某个 PTE，因为自映射后该虚地址对应 L3 表的一项）。
- 然后通过 `(union Lx_entry *)handler->map_vaddr[level]` 访问并修改该级表；若该级不存在则用 `handler_ppn[]` 分配新页、清零、填上级表项，再 `util_map` 到对应 `map_vaddr`。
- 因此不需要在系统运行前把全部页表都映射好，而是“按需映射到固定 4 个虚页”，修改完即可，实现动态、节省页表占用。

### 3.3 map / unmap 与层级

- `map(VSpace*, ppn, vpn, level, eflags, handler, lock)`：在 `vs` 的页表中建立 `vpn` → `ppn` 的映射；`level == 2` 表示 2M 页，`level == 3` 表示 4K 页。内部按 L0→L1→L2→L3 逐级用 `util_map` 与 `handler_ppn` 创建缺失表页并写表项。
- `unmap`：清除对应 vpn 的映射，可选地将同一 vpn 指向新的物理页（用于部分高级用法）。
- 内核恒等映射区的新页通过 `map()` 在对应 level 插入 2M 或 4K 项；用户态由 Nexus 在用户 vspace 上调用 `map`/`unmap`。

---

## 4. Nexus：每 CPU 虚拟页分配器

### 4.1 设计动机：为何按“页”记录而非传统 VMA 区间

主流 ISA 的 MMU 均采用**基于基数树（多级页表）的硬件结构**，软件侧若再维护一套与页表并行的“区间”抽象（如 Linux 的 VMA 树），会带来双重抽象与复杂的同步需求。SOSP 2025 的 **CortenMM**（*Efficient Memory Management with Strong Correctness Guarantees*）[^1] 指出：传统做法在软件层维护 VMA 等区间结构，并与硬件页表保持一致，往往需要**细粒度、多层次的锁**，在多核下容易成为扩展瓶颈；而直接围绕页表设计、采用可扩展的加锁协议，能同时获得性能与可验证性。

[^1]: CortenMM: Efficient Memory Management with Strong Correctness Guarantees. SOSP 2025. 采用单一页表层级、事务式接口与可扩展加锁，避免 VMA 与页表双抽象带来的锁竞争。RendezvOS 的设计与之呼应：**不采用 Linux 式的 VMA 区间 + 复杂 mm 锁**，而是用 **Nexus** 按**页/大页**粒度记录“虚拟页 ↔ 物理页”的映射，并配合**每 CPU 一个 Nexus** 与分层锁，尽量把竞争限制在单核或单 vspace 内。

- **按页而非按区间**：Nexus 中每条记录对应一段**固定大小**的映射（4K 或 2M），用红黑树按虚地址组织，便于查找与释放，但**不做区间的合并/分裂**（如 Linux 的 vma 合并）。这样与硬件页表“按页/大页映射”的粒度一致，无需在软件层维护区间代数，也避免为区间操作引入细粒度锁。
- **每 CPU 一个 Nexus**：内核态分配时，每个 CPU 使用本 CPU 的 `nexus_root` 与 `map_handler`，分配出的虚拟页记录在本 CPU 的树中，**同核上的分配/释放不与其他核争用同一棵 nexus 树**；只有访问共享的 PMM 或同一 vspace 时才会碰到跨核锁。

以下先说明 Nexus 的角色与数据结构，再详述**锁的使用**与**多核下同一 vspace 仍存在的性能瓶颈**。

### 4.2 角色与绑定

- **内核**：每个 CPU 有一个 `nexus_root`（`per_cpu(nexus_root, cpu_id)`），对应一个 `map_handler` 和共享的 `root_vspace`。内核的 `get_free_page`/`free_pages` 使用本 CPU 的 nexus_root。
- **用户**：每个 VSpace（进程地址空间）在**某一个** Nexus 下拥有一个“vspace 根节点”；该进程的用户态虚拟页分配记录在该 vspace 的子树中，管理页放在内核空间，数据页映射在用户空间。

地址判断：若虚拟地址 `>= KERNEL_VIRT_OFFSET` 则走内核 Nexus；否则按当前 vspace 找到其所属 nexus 的 vspace 根节点再操作。

VSpace 与 nexus 的关联（`include/rendezvos/mm/vmm.h`）：

```c
typedef struct {
        paddr vspace_root_addr;   /* 页表根物理地址 */
        u64 vspace_id;
        spin_lock vspace_lock;         /* 传给 map/unmap，保护页表修改 */
        cas_lock_t nexus_vspace_lock;   /* 保护本 vspace 在 nexus 内的子树操作 */
        void *_vspace_node;             /* 指向该 vspace 在某个 nexus 下的根 nexus_node */
} VSpace;
```

### 4.3 数据结构概要

`nexus_node` 用 union 区分三种角色：普通映射节点、vspace 根节点、管理页的 0 号节点。每页可放 `NEXUS_PER_PAGE = PAGE_SIZE / sizeof(nexus_node)` 个节点；**0 号描述本页，1 号常为 vspace 根**（或 per-CPU 的 nexus 树的根，即内核用的 nexus_root）。这种布局使每个管理页**自包含**：同一页内既有描述本页的 0 号、又有挂在该页上的树根或子树根（1 号），其余 slot 通过 `manage_free_list` 与页内 `_free_list` 分配，无需依赖外部元数据即可完成该页上的分配与释放，形成 nexus 节点的自包含机制。（`include/rendezvos/mm/nexus.h`）

```c
struct nexus_node {
        struct list_entry manage_free_list;  /* 管理页链表：有空闲 slot 的页 */
        struct list_entry _free_list;        /* 本页内空闲 nexus_node 的链表 */
        struct list_entry _vspace_list;      /* 挂到 vspace 根下，串起该 vspace 所有映射页 */
        VSpace *vs;
        union {
                /* 普通映射节点 或 管理页 0 号 */
                struct {
                        struct rb_node _rb_node;
                        ENTRY_FLAGS_t region_flags;   /* 含 PAGE_ENTRY_HUGE(2M) 等 */
                        vaddr addr;
                        union {
                                u64 page_left_nexus;     /* 管理页 0 号：本页剩余空闲 slot 数 */
                                struct list_entry rmap_list;  /* 普通节点：挂到 Page.rmap_list */
                        };
                };
                /* vspace 根节点 或 nexus 根节点 */
                struct {
                        struct rb_node _vspace_rb_node;
                        struct rb_root _rb_root;        /* 本 vspace 内按 addr 的映射树 */
                        struct map_handler *handler;
                        struct rb_root _vspace_rb_root;  /* 仅根：按 vspace_root_addr 的 vspace 树 */
                        cas_lock_t nexus_lock;
                };
        };
};
#define NEXUS_PER_PAGE (PAGE_SIZE / (sizeof(struct nexus_node)))
```

- 普通节点：`addr` + `region_flags` 表示一段 4K 或 2M 映射，`rmap_list` 链到物理页做反向映射。  
- vspace 根：`_rb_root` 为该 vspace 的映射红黑树，`handler` 指向本 CPU 的 map_handler；全局 nexus 根的 `_vspace_rb_root` 串起所有 vspace 根。

### 4.4 锁策略与“尽量每 CPU”的实现

为实现“尽量每 CPU、少争用”的 Nexus，代码中使用了多类锁，并尽量缩短持锁范围、区分“全局 nexus 元数据”与“单 vspace 树”的锁。

| 锁 | 作用范围 | 用途 |
|----|----------|------|
| **nexus_lock**（cas_lock） | 单个 nexus_root | 保护该 nexus 的 **vspace 集合**（`_vspace_rb_root`）：创建/删除/迁移 vspace 根节点时短时持有；`get_free_page`/`free_pages` 在用户态路径中仅用于按 `vs->vspace_root_addr` 查找 vspace_node，查完即放。 |
| **nexus_vspace_lock**（cas_lock） | 单个 VSpace | 保护**该 vspace 在 nexus 内的子树**及与之相关的分配/释放流程：在对应 vspace 的 `_rb_root` 上做插入、删除、查找，以及 `_take_range`、`_unfill_range`、管理页/nexus_node 的分配与归还时持有。内核路径用 `nexus_root->vs->nexus_vspace_lock`（内核 vspace）；用户路径用 `vspace_node->vs->nexus_vspace_lock`。 |
| **vspace_lock**（spin_lock*） | 单个 VSpace | 传给 `map()`/`unmap()`，在**修改该 vspace 的页表**时由 map_handler 内部使用，保证同一时刻只有一个线程在改该地址空间的页表。 |
| **pmm 锁**（MCS，per zone per CPU） | 每个 zone | 调用 `pmm_alloc`/`pmm_free` 时，用**当前 handler 的 cpu_id** 取该 zone 的 per-CPU MCS 锁（`per_cpu(pmm_spin_lock[zone_id], handler->cpu_id)`），以减少同一 zone 上的锁竞争形态。 |

**内核路径（`_kernel_get_free_page` / `_kernel_free_pages`）**  
- 仅使用**本 CPU 的** nexus_root，整段分配/释放过程只持本 vspace 的 `nexus_vspace_lock`（以及 map 时的 `vspace_lock`）；不涉及其他 CPU 的 nexus 树。  
- 唯一跨 CPU 共享的是 **PMM**：分配/释放物理页时短暂持 zone 的 MCS 锁，持锁时间仅为 pmm_alloc/pmm_free 内的一小段，从而在“多核同时做内核页分配”时，主要竞争点在 PMM，而非 nexus 树。

**用户路径（`get_free_page` / `free_pages`，用户虚址）**  
- 先用 `nexus_lock` 在 `_vspace_rb_root` 上按 `vs->vspace_root_addr` 找到 vspace_node，找到后立即释放 `nexus_lock`，再在**该 vspace 节点**上做 `_user_take_range` / `user_fill_range` / `user_unfill_range` / `_user_release_range`。  
- 上述对“单 vspace 树”的操作全程持 **nexus_vspace_lock**（该 vspace 的），因此**同一 vspace 上的并发分配/释放会串行化**；但不同 vspace（不同进程）之间仅在使用 nexus_lock 查找时可能短暂交错，不会长时间共持一把锁。

**设计意图小结**：通过“每 CPU 一个 nexus_root + 每 vspace 一把 nexus_vspace_lock”，使得**不同 CPU 上的内核分配互不争用 nexus 树**，**不同进程的用户分配也只在查找 vspace 时共用 nexus_lock**；代价是同一进程（同一 vspace）内多线程会争用该 vspace 的 nexus_vspace_lock 与 vspace_lock。

### 4.5 多核下同一 vspace 的性能局限

当**多个线程分属不同 CPU、但共享同一 VSpace**（同一进程的地址空间）时，会出现以下情况：

- **用户态 get_free_page / free_pages**：所有操作都要先找到该 vspace 对应的 vspace_node，再持 **vs->nexus_vspace_lock** 进行红黑树操作、PMM 分配/释放以及 `map`/`unmap`。因此，同一进程内多线程并发做虚拟页分配/释放时，会**串行化在这把 nexus_vspace_lock 上**，无法随 CPU 数扩展。
- **页表修改**：`map`/`unmap` 内部使用 **vs->vspace_lock**，同一 vspace 的任意映射变更都会互斥。多线程同时为同一地址空间建映射或拆映射时，同样会在这把锁上排队。
- **PMM**：即使用 per-CPU 的 MCS 锁，zone 仍是全局的；若多核同时向同一 zone 要页，仍会在 pmm 层产生竞争，只是锁的形态比单一全局自旋锁更利于扩展。

因此，当前设计在“每 CPU 一个 Nexus、按页记录、少用 VMA 式区间锁”上取得了**内核路径与多进程用户路径**的较好隔离，但在**单进程多线程、高并发操作同一地址空间**的场景下，仍存在 **nexus_vspace_lock 与 vspace_lock 的串行化瓶颈**；若需进一步扩展，可考虑按 vspace 内地址区间或按线程/CPU 做更细的划分（例如每 vspace 多棵子树、或更细粒度锁），与 CortenMM 等工作中“可扩展加锁协议”的方向一致。

### 4.6 get_free_page / free_pages 与反向映射

- **内核**：`_kernel_get_free_page` 持 `nexus_vspace_lock` 后向 PMM 要页、`_take_range` 分配 nexus_node、对每节点 `map()`（内部用 `vspace_lock`）、`link_rmap_list`；`_kernel_free_pages` 持同一把 `nexus_vspace_lock` 后查树、`_unfill_range`（unmap + pmm_free）、删除节点并 `nexus_free_entry`。  
- **用户**：`get_free_page` 先短持 `nexus_lock` 查 vspace_node，再在 vspace_node 上做 `_user_take_range`（持 `nexus_vspace_lock`）与 `user_fill_range`（持锁下逐页 pmm_alloc + map）；`free_pages` 先查 vspace_node，再 `user_unfill_range` 与 `_user_release_range`，均持该 vspace 的 `nexus_vspace_lock`。  
- **反向映射（rmap）**：每个 `Page` 的 `rmap_list` 链起所有映射到该页的 nexus_node；`unfill_phy_page` 等会遍历 rmap 并逐项 unmap。同一 2M 对齐物理范围内不允许 2M 与 4K 混用（会报错）。**Shared memory**（多地址空间共享同一物理页、引用计数与 rmap 协同）是后续必须支持的能力，用于 swap/COW 等，当前为 TODO。

**用户态多 vspace 与迁移**：**每个 vspace 单独占一页**作为其 nexus 根所在的管理页；新 VSpace 通过 `nexus_create_vspace_root_node` 在某个 nexus_root 的 `_vspace_rb_root` 上挂一个 vspace 根节点（持 `nexus_lock`），该节点位于新分配的这一页内（1 号 slot 为 vspace 根）。这样不同 vspace 的 nexus entry 分配互不干扰，避免多 vspace 共用一棵树时 entry 争用。删除进程时 `nexus_delete_vspace` 持同一把锁摘下该节点，并持该 vspace 的 `nexus_vspace_lock` 释放其下所有映射与 nexus 节点。线程迁移时可通过 `nexus_migrate_vspace` 将某 vspace 从源 nexus 移到目标 nexus（先持源 nexus_lock 摘下，再持目标 nexus_lock 挂上），保证释放时仍能找到对应的 handler/allocator。

**用户态相关设计意图与 TODO（须保留）**：  
- **Lazy alloc**：用户态虚拟页可先占区间、后补物理映射；分配时需能指明是否立即分配物理页，释放时若无物理页则只收虚拟区间。  
- **Map override**：页表项已存在时，若允许 override 则覆盖并返回原 ppn，否则报错，以便上层决定是否 unmap 再 map 或复用。  
- **区间合并/分裂**：当前按 4K/2M 单页记录、不做区间合并与分裂；若将来在用户态维护连续区间（例如配合 page fault 的区间信息），需要定义合并/分裂语义及与现有 nexus 记录的配合方式。  
以上均为设计意图或待实现（TODO），实现时需与现有 get_free_page / free_pages / user_fill_range / user_unfill_range 接口一致。

---

## 5. 内核对象分配器（kmalloc）

### 5.1 层次与接口

- 上层：`struct allocator`，提供 `m_alloc(allocator, size)` / `m_free(allocator, ptr)`。
- 实现为 `struct mem_allocator`（`include/rendezvos/mm/kmalloc.h`），内部持有一个 `nexus_node* nexus_root`（即本 CPU 的 nexus），和 12 个 `mem_group`、一个 `page_chunk_root`（红黑树）、以及每 CPU 的 buffer 无锁队列等。
- 分配时：小对象从对应 slot 的 group 取 object；若需新页则通过 `get_free_page(nexus_root, ...)` 向 Nexus 要页（Nexus 再向 PMM 要）。  
- 释放时：若指针是 4K 对齐的“整页分配”，则走 `free_pages` 并查 `page_chunk_root` 确定页数；否则按 object 归还到对应 chunk/group。若对象属于其他 CPU 的 allocator（通过 `allocator_id`），则放入目标 CPU 的 buffer 无锁队列，由目标 CPU 在后续 free 时统一回收。

### 5.2 档位与 chunk 结构

12 档 slot 大小（字节）：`slot_size[] = { 8, 16, 24, 32, 48, 64, 96, 128, 256, 512, 1024, 2048 }`。每个 allocator 有 12 个 `mem_group`，每组对应一档；chunk 内按 `object_header + payload` 密集排列。**Chunk 的页数（`PAGE_PER_CHUNK`，当前为 4）不是随意定的**：必须保证在每种 slot 下，chunk 内每个 object 的起始地址都**不是 4K 页对齐**的，这样在 free 时可以根据“是否 4K 对齐”可靠地区分“整页分配”与“从 chunk 里分配的 object”，避免把 chunk 返回的指针误判为整页分配而错误地走 `free_pages`。4 页是在满足上述约束下计算得到的结果。

```c
#define MAX_GROUP_SLOTS 12
#define PAGE_PER_CHUNK  4
#define CHUNK_MAGIC     0xa11ca11ca11ca11c

struct page_chunk_node {   /* 整页分配时记在 page_chunk_root 红黑树里，按 page_addr 查页数 */
        struct rb_node _rb_node;
        vaddr page_addr;
        i64 page_num;
};

struct object_header {
        struct list_entry obj_list;
        i64 allocator_id;
        ms_queue_node_t msq_node;   /* 跨 CPU 释放时挂到目标 allocator 的 buffer_msq */
        char obj[];                 /* 实际返回给调用方的 payload */
};

struct mem_chunk {
        u64 magic;
        int allocator_id, chunk_order;   /* chunk_order = slot 索引 */
        int nr_max_objs, nr_used_objs;
        struct list_entry chunk_list;   /* 挂到 group 的 full_list / empty_list */
        struct list_entry full_obj_list; /* 已分配 object 的链表 */
        struct list_entry empty_obj_list;
        char padding[];
};

struct mem_group {
        int allocator_id, chunk_order;
        size_t free_chunk_num, empty_chunk_num, full_chunk_num;
        struct list_entry full_list;   /* 部分满或全满的 chunk */
        struct list_entry empty_list;  /* 空 chunk，可跨 group 复用 */
};

struct mem_allocator {
        /* MM_COMMON: init, m_alloc, m_free, allocator_id */
        struct nexus_node *nexus_root;
        struct mem_group groups[MAX_GROUP_SLOTS];
        struct rb_root page_chunk_root;   /* 整页分配的 (page_addr, page_num) */
        ms_queue_t *buffer_msq;           /* 其他 CPU 释放过来的 object 队列 */
        atomic64_t buffer_size;
        cas_lock_t lock;
};
```

- 大于 2048 字节的请求向 Nexus 要整页（或连续多页），在 `page_chunk_root` 中记录 `(page_addr, page_num)`，free 时按虚址查得页数再 `free_pages`。通过 Nexus 的整页请求**单次不超过 2M**（与 buddy 最大块 2^9 页一致）；更大需求需多次分配或由上层拆分。

### 5.3 多核与跨 CPU 释放

- 每个 CPU 一个 `kallocator`（mem_allocator），`allocator_id` 一般为 cpu_id；**allocator（与 map_handler）数量受 128 上限约束**，与 3.2 节中“每 CPU 占 4 页、2MiB 最多 128 CPU”一致。  
- 释放时若发现 `object_header.allocator_id != 当前 allocator_id`，则不直接在本 allocator 释放，而是把该 object 挂到目标 allocator 的 `buffer_msq` 无锁队列。目标 CPU 在每次执行 free 时会顺带消费该队列，把对象还到正确的 chunk/group，避免跨 CPU 锁 chunk。  
- 整页分配通过 `page_chunk_root` 红黑树按虚地址查找，得到页数后调用 `free_pages`；该树与 nexus 的映射关系一致，由 allocator 自己维护，不依赖 Nexus 记录“上层要了多少页”。

---

## 6. 与代码的对应关系（便于检索）

| 主题 | 主要文件 |
|------|----------|
| 物理内存区域与预留 | `kernel/mm/pmm.c`（m_regions, phy_mm_init, split_pmm_zones, generate_zone_data） |
| PMM 超 1G 时的 L1/L2 额外映射 | `kernel/mm/pmm.c`（calculate_pmm_space, arch_map_pmm_data_space） |
| x86_64 内存发现 | `arch/x86_64/mm/pmm.c`（arch_init_pmm, multiboot mmap） |
| aarch64 内存发现与 boot 映射 | `arch/aarch64/mm/pmm.c`，`arch/aarch64/boot/boot_map.c` |
| Zone / Section / Page | `include/rendezvos/mm/pmm.h` |
| Buddy | `kernel/mm/buddy_pmm.c`，`include/rendezvos/mm/buddy_pmm.h` |
| 页表自映射与 map | `kernel/mm/map_handler.c`，`include/rendezvos/mm/map_handler.h`，`kernel/mm/map_util_page.S` |
| Nexus 与锁（nexus_lock, nexus_vspace_lock, vspace_lock） | `kernel/mm/nexus.c`，`include/rendezvos/mm/nexus.h` |
| kmalloc | `kernel/mm/kmalloc.c`，`include/rendezvos/mm/kmalloc.h` |
| 虚拟 MM 与 per-CPU 初始化 | `kernel/mm/vmm.c`（virt_mm_init, init_map, init_nexus, kinit） |

---

*本文档以当前代码为准；若与历史设计或注释有出入，以代码实现为准。*
