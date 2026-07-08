# RendezvOS 内存系统设计文档

> **文档角色：** 子系统参考（maintained）  
> **入口：** [`USING_CORE.md`](USING_CORE.md)（外部调用方）· [`GUIDE.md`](GUIDE.md) · [`README.md`](README.md)  
> **相关：** [`cache&tlb.md`](cache&tlb.md)

本文档描述 RendezvOS 内存子系统的分层设计与实现，与当前代码保持一致。整体分为：物理内存管理（PMM / zone / buddy）、虚拟内存与页表自映射（Map Handler）、**每地址空间 radix tree**（用户 VA 真源 + 内核 `root_vspace` 元数据），以及 **per-CPU kmalloc**（小对象 + 经 `root_vspace` 的整页路径）。

> **历史说明：** 早期版本有独立 “Nexus” 中间层；已移除。下文凡提及 “整页虚拟分配” 均指 **radix + `map()`**，用户路径经 **`mm_user_utils_*`**，内核堆经 **`kmalloc` → `root_vspace`**（**不**走 `mm_user_utils`，因其拒绝 `&root_vspace`）。

**调用方（core 外代码）请先读本节「Caller contract」，再读后文实现细节。**

---

## 0. Caller contract（core 外调用方）

本节约定 **公开 MM API 的使用顺序与锁层级**，不描述任何特定 OS 人格的策略（mmap 语义、COW 策略等由调用方决定）。

### 0.1 核心对象

| 对象 | 头文件 | 含义 |
|------|--------|------|
| `VSpace` | `mm/vmm.h` | 地址空间：radix 元数据 + 页表根 + ASID/refcount |
| `struct map_handler` | `mm/map_handler.h` | **当前 CPU** 的页表修改器；取 `&percpu(Map_Handler)` |
| `percpu(current_vspace)` | `mm/vmm.h` | **当前 CPU** 正在运行的 `VSpace*`（调度切换时更新） |
| Radix API | `mm/vmm_radix_tree.h` | 用户 VA 区间的真源（映射记录、锁、fault） |
| `mm_user_utils_*` | `mm/mm_user_utils.h` | 多后端编排（radix + PTE + PMM），**非**随意封装 |

### 0.2 锁：L0（big）与 L2（small）

| 层级 | API | 作用 |
|------|-----|------|
| **L0** | `vmm_radix_tree_lock_range_big` / `unlock_range_big` | 串行化跨 512 GiB 片的 radix 元数据访问 |
| **L2** | `vmm_radix_tree_lock_range_small_with_big_locked` / `unlock_range_small` | 在 **已持 L0** 的区间上，对 VA 带做 insert/delete/query |

**规则：**

- 遍历、按区间发现占用页：先 **L0** 锁覆盖搜索范围，再用 `find_first_occupied_leaf` / `find_first_occupied_interval`。
- `mm_user_utils_*`：**假定调用方已持 L0**；函数内部只拿 L2，见 `mm_user_utils.h` 文件头注释。
- 不得在持 PMM 锁时嵌套调用 `pmm_alloc`（`memory.md` §2 与 `map_handler` 注释）。

### 0.3 按场景选 API

| 场景 | 推荐 API | 说明 |
|------|----------|------|
| 新建用户地址空间 | `create_vspace` → `register_vspace` | 任务绑定 `Tcb_Base->vs` |
| 复制地址空间（fork 类） | `clone_vspace(src, &dst, flags)` | `VSPACE_CLONE_F_*` 见 §0.4 |
| 清空用户映射（exec 类） | `vspace_clear_user_mappings(vs, &percpu(Map_Handler), true)` | 保留内核高半部；调用前任务内无其他线程跑在此 `vs` 上 |
| 映射 ELF PT_LOAD | `load_elf_to_vs`（`thread_loader.h`） | 内部走 radix/map |
| 分配连续用户页并清零 | `mm_user_utils_set_range_and_fill` | 需先 L0；区间不得与已有 insertable 重叠 |
|  demand-fill 单页（已有 LAZY 叶） | `mm_user_utils_fill_page_with_exist_range` | 需先 L0 |
| 拆掉连续用户映射 | `mm_user_utils_clean_range_and_unfill` 等 | 见 `mm_user_utils.h` |
| 统一改一段 VA 的 flags | `mm_user_utils_set_range_flags` | 区间须已 VALID 且 flags 一致 |
| 低层插入/删除 radix 记录 | `vmm_radix_tree_insert_range` / `leaf_bind` / `leaf_unbind` | 在 L0+L2 契约下使用 |
| 查询映射 | `vmm_radix_tree_query_range` | `RADIX_RL_QUERY_OR_CHANGE` |
| 页故障处理 | fault 路径 + radix `VALID`/COW 叶 | 见 [`trap.md`](trap.md) `TRAP_CLASS_PAGE_FAULT` |
| 销毁地址空间 | `unregister_vspace` → `del_vspace` | `del_vspace` 会 clean_user + radix delete |
| 内核稀疏大 buffer（page cache 等） | `page_slice_*`（`mm/page_slice.h`） | **非** `VSpace`；调用方分配内容页；见 [`page-slice.md`](page-slice.md) |

更细的 radix 语义以 `vmm_radix_tree.h` 内 Doxygen 为准。

### 0.4 `clone_vspace` 标志（`enum vspace_clone_flags`）

| 标志 | 含义 |
|------|------|
| `VSPACE_CLONE_F_USER_4K_ONLY` | 用户侧仅 4 KiB 叶（常用，应设置） |
| `VSPACE_CLONE_F_COW_PREP` | 复制页表树，L3 共享，为 COW 准备 |
| `VSPACE_CLONE_F_COPY_PAGES` | L3 指向新物理页（物理复制路径） |

调用方组合标志以实现其 COW/复制策略；core 不解释 Linux `fork` 语义。

### 0.5 `vspace_clear_user_mappings` 前置条件

**调用方（compat）：** 同 task 内无其它线程仍在此 `vs` 上运行（exec 前先结束 sibling 线程）。

**core 内检查（`vmm.c`）：** 失败（`-E_REND_AGAIN` / `-E_IN_PARAM`）若：

- `vs` 为 `root_vspace`（`-E_IN_PARAM`）。
- **`allow_self_use == true`（exec）**：远端 CPU 的 `tlb_cpu_mask` 仍置位则失败；本 CPU 允许在 `current_vspace == vs` 时保留本地位。
- **`allow_self_use == false`（`del_vspace`）**：任意 CPU 的 mask 位都必须已清。

### 0.6 TLB

修改映射或 ASID 后，遵循 `vs_tlb_cpu_mask` 与 arch TLBI（见 [`cache&tlb.md`](cache&tlb.md)）。切换 `current_vspace` 时由调度器处理本 CPU 的 shootdown 位图。

aarch64 上各 `arch_tlb_invalidate_*` helper **自包含** barrier（`dsb` + `tlbi` + `dsb` + `isb`），调用方无需再配对 `begin/end`。`map_handler` 的窗口 slot 仅在 PTE 变更时 TLBI；`map`/`unmap` 成功路径只 invalid 目标 VPN 对应 VA。

### 0.7 Radix 详细流程（调用方）

**锁序（禁止逆序）：** L0 big → L2 band → PMM zone → `vspace_lock`。持 PMM zone 锁时 **不要** 调用 `map()`/`unmap()`。

```c
/* 推荐：编排层（调用方已持 L0） */
vaddr addr = mm_user_utils_set_range_and_fill(vs, start, page_count, flags);
error_t err = mm_user_utils_fill_page_with_exist_range(vs, fault_va, flags);
error_t err = mm_user_utils_clean_range_and_unfill(vs, start, page_count, ppn_first);
error_t err = mm_user_utils_set_range_flags(vs, start, len_bytes,
        MM_USER_RANGE_FLAGS_DELTA, set_mask, clear_mask);
error_t err = mm_user_utils_remap_page(vs, page_va, new_ppn, new_flags, old_ppn);
```

**低层分配路径（须 `vmm_radix_tree_calculate_end_check` 得 `range_end`）：**

1. `vmm_radix_tree_lock_range_big_and_small(handler, vs, start, end, RADIX_RL_INSERT)`
2. `vmm_radix_tree_insert_range`（LAZY 预留）
3. `map()` 循环 + `vmm_radix_tree_leaf_bind_range`
4. `vmm_radix_tree_unlock_range_big_and_small`

**低层释放路径：** `RADIX_RL_QUERY_OR_CHANGE` → `leaf_unbind_range` → unlock L2 → `unmap` 循环 → `RADIX_RL_DELETE` → unlock → `pmm_free`。

**遍历父地址空间（复制/COW 编排）：**

```c
while (search_start < search_end) {
        if (!vmm_radix_tree_find_first_occupied_interval(
                    parent, search_start, search_end,
                    &interval_start, &interval_end, &flags))
                break;
        /* caller: copy/COW/remap interval to child vs */
        search_start = interval_end;
}
```

（调用方在持 L0 的前提下对每个 interval 调用 `mm_user_utils_*` 或 radix bind。）

**禁止：** 直接读写 `vs->root_radix` 内部；忘记 `calculate_end_check`；在持 zone 锁时 `map()`。

**调试：** `vmm_radix_tree_query_range` 检查 LAZY/VALID；确认 L0/L2 成对释放。

外部调用方总览：[`USING_CORE.md`](USING_CORE.md)。

---

## 1. 总览与分层

内存子系统自上而下分为：

| 层次 | 组件 | 职责 |
|------|------|------|
| 上层 | **kmalloc**（每 CPU `kallocator`） | 小对象按 slot；整页经 **`root_vspace` radix + `map()`**（见 §5） |
| 上层 | **page_slice** | 内核逻辑 buffer：pgoff → 调用方 kva（稀疏页）；见 [`page-slice.md`](page-slice.md) |
| 元数据 | **VSpace radix tree** | 记录 VA 区间与叶标志（LAZY/VALID/rmap）；用户编排见 **`mm_user_utils_*`** |
| 页表 | **Map Handler** + `map`/`unmap` | 每 CPU 临时映射区修改页表；非一次性全映射 |
| 物理 | **PMM**（Zone + Buddy） | 物理页框分配 |

设计要点：

- **物理内存**：引导信息 → `m_regions` 预留 kernel/percpu/pmm → zone/section/buddy。
- **虚拟内存**：内核恒等映射；运行期用自映射区改页表。
- **用户 VA**：每 `VSpace` 一棵 radix；`mm_user_utils_*` 在 **持 L0** 前提下编排 radix + PTE + PMM（§0）。
- **内核堆页**：`kmalloc.c` 直接对 **`&root_vspace`** 做 `insert_range` / `leaf_bind` / `map` 等，**不**调用 `mm_user_utils_*`。
- **kmalloc 小对象**：chunk 占用的虚拟页来自上述内核堆路径；跨 CPU 释放走 MSQ。

以下按 PMM → 页表/radix → kmalloc 展开。

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
   从 `pmm_phy_start_addr` 到 `pmm_phy_start_round_up_1g = ROUND_UP(pmm_phy_start_addr, GIGAN_PAGE_SIZE)` 的物理范围（若存在），仍落在 boot 已有的 L2 表（`L2_table`）内，直接对该区间以 2M 为单位调用 `arch_set_L2_entry`，建立恒等映射。

2. **为超出 1G 的虚拟空间增加 L1 项**  
   PMM 区在物理上占据的“1G 对齐段”数决定了需要的额外 L2 表数量。`calculate_pmm_space` 中：`L2_table_pages = total_pages / (GIGAN_PAGE_SIZE / PAGE_SIZE) + 1`（即每 1G 数据约一页 L2 表，再加一页余量）。这些 L2 表页在 PMM 预留区的**最前面**分配，物理地址为 `pmm_l2_start .. pmm_l2_start + L2_table_pages * PAGE_SIZE`。  
   对每一页 L2 表（`pmm_l2_start_iter`），在 **L1 表**（`L1_table`）中新增一项：虚拟地址取 1G 对齐的 `pmm_phy_start_round_up_1g_iter`（从 `pmm_phy_start_round_up_1g` 起每次 +1G），物理地址指向该 L2 表页。这样，第一个 1G 之后的每一段 1G 虚拟地址都有对应的 L2 表，且这些 L2 表本身放在 PMM 区开头，已落在第一段 1G 内、可访问。

3. **在每张新 L2 表中建立 2M 映射**  
   对每张上述 L2 表，在其内填 2M 大页表项：将物理区间 `[pmm_phy_start_round_up_1g_iter, pmm_phy_start_round_up_1g_iter + GIGAN_PAGE_SIZE)` 与同范围的虚拟地址（恒等映射）建立映射，直到覆盖 `pmm_phy_end`。

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
        struct list_entry rmap_list;   /* 反向映射：链到映射该页的 radix tree node */
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

#### 2.5.5 释放（`pmm_free`）

- `pmm_free` 按请求的 `page_number` 得到 `alloc_order`，顺序遍历块内 ppn，先对每页调用 `phy_Page_ref_minus`；若某页 `ref_count` 仍 > 0 则不再归还。否则对该页执行 buddy free/merge（用 zone 内线性下标定位 `bp->pages[index]`，从 order 0 起尝试与伙伴合并）：
  - 用 `(insert_node->ppn >> tmp_order) % 2` 决定伙伴在左还是右，得到 `buddy_index = index ± (1 << tmp_order)`；  
  - 若伙伴在 zone 内、且与当前块在物理上连续、且伙伴块首的 `order == tmp_order`（即伙伴整块空闲），则从两桶摘下这两块，合并为一块，`insert_node` 更新为合并后块首（`MIN(index, buddy_index)` 对应的页），`tmp_order++`，继续尝试与上一阶伙伴合并；  
  - 若无法合并，则将当前块首的 `order` 设为 `tmp_order`，挂到 `buckets[tmp_order].avaliable_frame_list`，并增加 `total_avaliable_pages`。

PMM 层对外接口为 `pmm_alloc` / `pmm_free`。**自本仓库当前实现起，PMM allocator 的并发互斥由 PMM 自己负责**：buddy 的 `pmm_alloc` / `pmm_free` **内部会获取并释放该 zone 的 PMM MCS 锁**（见 `pmm_lock/pmm_unlock` 或 `pmm_zone_lock/pmm_zone_unlock` 封装）。

与之对应，radix / kmalloc 等上层仍可能需要在**不进行分配/释放**的情况下保护 PMM 拥有的页元数据（例如 `Page.rmap_list` 的 link/unlink/遍历快照）。这类场景允许上层直接持有 zone 的 PMM 锁，但必须遵守以下契约：

- **允许**：持 `pmm_zone_lock(zone)` 保护 `Page` 元数据（如 `rmap_list`）的链表操作与遍历。
- **禁止**：在持有 PMM 锁期间调用 `pmm_alloc` / `pmm_free`（否则会因 allocator 内部再次取同一把锁而死锁）。
- **建议**：调用方不要再直接展开 `lock_mcs(&pmm->spin_ptr, &percpu(pmm_spin_lock[zone_id]))`，而是统一使用 `pmm_lock/pmm_unlock` 或 `pmm_zone_lock/pmm_zone_unlock`，以保证 `me` 节点选择一致（per-zone per-CPU）。

若从 buddy 取不到页，后续应在上层做 **swap**（置换），以腾出物理页再分配，当前为 TODO。

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
        cpu_id_t cpu_id;
        vaddr map_vaddr[4];      /* 本 CPU 的 4 个临时映射虚址，对应 L0/L1/L2/L3 访问 */
        ppn_t ppn_cache[4];      /* 从 PMM 预借的 4 页，map() 创建新页表页时消费并补回 */
        ppn_t mapped_ppn[4];     /* 各 slot 窗口当前映射的 phys pfn；与 MAP_L3_table PTE 一致 */
        struct pmm *pmm;
        spin_lock_t vspace_lock_node;
};
```

- 每个 CPU 的 4 个虚址为 `map_pages + cpu_id * 4 * PAGE_SIZE`。**使用 4 个 slot 而非单 entry 反复复用**，是为了在 walk 页表时 L0–L3 表页可并行挂在固定窗口上；配合 `mapped_ppn[]` 缓存，**同一 phys 页重复 walk 时跳过 PTE 写与窗口 TLBI**（热路径如内核堆连映多 4K 页）。
- `ppn_cache[]` 与 `mapped_ppn[]` 职责分离：前者是 map 过程中 **待用的页表页库存**；后者是 **窗口 PTE 的软件镜像**，仅由 `util_map` / `map_handler_unmap_slot` 维护。
- 2MiB 空间共 512 个 4K 页，每 CPU 占 4 页，故 **最多支持 128 个 CPU**；map_handler 与 per-CPU 的 allocator 数量受此上限约束。`init_map()` 为每 CPU 分配 4 个 ppn 填入 `ppn_cache[]`，将 `mapped_ppn[]` 置 0，并设定上述 `map_vaddr[]`。

**自映射的建立**（`kernel/mm/map_handler.c` 中 `sys_init_map`，仅在 BSP 执行一次）：

- 在**当前内核页表**的 L0 中，将 `map_pages` 对应项指向一张 L1 表（该 L1 表物理地址为 `MAP_L1_table` 的物理地址）。
- 该 L1 表中，`map_pages` 对应项指向 `MAP_L2_table`。
- 该 L2 表中，`map_pages` 对应项指向 `MAP_L3_table`。
- 即：访问 `map_pages` 起 4K 对齐的地址，依次通过 L0→L1→L2→L3，最终落在 `MAP_L3_table` 的某个 PTE 上。

`MAP_L1_table`、`MAP_L2_table`、`MAP_L3_table` 在 `kernel/mm/map_util_page.S` 中定义为 3 个 4K 的零页（boot 阶段或早期分配），物理上连续，用于根页表到 L3 的这条“自映射链”。

**修改页表时的用法**（`map()` 内）：

- 内部 `util_map(paddr, handler, slot_id)`：若 `PPN(p) == mapped_ppn[slot_id]` 则直接返回；否则写 `MAP_L3_table` 中 `map_vaddr[slot_id]` 的 PTE、TLBI 该窗口 VA，并更新 `mapped_ppn[slot_id]`。
- 然后通过 `(union Lx_entry *)handler->map_vaddr[level]` 访问并修改该级表；若该级不存在则用 `ppn_cache[]` 借页、清零、填上级表项，再 `util_map` 到对应 slot。
- `map` / `unmap` 成功时 **只** invalid 目标 `vpn` 的 VA；不再在出口无条件刷四个窗口（窗口 TLBI 由 `util_map` 按需完成）。
- 临时访问任意 phys 页（COW、清零等）应使用 **`map_handler_map_slot` / `map_handler_unmap_slot`**，不要直接写 `MAP_L3_table`；若必须手写 PTE，须同步 TLBI 并更新或清零对应 `mapped_ppn[slot]`。
- 因此不需要在系统运行前把全部页表都映射好，而是“按需映射到固定 4 个虚页”，修改完即可，实现动态、节省页表占用。

### 3.3 map / unmap 与层级

- `map(VSpace*, ppn, vpn, level, eflags, handler)`：在 `vs` 的页表中建立 `vpn` → `ppn` 的映射；`level == 2` 表示 2M 页，`level == 3` 表示 4K 页。内部按 L0→L1→L2→L3 逐级用 `util_map` 与 `ppn_cache[]` 创建缺失表页并写表项。
- `unmap`：清除对应 vpn 的映射，可选地将同一 vpn 指向新的物理页（用于部分高级用法）。
- 内核恒等映射区的新页通过 `map()` 在对应 level 插入 2M 或 4K 项；用户 vspace 由 `mm_user_utils_*` 或 fault 路径在持锁契约下调用 `map`/`unmap`。

---

## 4. Radix Tree：虚拟地址空间元数据

### 4.1 设计动机：硬件对齐的基数树架构

主流 ISA 的 MMU 均采用**基于基数树（多级页表）的硬件结构**，软件侧若再维护一套与页表并行的区间抽象（如 Linux 的 VMA 树），会带来双重抽象与复杂的同步需求。SOSP 2025 的 **CortenMM**（*Efficient Memory Management with Strong Correctness Guarantees*）[^1] 指出：传统做法在软件层维护 VMA 等区间结构，并与硬件页表保持一致，往往需要**细粒度、多层次的锁**，在多核下容易成为扩展瓶颈；而直接围绕页表设计、采用可扩展的加锁协议，能同时获得性能与可验证性。

[^1]: CortenMM: Efficient Memory Management with Strong Correctness Guarantees. SOSP 2025. 采用单一页表层级、事务式接口与可扩展加锁，避免 VMA 与页表双抽象带来的锁竞争。RendezvOS 的设计与之呼应：**采用 4 级 radix tree**，**按页/大页**粒度记录“虚拟页 ↔ 物理页”的映射，并配合**两层锁机制**，尽量把竞争限制在单核或单 vspace 内。

- **硬件对齐的 radix tree**：采用 4 级 512 路基数树，与 x86-64 页表结构（L0/L1/L2/L3）直接对应，每级索引分别为 9 位，覆盖 48 位虚拟地址空间。L0/L1/L2 每个表项为 `Radix_entry_t`（8 字节），L3 叶子为 `Radix_node_t`（32 字节，含 flags、rmap_list、owner）。
- **两层锁机制**：L0 "big lock" 覆盖 512 GiB 粒度（ coarse-grained 互斥），L2 per-band lock 覆盖 2 MiB 粒度（fine-grained 并发）。相比单一 vspace-wide 锁，多核扩展性显著提升。
- **Range-based APIs**：INSERT/DELETE/QUERY_OR_CHANGE 三种语义，支持 **lazy allocation**（PAGE_ENTRY_LAZY）、**COW**（fork 后共享只读页）、**部分区间操作**（munmap/mprotect）等。

以下先说明 Radix Tree 的角色与数据结构，再详述**两层锁的使用**与**多核下的扩展性**。

### 4.2 角色与绑定

- **内核堆 / kmalloc 整页**：共享 **`root_vspace`**；`kmalloc.c` 在 radix 上 insert/bind 并 `map()`。**不要**对 `&root_vspace` 调用 `mm_user_utils_*`（实现中显式拒绝）。
- **用户地址空间**：每 `VSpace` 独立 radix（`vs->root_radix`），经 **`mm_user_utils_*`**（调用方先持 L0）或 fault 路径更新。
- **Map Handler**：每 CPU `percpu(Map_Handler)`，修改任意 vspace 页表时使用。

地址判断：若虚拟地址 `>= KERNEL_VIRT_OFFSET` 则走内核路径；否则按当前 vspace 的 `root_radix` 操作。

VSpace 与 radix tree 的关联（`include/rendezvos/mm/vmm.h`）：

```c
typedef struct {
        paddr vspace_root_addr;   /* 页表根物理地址 */
        u64 vspace_id;
        void *root_radix;         /* 指向 Radix_entry_t* L0 表 */
        struct pmm *pmm;          /* 物理内存管理器 */
        spin_lock_t vspace_lock;  /* 保护页表修改（map/unmap） */
} VSpace;
```

**共享内核高半**：L0[256..511] 可在多个 vspace 间共享 L1/L3，通过 `tagged_ptr owner` 字段追踪所有者，实现安全的跨地址空间内核映射共享。

### 4.3 数据结构概要

Radix tree 采用 4 级 512 路结构，与 x86-64 页表层级对应。每级表页大小为 4KiB，通过 CAS bitlock 保护并发访问。（`include/rendezvos/mm/vmm_radix_tree.h`）

**L0/L1/L2 表项**：每个 `Radix_entry_t` 为 8 字节，包含锁、有效位、占用计数和子表指针：

```c
typedef struct {
        u64 value;  /* packed: bit0=lock, bit1=VALID, bits2..11=count, bits12..63=child_ptr */
} Radix_entry_t;

/* 字段解析（位域） */
#define VMM_RADIX_ENTRY_LOCK_OFF     0   /* CAS bitlock */
#define VMM_RADIX_ENTRY_VALID_OFF    1   /* 子表已发布 */
#define VMM_RADIX_CNT_SHIFT          2   /* 子占用计数（bits 2..11，支持 0..511） */
#define VMM_RADIX_PTR_MASK           0xfffffffffffff000ULL  /* 子表 KVA（低 12 位清零） */
```

- **bit0**：CAS bitlock，保护该表项的原子修改。
- **bit1**：VALID，表示子表指针已对其他 CPU 可见（pending 状态仅在 INSERT 事务内部可见）。
- **bits 2..11**：子级占用计数，用于判断何时可以释放整张子表（I7：只有计数为 0 才能释放）。
- **bits 12..63**：子表内核虚拟地址，低 12 位为 0（4K 对齐）。

**L3 叶子节点**：每个 `Radix_node_t` 为 32 字节，记录单个 4KiB 页的元数据：

```c
typedef struct {
        ENTRY_FLAGS_t flags;        /* 影子标志（与 PTE 同步） */
        struct list_entry rmap_list; /* 反向映射：链到 Page::rmap_list */
        tagged_ptr_t owner;         /* 所有者：低半=vs，高半=&root_vspace */
} Radix_node_t;
```

- **flags**：`PAGE_ENTRY_LAZY`（已预留但无物理页）、`PAGE_ENTRY_VALID`（已绑定物理页）、读写权限等。与 PTE flags 词汇对齐，但仅作为影子元数据。
- **rmap_list**：链到物理页的 `Page::rmap_list`，用于反向查找（swap、COW 分裂等）。
- **owner**：`tagged_ptr` 存储 `VSpace*` + CPU ID，用于共享高半的权限检查（I5）。

**表页布局**：
- L0 表：512 个 `Radix_entry_t`，覆盖 256 TiB（索引 L0[0]..L0[511]）。
- L1 表：每个 L0 项指向一张 L1 表（512 项），覆盖 512 GiB。
- L2 表：每个 L1 项指向一张 L2 表（512 项），覆盖 1 GiB。
- L3 数组：每个 L2 项指向 512 个 `Radix_node_t`（16 KiB），覆盖 2 MiB（每个 Radix_node_t 对应 4 KiB）。

### 4.4 两层锁机制与可扩展性

为实现**高并发、低争用**的虚拟地址空间操作，radix tree 采用**两层锁**：**L0 big lock**（coarse-grained）+ **L2 per-band lock**（fine-grained），相比单一 vspace-wide 锁，多核扩展性显著提升。

| 锁类型 | 作用范围 | 保护内容 | 持锁时长 |
|--------|----------|----------|----------|
| **L0 big lock**（`Radix_entry_t::bit0`） | 512 GiB shards（L0 表项） | 保护该 L0 项下的 L1 表分配、L1→L2 grow 路径、多 band 操作的原子性。区间操作前 `vmm_radix_tree_lock_range_big`，结束后 `vmm_radix_tree_unlock_range_big`。 | 整个 VA 区间操作期间 |
| **L2 per-band lock**（`Radix_entry_t::bit0`） | 2 MiB bands（L2 表项） | 保护该 2 MiB band 内的 L3 数组、叶子的 bind/unbind、flags 更新、占用计数调整。通过 `vmm_radix_tree_lock_range_small_with_big_locked` 或 `vmm_radix_tree_lock_range_big_and_small` 获取。 | 单个 band 操作期间 |
| **PMM zone lock**（MCS，per zone） | 每个 zone | 保护 PMM 的 `Page` 元数据（如 `rmap_list`）。`pmm_alloc`/`pmm_free` **内部自锁**；上层若需保护 `Page` 元数据，可用 `pmm_zone_lock`，但**不得在持锁时调用 `pmm_alloc/free`**。 | rmap 链表操作期间 |
| **vspace_lock**（MCS） | 单个 VSpace | 保护页表修改（`map`/`unmap`）。由 `map_handler` 内部管理，调用方通过 `handler->vspace_lock_node` 传入。 | 单次 `map`/`unmap` 调用 |

**内核路径**：
- 使用 `root_vspace`，通过 `mm_user_utils_set_range_and_fill` 分配连续物理页并映射。
- 持锁顺序：**L0 big lock → L2 band lock → PMM zone lock**（rmap 操作）。
- 多核分配主要竞争点在 **L2 band lock** 和 **PMM zone lock**，L0 big lock 仅在跨 512 GiB 区间时才可能争用。

**用户路径**：
- 每个进程（VSpace）有独立的 radix tree，通过 `vs->root_radix` 访问。
- **同一 vspace 内多线程**：在 L2 band 粒度上并发，不同 2 MiB band 可同时操作，**多核扩展性显著优于单一 vspace-wide 锁**。
- **跨 vspace**：不同进程的 radix tree 完全独立，无锁争用。

**设计意图小结**：通过”两层锁 + per-band 并发”，使得**内核分配与多进程用户路径几乎无锁争用**；**单进程多线程**的竞争点在 L2 band lock，粒度为 2 MiB，远优于单一 vspace-wide 锁。

### 4.5 Range API 与编排层次

Radix tree 提供**range-based APIs**，支持 INSERT/DELETE/QUERY_OR_CHANGE 三种语义，由 `mm_user_utils` 层编排为完整的”分配→映射→绑定”或”解绑→解映射→释放”流程。

**Range 锁获取五阶段**（`radix_range_lock_acquire`）：
1. **Phase 1**：锁 L0 片，保证 L1 表页存在（INSERT 可 grow）。
2. **Phase 2**：铺 L1 槽到 L2 表（INSERT 可 grow；不对 l1e 单独长期持锁）。
3. **Phase 3**：真实”区间锁”：按 VA 升序锁每条 2 MiB 行（L2 entry）、挂 L3、做叶级校验。
4. **Phase 4**：INSERT 提交 VALID + 结构计数；DELETE 仅 L1 带级结构减量。
5. **Phase 5**：释放全部 L0 锁；在仅持 L2 行锁下调整”页数”占用。

**核心 API 语义**：

| API | kind | 语义 | 调用时机 |
|-----|------|------|----------|
| `vmm_radix_tree_insert_range` | INSERT | Grow 路径，预留 LAZY 叶子，设置 `owner` | `mmap` reserve、lazy alloc |
| `vmm_radix_tree_leaf_bind_range` | QUERY_OR_CHANGE | LAZY → VALID，link rmap | `map` 之后，提交物理页 |
| `vmm_radix_tree_leaf_unbind_range` | QUERY_OR_CHANGE | VALID → LAZY，unlink rmap | `unmap` 之前，解除物理页绑定 |
| （无独立 `delete_range` 符号） | DELETE | 持 `RADIX_RL_DELETE` 锁后 `leaf_unbind` + `unmap` + 解锁并收缩 radix | `munmap` / `clean_range` 最后阶段 |
| `vmm_radix_tree_change_leaf_ppn` | QUERY_OR_CHANGE | COW 分裂、remap | fork page fault、`mremap` |
| `vmm_radix_tree_change_range_flag` | QUERY_OR_CHANGE | `mprotect` 批量 flag 更新 | `mprotect` |
| `vmm_radix_tree_query_range` | QUERY_OR_CHANGE | 读叶子元数据 | page fault 快速路径 |

**编排层次**（L5：`mm_user_utils`）：
- **分配路径**：`pmm_alloc` → `insert_range`（LAZY）→ `map` → `leaf_bind_range`（VALID + rmap）
- **释放路径**：`leaf_unbind_range`（VALID → LAZY）→ `unmap` → **RADIX_RL_DELETE 锁路径下回收区间** → `pmm_free`
- **COW 分裂**：`pmm_alloc` 新页 → `map_handler_copy_page` → `change_leaf_ppn`

### 4.6 用户编排层与反向映射

**内核堆路径**（`kmalloc` / `core_alloc_pages`，非 `mm_user_utils`）：
- `pmm_alloc` → radix `insert_range`（LAZY）→ `map` → `leaf_bind_range` → 可选 `map_handler_zero_page`（见 `kmalloc.c`）。
- 失败回滚：按相反顺序释放已绑定叶子（`leaf_unbind_range`），解映射页表（`unmap`），在 DELETE 语义下释放 radix 预留，释放物理页（`pmm_free`）。

**用户路径**：
- **分配**：`mm_user_utils_set_range_and_fill`（同内核路径，但作用于用户 vspace）。
- **lazy 分配**：`mm_user_utils_fill_page_with_exist_range`：page fault 时将 LAZY 叶子物化为 VALID，分配物理页并映射。
- **释放**：`mm_user_utils_clean_range_and_unfill`：`leaf_unbind_range` → `unmap` → radix DELETE 路径 → `pmm_free`。
- **remap**：`mm_user_utils_remap_page`：COW 分裂或 `mremap`，更新 PPN 和 flags。
- **flag 更新**：`mm_user_utils_set_range_flags`：`mprotect` 批量更新 PTE 和 radix flags，支持 ABSOLUTE/DELTA/DELTA_PTE_ONLY 三种模式。

**反向映射（rmap）**：
- 每个 `Page` 的 `rmap_list` 链起所有映射到该页的 `Radix_node_t`（通过 `radix_leaf_link_rmap` / `radix_leaf_unlink_rmap` 操作）。
- **共享内存**：多地址空间共享同一物理页时，每个 vspace 的 radix 叶子独立 rmap 链接到同一个 `Page`。
- **COW 分裂**：fork 后共享只读页，写故障时通过 rmap 找到所有共享者，分配新页并更新受害者的 `Radix_node_t`（`change_leaf_ppn`）。

**共享内核高半**：
- L0[256..511] 可在多个 vspace 间共享 L1/L3，通过 `vmm_radix_tree_bootstrap_shared_kernel_high_half`（BSP 一次性）+ `vmm_radix_tree_install_shared_kernel_high_half`（每个 vspace）建立。
- **owner 字段**：低半地址（L0[0..255]）为 `vs`；高半地址（L0[256..511]）为 `&root_vspace`。
- **安全保证**：DELETE 操作只清除 `owner` 匹配的叶子；bind/unbind 不检查 `owner`，允许跨 vspace 共享。

---

## 5. 内核对象分配器（kmalloc）

### 5.1 层次与接口

- 上层：`struct allocator`，提供 `m_alloc(allocator, size)` / `m_free(allocator, ptr)`。
- 实现为 `struct mem_allocator`（`include/rendezvos/mm/kmalloc.h`），内部包含 radix tree、12 个 `mem_group`、一个 `page_chunk_root`（红黑树）、以及每 CPU 的 buffer 无锁队列等。
- 分配时：小对象从对应 slot 的 group 取 object；若需新页则在 **`root_vspace`** 上走 radix insert/bind + `map()` + PMM（`kmalloc.c`），不经 `mm_user_utils`。  
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
        struct mem_group groups[MAX_GROUP_SLOTS];
        struct rb_root page_chunk_root;   /* 整页分配的 (page_addr, page_num) */
        ms_queue_t *buffer_msq;           /* 其他 CPU 释放过来的 object 队列 */
        atomic64_t buffer_size;
        cas_lock_t lock;
};
```

- 大于 2048 字节的请求在 **`root_vspace`** 上要整页（或连续多页），记入 `page_chunk_root`；free 时查页数再 `core_free_pages`。单次整页请求**不超过 2M**（与 buddy 最大块一致）；更大需求需多次分配或由上层拆分。

### 5.3 多核与跨 CPU 释放

- 每个 CPU 一个 `kallocator`（mem_allocator），`allocator_id` 一般为 cpu_id；**allocator（与 map_handler）数量受 128 上限约束**，与 3.2 节中“每 CPU 占 4 页、2MiB 最多 128 CPU”一致。  
- 释放时若发现 `object_header.allocator_id != 当前 allocator_id`，则不直接在本 allocator 释放，而是把该 object 挂到目标 allocator 的 `buffer_msq` 无锁队列。目标 CPU 在每次执行 free 时会顺带消费该队列，把对象还到正确的 chunk/group，避免跨 CPU 锁 chunk。  
- 整页分配通过 `page_chunk_root` 红黑树按虚地址查找，得到页数后调用 `free_pages`；该树与 radix tree 的映射关系一致，由 allocator 自己维护，不依赖 radix tree 记录”上层要了多少页”。

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
| Radix Tree 与两层锁 | `kernel/mm/vmm_radix_tree.c`，`include/rendezvos/mm/vmm_radix_tree.h` |
| 用户编排层（mm_user_utils） | `kernel/mm/mm_user_utils.c`，`include/rendezvos/mm/mm_user_utils.h` |
| kmalloc | `kernel/mm/kmalloc.c`，`include/rendezvos/mm/kmalloc.h` |
| page_slice | `kernel/mm/page_slice.c`，`include/rendezvos/mm/page_slice.h` |
| 虚拟 MM 与 per-CPU 初始化 | `kernel/mm/vmm.c`（virt_mm_init, init_map, init_radix, kinit） |

---

*本文档以当前代码为准；若与历史设计或注释有出入，以代码实现为准。*
