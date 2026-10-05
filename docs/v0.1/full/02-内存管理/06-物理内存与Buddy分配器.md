# 物理内存与Buddy分配器

v0.1 · 2026-10-02

本篇覆盖：`kernel/mm/pmm.c`、`kernel/mm/buddy_pmm.c`、`include/rendezvos/mm/pmm.h`、`include/rendezvos/mm/buddy_pmm.h`、`arch/x86_64/mm/pmm.c`、`arch/aarch64/mm/pmm.c` 及对应 `include/arch/*/mm/pmm.h`。

调用时机见 `01-启动与初始化/02-启动流程总览.md`：`phy_mm_init` 在 `virt_mm_init` 之前。页表 / Map_Handler 见同分区虚拟地址篇；内核堆见 `09-kmalloc与内核堆.md`。平台侧 RSDP / DTB memory 的细节分别链到 ACPI 篇与 DTB 篇，本篇只写它们如何进入 `m_regions`。

---

## 1. 概述

本篇介绍物理内存分配器。

物理内存子系统在启动阶段做四件事：

 - 把固件给出的可用物理区间（x86 来自 Multiboot memmap，aarch64 来自设备树 `/memory` 节点）读进 `m_regions` 数组，统一不同架构下可用内存的描述方式。
 - 从中扣除内核镜像、percpu 区、PMM 自身元数据，以及架构必须保留的区间（例如 x86 上 ACPI 表所在窗口）。这些区间不能再被分配出去。
 - 把剩下的可用内存按 **zone** 划分。默认只有一个 `ZONE_NORMAL`，挂全局的 `buddy_pmm`；也可以通过 `configure_pmm_zones_hook` 配置多个 zone，但每个 zone 必须指向**各自独立**的 `struct pmm` 实例。
 - 按 pmm 的初始化要求逐个 zone 初始化，把生成的元数据放进对应的元数据区。此后凡是要取「物理上连续的若干页」——例如页表大页、kmalloc 的整页——都从选定 zone 的 `pmm->pmm_alloc` 申请，释放时调 `pmm_free`。

目前只实现了 buddy 这一种 pmm 分配器，本篇也会讲清它的算法。但对使用者而言，只需关心 `struct pmm` 提供的那组函数指针即可。

---

## 2. 目标与边界

本篇要讲清：x86 与 aarch64 各自从哪里读出可用物理内存、`m_regions` 如何建立、保留区间如何扣除、zone 如何划分、buddy 如何分配和释放、分配失败时的 reclaim 约定。

明确不做的事：完整的 NUMA 与多 zone 策略（设计上支持多 zone，目前只启用一个）；页面换出；以及「NORMAL 不够就自动借用别的 zone」——调用方必须自己选对 `zone->pmm`。

---

## 3. 分层与调用方

架构侧的 `arch_init_pmm` 做两件事：一是解析 Multiboot tag 或设备树属性，把可用物理区间填进 `m_regions`；二是给出「内核镜像之后」的游标 `next_region_phy_start`，供后续摆放 percpu 区和 PMM 元数据。

跨架构的 `phy_mm_init` 只负责按既定前提扣除保留区间、按策略划分 zone、生成 buddy 元数据。

本篇内容的调用方通常是 core 封装好的虚拟页面分配方案，或内核对象分配器。它们持有 `struct pmm *`（一般是 `mem_zones[ZONE_NORMAL].pmm`），调用其中通用的方法即可。

与启动篇的衔接：`prepare_arch` 只保证「能读到平台描述」（x86 认出 Multiboot，aarch64 映射好 DTB 头），真正把可用内存整理成区域表是 `arch_init_pmm` 的工作。`phy_mm_init` 成功之后，`virt_mm_init` 和 `arch_start_platform` 才能去分配物理页。

---

## 4. 数据结构与不变量

物理内存这边叠了几层表，先把名字对齐，后面讲布局和 buddy 才读得下去。

### 4.1 区域、zone、section 和页描述

**`m_regions`（`struct memory_regions`）** 是一张「现在还有哪些连续物理内存可以用」的表。固件通过 Multiboot mmap 或设备树 `memory` 节点给出若干段 RAM，`arch_init_pmm` 把它们逐段 `insert` 进来，每一槽是一段 `[addr, addr+len)`，最多 `RENDEZVOS_MAX_MEMORY_REGIONS`（128）槽。这张表本身**并不是分配器**，只记录「这段物理地址目前仍算可用」。

```c
/* include/common/mm.h */
struct region {
        u64 addr;
        u64 len;
};

/* include/rendezvos/mm/pmm.h */
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

从中扣除已经占用的空间，叫 **reserve**。内核镜像、percpu 区、PMM 自身元数据，以及架构必须保留的洞（例如 x86 上 ACPI 表所在窗口），都走这条路。实现上是修改槽里的 `addr`/`len`：整段用光就把该槽**置零**（`delete`），并不把后面的槽往前挪；只切掉一头则缩短这一槽；从中间掏空则一槽裂成两槽。因此 `region_count` 只增不减，空槽靠 `addr==0 && len==0` 判断。

**zone（`MemZone`）** 是软件在物理地址上划出的窗口：`[lower_addr, upper_addr)`，再挂一个分配器 `struct pmm *`。它回答的是「这段地址归哪套策略管」，与固件原始的那几段 RAM 不是一回事。Linux 里常见 DMA / NORMAL / HIGHMEM 这种划分，本仓库沿用同一套骨架，但默认只有一个 **`ZONE_NORMAL`**，窗口覆盖扣除内核和 percpu 之后的可用范围，分配器用全局的 **buddy**（`buddy_pmm`）。`mem_zones[]` 编译期容量是 `ZONE_NR_MAX`（16），真正启用的个数是 `nr_mem_zones`，使用前缀 `[0, nr_mem_zones)`。也可以用 `configure_pmm_zones_hook` 再划窗口，但每个 zone 必须指向**各自独立**的 `pmm` 实例——两个窗口共用同一个 buddy 对象会把账本写乱。`pmm_zone_config_legal` 发现区间非法、`pmm` 为空、或指针重复，就退回默认单 zone。挂上的 `pmm` 必须能 `pmm_init`，否则 `phy_mm_init` 失败。

```c
enum zone_type {
        ZONE_NORMAL = 0,
        ZONE_NR_MAX = 16,
};

extern MemZone mem_zones[ZONE_NR_MAX];
extern int nr_mem_zones;

typedef struct {
        struct list_entry section_list;
        u64 zone_id;
        struct pmm *pmm;
        paddr upper_addr;
        paddr lower_addr;
        size_t zone_total_pages;
        size_t zone_total_sections;
        size_t zone_pmm_manage_pages;
} MemZone;
```

固件给的 RAM 中间往往有洞（被 reserve 扣除过，或本来就不连续）。zone 窗口盖上去之后，与 `m_regions` 里剩下的区间求交，得到若干段**真正连续、且落在该 zone 里**的物理内存。每一段就是一个 **`MemSection`**：有自己的 `[lower_addr, upper_addr)`，后面紧跟这块里每一页的 **`Page` 数组**（柔性数组 `pages[]`）。一个 zone 上这些 section 串成链表。跨洞的两段不会合成一个 section，后面 buddy 合并空闲块时也不能跨 section。

```c
typedef struct {
        i64 ref_count;
        MemSection *sec;
        struct list_entry rmap_list;
} Page;

struct mem_section {
        struct list_entry section_list;
        u64 sec_id;
        MemZone *zone;
        size_t page_count;
        paddr upper_addr;
        paddr lower_addr;
        Page pages[];
};
```

这样每 4 KiB 物理页会有**两套**结构来描述，不要混淆：

| 结构 | 跟谁走 | 记什么 |
|------|--------|--------|
| `Page` | 跟 `MemSection`，与物理页一一对应 | 引用计数、所属 section、rmap 链表（谁映射了这一页，虚拟地址篇再用） |
| `buddy_page` | 跟该 zone 的 buddy 分配器，与 zone 内页一一对应 | 空闲阶 `order`、挂在哪一阶空闲链上。这是分配/释放用的账本，具体字段取决于所用的 pmm 算法，buddy 见 §4.3 |

```c
/* include/rendezvos/mm/buddy_pmm.h */
struct buddy_page {
        struct list_entry page_list;
        ppn_t ppn;
        i64 order; /* 块首且空闲：0..BUDDY_MAXORDER；已分配或非首页：-1 */
};
```

**`struct pmm`** 是 zone 对外的分配器接口，包含 `pmm_alloc` / `pmm_free` / `pmm_init` 等函数指针。调用方拿着 `zone->pmm` 要页、还页。默认实现是 buddy（`struct buddy` 开头就是这份 `PMM_COMMON`）；换别的分配器也走同一组函数指针。页帧号记作 **ppn**（physical page number）；`invalid_ppn` 把 `ppn <= 0` 都当非法，所以 **0 号页不能拿来分配**。

```c
typedef i64 ppn_t;
#define invalid_ppn(ppn) (ppn <= 0)

#define PMM_COMMON                                                            \
        void (*pmm_init)(struct pmm * pmm,                                    \
                         paddr pmm_phy_start_addr,                            \
                         paddr pmm_phy_end_addr);                             \
        ppn_t (*pmm_alloc)(struct pmm * pmm,                                  \
                           size_t page_number,                                \
                           size_t *alloced_page_number);                      \
        error_t (*pmm_free)(struct pmm * pmm, ppn_t ppn, size_t page_number); \
        size_t (*pmm_calculate_manage_space)(size_t zone_page_number);        \
        void (*pmm_show_info)(struct pmm * pmm);                              \
        spin_lock spin_ptr;                                                   \
        MemZone *zone;                                                        \
        u64 total_avaliable_pages;                                            \
        pmm_reclaim_fn_t reclaim_fn;

struct pmm {
        PMM_COMMON;
};

struct buddy {
        PMM_COMMON;
        u64 buddy_page_number;
        struct buddy_page *pages;
        struct buddy_bucket buckets[BUDDY_MAXORDER + 1];
};
```

还有一条启动期硬约束：内核镜像和 percpu 区的物理范围必须落在**同一个 1 GiB** 窗口里，否则 `phy_mm_init` 失败。早期页表常常只有一张 L1/PDPT 项在支撑这段映射，两块若跨到下一个 1 GiB，启动阶段还没有完整页表能覆盖。

### 4.2 PMM 元数据落在哪、里面摆什么

上面这些 `MemSection`、`Page[]`、`buddy_page[]` 本身也要占物理内存。`phy_mm_init` 从可用区里再切出一块连续区间专门放它们，本文称作 **PMM 元数据**。这块是给记账用的，与后面交给 buddy 去分配的页不是一回事，不要混在一起。

内核后游标（`next_region_phy_start`）标出「镜像之后从哪里开始用」。x86 一般从 `PHY(_end)` 起；aarch64 常把早期 UART、DTB 也算进已占用，游标取 `PHY(map_end_virt_addr)`。percpu 紧挨游标往后摆，PMM 元数据再往后。镜像和 percpu 必须落在同一个 1 GiB 窗口里（§4.1）。整段物理内存大致是：

```text
低址 … 可用 RAM …
        ├─ 内核镜像
        ├─ percpu 区
        ├─ PMM 元数据
        └─ … 其余交给 buddy …
```

元数据内部按顺序摆放：

1. 先放扩展 boot L2 表页，让后续 PMM 元数据也能被早期大页映射到；
2. 再放各 zone 的 `MemSection` 与 `Page[]`；
3. 再按 zone 顺序接上各 zone 对应 pmm 的 `buddy_page[]`。`pmm_init` 的物理区间用 `zone_pmm_manage_offset` 往下推，下一个 zone 接在上一个 zone 的元数据后面。

切完之后，还会把 `[pmm_end, ROUND_UP(pmm_end, 2 MiB))` 这段尾页也 reserve 掉。若不扣除，buddy 可能把它们分出去，而这些页仍落在「按 2 MiB 映射的 PMM 窗口」里，之后用 4 KiB 重新映射同一窗口就会冲突。

物理内存很大时，**元数据可能越过第一个 1 GiB**。`arch_map_pmm_data_space` 会分阶段扩 L2：先填已有的 L2 空洞，再在 PMM 头部分配新的 L2 页并挂上新 L1 项，用 2 MiB 项映射新增窗口。否则 `KERNEL_PHY_TO_VIRT` 访问越界的元数据会缺页。具体怎么挂表，以 `arch/*/mm/pmm.c` 为准。

### 4.3 Buddy：把二叉树写进数组

§4.2 把元数据本身的物理内存布置好了，接下来由 buddy 管理剩下的可用页怎么分配、怎么释放。

先把三个词说清楚。**阶（order）** `k` 表示这一块里有 `2^k` 张连续页。**块首**是这块里下标最小的那一页，块有多大、是否空闲，都记在块首上。**伙伴**是同一阶、对齐之后能拼成更大一块的另一半：两页互为 0 阶伙伴，两块 2 页互为 1 阶伙伴，依此类推。buddy 做的事很直接：分配时给出一块够大的 2ⁿ 连续页；释放时若伙伴也空着，就拼成更大的块，避免可用内存全碎成单页。一次最多给出 `2^10` 页（4 KiB 页即 4 MiB），9 阶正好是一张 2 MiB 大页，`BUDDY_MAXORDER` 就是这个上限 10。

逻辑上这是一棵二叉树：大块劈成两个半块，两个半块再合成大块。实现上不必单独分配树节点——本仓库把这棵树**写在 `pages[]` 里**，一页对应一个 `buddy_page`，物理页帧号记在它的 `ppn` 字段中。

#### 为什么 `buddy_page` 要排密

zone 里的物理页往往不从 0 号帧连续到末尾，中间有洞（§4.1 的 `MemSection`）。如果让 `buddy_page` 数组的下标直接等于页帧号，空洞会让数组变得很稀疏，大段空洞甚至无法跨越。

所以 `pmm_init` 按 section 的顺序，把每一张可用页排进 `pages[0]`、`pages[1]`、…、`pages[N-1]`。下标是连续的，真正的帧号另存在 `pages[i].ppn` 里。后面找伙伴、劈块、合并，都先在这个连续下标上算，算完再用 ppn 核对：两页在数组里相邻，物理上不一定相邻，核对不过就不合并。

教科书里的 buddy 常常是每阶一条空闲链表，链上的节点另外分配。Linux 一类实现则按页帧号铺一张很大的 `struct page[]`，对帧号做异或找伙伴。本仓库也走后一种路子：每张可用页对应 `pages[]` 里的一个 `buddy_page`，空闲链就挂在这个结构自己的 `page_list` 上，不再另开一套链表节点。差别在下标的含义：这里的下标是 **zone 里数过来的第几页**，不是帧号。有洞时，下标相邻不能直接当作物理相邻，合并前必须看 ppn 之差是否正好是 `2^k`。

| | 每阶链表、节点另分 | 按页帧号铺的 `page[]` | 本实现 |
|--|--|--|--|
| 每页的记录放哪 | 另一套对象或一份 bitmap | 按下标 = 帧号铺开 | 按 zone 内页序排密的 `buddy_page[]` |
| 找伙伴 | 对帧号异或 | 对帧号异或 | 对**下标**异或，再用 ppn 差确认 |
| 有洞时 | 取决于节点怎么建 | 帧号不连续，还要再包一层 | 洞不占数组项；ppn 对不上就不合并 |

#### 下标怎样表示一棵树

`pages[i]` 是 zone 里按顺序的第 `i` 页。一块空闲、阶为 `k` 的内存，块首在下标 `i`，覆盖数组区间 `[i, i + 2^k)`。只有块首的 `order` 写成 `k`；块里其余项写成 `-1`，不挂进空闲链。孩子不用指针：左半块仍从 `i` 起、阶变成 `k-1`；右半块从 `i + 2^(k-1)` 起。源码里右孩子就是 `del_node + (1 << (k-1))`。

伙伴用异或算：把下标的第 `k` 位翻转过来。`0` 和 `1` 互为 0 阶伙伴，`0` 和 `2` 互为 1 阶伙伴，`0` 和 `4` 互为 2 阶伙伴。源码是 `index ^ (1UL << k)`，运算对象是下标，不是 ppn。

下面用 8 页示意（真实最高阶是 10）。`k` 写在块首上，表示这是一块空闲的 `2^k` 页；`.` 表示被这块覆盖、`order == -1`。

若这 8 页的 ppn 也连着，初始化会合成一块 3 阶：

```text
下标   0    1    2    3    4    5    6    7
       3    .    .    .    .    .    .    .     一块 8 页，块首在 0
```

#### 启动时怎样合成大块

上面那块 8 页并不是一开始就有的。先把每一页当成 0 阶，挂进 0 阶空闲链。然后从低阶往高阶扫：若块首的 ppn 按 `2^k` 对齐，并且 `pages[i + 2^(k-1)]` 的 ppn 恰好是下一块物理页，就把两个半块从低阶链上摘掉，块首升一阶。对不齐、或中间有洞的，停在已经合到的那一阶。

#### 空闲链：按阶把块首串起来

为了快速回答「现在有没有一块至少这么大的空闲内存」，buddy 给每一阶各准备一条空闲链表，链上挂的就是块首那个 `buddy_page`，不再另分配节点。每条链表连同这一阶的统计信息，组成一个 **桶（bucket）**：

```c
struct buddy_bucket {
        u64 order;                       /* 这一阶的阶数 */
        u64 aval_pages;                   /* 这一阶当前空闲的总页数 */
        struct list_entry avaliable_frame_list; /* 空闲块首链 */
};
```

`struct buddy` 里有 `BUDDY_MAXORDER + 1` 个桶，`buckets[k]` 专管阶 `k` 的空闲块。分配时从所需的阶往上找：哪一阶的桶里链表非空，就摘下那个块首。块比需要的更大，再往下劈。

#### 分配：找到一块，劈到刚好

例如需要 2 页（1 阶），手里却只有上面那块 8 页（3 阶）：

```text
先劈成两块 4 页：块首 0 和 4
再把左边劈成两块 2 页：块首 0 和 2
拿走下标 0 起的 2 页（0、1 写成 order = -1）
2 和 4 两块挂回各自那一阶的桶里

下标   0    1    2    3    4    5    6    7
       #    #    1    .    2    .    .    .     # = 已分配
```

源码分两步走，避免半途失败：先给范围内每张 `Page` 加上引用计数，这一步失败则空闲链还没动；成功后再劈块，把交出去的那些项写成 `order = -1`，块首从桶里摘掉。

#### 释放：一页一页还，能合就合

释放时不会把当初分出去的整块 2ⁿ 一次还清。`pmm_free` 对范围内每一页把 `Page.ref_count` 减一；减到 0，才把**这一页**送进 `pmm_free_one_index`。同一页若还被别人映射着，这块就不能整段收回。

从 0 阶往上试：刚还的是下标 0，伙伴是 `0 xor 1 = 1`。若 `pages[1]` 也是空闲的 0 阶，且两个 ppn 相差 1，就合成下标 0 起的 1 阶。再问 1 阶的伙伴 `0 xor 2 = 2`……伙伴越界、阶对不上、或 ppn 差不是 `2^k`，就停，把当前块首挂进这一阶的桶里。

数组把洞略去之后，下标仍可能相邻，物理上却隔着一段：

```text
一段：ppn 10、11  →  pages[0]、pages[1]
（中间没有可用页）
下一段：ppn 20、21  →  pages[2]、pages[3]
```

`pages[1]` 和 `pages[0]` 可以合（ppn 差 1）。某一阶上异或也许会算到下标 2，但 `|ppn 差| = 9`，不是任何 `2^k`，拒绝合并。洞的两边拼不成一块。

#### 锁，以及两个容易看错的接口

修改空闲链时，每个 zone 一把 MCS 锁，`me` 必须是**当前核**的 `percpu(pmm_spin_lock[zone_id])`。

`invalid_ppn(ppn)` 把 `ppn <= 0` 都当成非法，所以 0 号页不能拿来分配。`phy_Page_first_addr` 返回的是该 `Page` 对应的**物理地址起点**，不是页帧号。

### 4.4 Reclaim

空闲链里找不到够大的块时，buddy 可以请上层把一些页还回来再试。但 core 本身不负责 OOM 时的页面置换或杀进程，所以只能通过一个可注入的 hook 把这件事交给兼容层：hook 释放完页后，buddy 重新尝试分配，直到达到最大重试次数仍失败，才真正判定分配不出去。这就是 **reclaim**：

```c
typedef bool (*pmm_reclaim_fn_t)(struct pmm *pmm, size_t need_pages,
                                 unsigned have_tried_attempts);
```

调用时 **zone 锁已经释放**。hook 可以对这个 `pmm` 调 `pmm_free`，但 **禁止再调 `pmm_alloc`**（会死锁或重入）。最多约 `PMM_RECLAIM_MAX_ATTEMPTS`（64）次。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/mm/pmm.c` | `phy_mm_init`、regions、zone 切分、Page 生成、弱 hook |
| `kernel/mm/buddy_pmm.c` | buddy 算法、reclaim 循环 |
| `arch/x86_64/mm/pmm.c` | Multiboot memmap、ACPI 相关保留、boot 映射辅助 |
| `arch/aarch64/mm/pmm.c` | DTB `/memory`、以 `map_end` 为内核后游标 |
| `pmm.h` / `buddy_pmm.h` | `struct pmm`、`Page`、`buddy_page`、API |

riscv / loongarch 的 `pmm.h` 为空桩，无实现。

---

## 6. 流程

### 6.1 从平台描述读出可用物理内存

`arch_init_pmm(setup_info, &next_region_phy_start)` 是整条链的入口。各 ISA 最终都往同一个 `m_regions` 里 `insert(addr, len)`，但**输入格式因固件协议而异**。下面分别看现行主线路径。

#### x86_64：Multiboot memory map

平台约定见 [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)（Multiboot1）与 Multiboot2 的 mmap tag。装载器在 info 结构里给出一段 **mmap**，每项至少含 `addr`、`len`、`type`。`type == 1`（本仓库宏 `MULTIBOOT_MEMORY_AVAILABLE`）表示可用 RAM；其它类型（保留、ACPI、NVS 等）本实现**一律不纳入** buddy。

代码路径（`arch/x86_64/mm/pmm.c`）：

1. 看 `setup_info->multiboot_magic`。Multiboot1 要求 info 的 `flags` 里同时有「基本 mem」和「mmap」位，否则直接失败；mmap 物理指针加 `KERNEL_VIRT_OFFSET` 后按 `for_each_multiboot_mmap` 遍历。Multiboot2 则扫描 tag，找到 `MULTIBOOT2_TAG_TYPE_MMAP` 后再遍历其中的各项。
2. 插入条件写在宏 `multiboot_insert_memory_region` 里：**`type == AVAILABLE`，且 `addr + len > BIOS_MEM_UPPER`（`0x100000`）**。也就是说：整段都落在 1 MiB 以下的可用区不要；跨过 1 MiB 的区整段插入。插入时不再裁掉低 1 MiB 前缀，低地址的占用靠后面的 kernel/percpu/ACPI reserve 与「可用窗口」计算来扣除。
3. 随后 `reserve_arch_region`：在低地址探测 RSDP，写回 `setup_info->rsdp_addr`；若 revision 0，再按 RSDT 地址对齐扣除一个 2 MiB 窗口，避免 buddy 把 ACPI 表所在页分掉。更高 revision 当前直接报不支持。
4. `next_region_phy_start = PHY(_end)`，即内核镜像物理末尾，percpu 紧随其后。

#### aarch64：设备树 `/memory`

平台约定见 Device Tree 惯例与 Linux 的 arm64 引导说明（DTB 由固件经 x0 传入，见平台启动篇 §4.6）。描述「系统 RAM」的常见写法是：存在一个或多个节点，带 **`device_type = "memory"`**，并用 **`reg`** 给出一个或多个 `(基址, 长度)` 对。`#address-cells` / `#size-cells` 决定每对里各几个 cell；在 QEMU virt 一类 64 位板上，常见的是各 2 个 cell，于是 `reg` 里每 16 字节是一组 `u64 基址 + u64 长度`（FDT 里为大端，读取时要字节交换）。

代码路径（`arch/aarch64/mm/pmm.c`）：

1. 用 `setup_info->boot_dtb_header_base_addr`（`prepare_arch` 映射出的 DTB 虚地址）作为 FDT 根。
2. `raw_get_prop_from_dtb` 按 **`device_type` 属性值为 `"memory"`** 查找（不是只认节点名 `/memory`，属性匹配更贴近 DT 惯例），对找到的 `reg` 调 `get_mem_prop_and_insert_region`。
3. 该回调假定 **每条区域 4×u32（两个 u64）**：大端解开后 `insert(addr, mem_len)`。**没有**像 x86 那样过滤「低 1 MiB」，板级可用区原样进表。
4. 接着遍历 FDT 的 **mem_rsvmap**（固件声明的保留项），目前 **只打印**，不 `reserve`——因此这些区间仍可能被 buddy 分出去，是已知缺口。
5. `next_region_phy_start = PHY(map_end_virt_addr)`，把早期 boot 在镜像后挂上的 UART 窗口、DTB 拷贝等算进「已占用游标」，避免 percpu 或 PMM 元数据压上去。

上述各 ISA 路径都要求最终得到的 `m_regions.region_count > 0`，否则 `arch_init_pmm` 直接 `kernel_halt`。

### 6.2 `phy_mm_init`（区域表之后）

`cmain` 传入的是 `boot.S` 填好、并经 `prepare_arch` 补过的 `struct setup_info *`。先跑完 §6.1 的 `arch_init_pmm`，再在可移植层扣除保留区间并初始化 buddy。本函数成功返回后 buddy 可分配；`virt_mm_init` 和 `arch_start_platform` 都依赖这一点。

骨架顺序：

1. `arch_init_pmm` → 填 `m_regions`，给出 `per_cpu_phy_start`（即上一节的游标）；
2. `reserve_per_cpu_region`；整段 `[kernel, percpu_end)` reserve；
3. 校验同 1 GiB；`arch_map_percpu_data_space` 并清零；
4. 算可用窗口；`pmm_configure_zones`（非法则默认 NORMAL）；
5. `split_pmm_zones`；估算 Page / section / manage / L2 页数；
6. `reserve_region_with_length` 占用 PMM 元数据；再 reserve 到 2 MiB 对齐；
7. `arch_map_pmm_data_space`；`generate_zone_data`；各 zone 的 `pmm_init` 按 `zone_pmm_manage_offset` 接到上一份 `buddy_page[]` 后面并生成。某个 zone 没有 `pmm_init` 则本函数失败。

### 6.3 alloc / free

alloc：持锁找到 ≥ 所需阶的块 → 分裂 → 先增加 `Page` 引用计数再改动桶 → 返回起始 ppn，实际页数 **向上取整到 2ⁿ**（经 `alloced_page_number` 传出）。

free：降低引用计数；减到 0 再合并回桶。空闲页总数够、但拼不出连续 2ⁿ 时，会进入 reclaim（若已注册）。

### 6.4 与相邻子系统

`virt_mm_init` / Map_Handler / kmalloc 都假定本阶段已经能分配物理页；平台 ACPI / PCI 映射也走 NORMAL pmm。因果链：本篇 → 虚拟地址篇 → kmalloc 篇。DTB 属性解码的更细约定见 `09-平台模块/36-DTB与设备树-aarch64.md`；ACPI / RSDP 见 `35-ACPI与MADT-x86_64.md`。

---

## 7. 公开 API

本篇涉及的接口分布在：可移植的 `include/rendezvos/mm/pmm.h`（以及 buddy 默认实现 `buddy_pmm.h` / `buddy_pmm.c`），和架构侧 `include/arch/<isa>/mm/pmm.h` 的 `arch_init_pmm`。页表 / Map_Handler / kmalloc 见同分区后续篇。

### 7.1 编排顺序（与源码一致）

| 阶段 | 顺序 |
|------|------|
| `cmain` | `prepare_arch` → **`phy_mm_init`** → … → `virt_mm_init` → … |
| `phy_mm_init` 内部 | **`arch_init_pmm`** → reserve percpu+kernel → 同 1 GiB 校验 → map/clean percpu → 算可用窗 → **`configure_pmm_zones_hook`**（非法则默认 NORMAL）→ `split_pmm_zones` → reserve PMM 元数据（+ 2 MiB 尾）→ map/clean → `generate_zone_data` → 按 zone **依次** `pmm->pmm_init`（manage 区间不重叠） |
| 运行期 | 调用方选 `zone->pmm`，再 `pmm->pmm_alloc` / `pmm->pmm_free`（可先 `pmm_set_reclaim_hook`） |

### 7.2 初始化

```c
error_t phy_mm_init(struct setup_info *arch_setup_info);
void arch_init_pmm(struct setup_info *, vaddr *next_region_phy_start);
void configure_pmm_zones_hook(paddr avail_lo, paddr avail_hi); /* weak */
```

| 接口 | 说明 |
|------|------|
| `phy_mm_init` | BSP 物理内存总入口。成功 `REND_SUCCESS`；reserve/布局失败、或某个已启用 zone 没有 `pmm_init`，返回 `-E_RENDEZVOS`。不建线程、不建 Map_Handler。成功后各 zone 的 buddy 可分配。 |
| `arch_init_pmm` | 填 `m_regions`，写出 percpu 起点游标。**x86**：Multiboot mmap（AVAILABLE 且越过 1 MiB），可保留 RSDP 窗，游标=`PHY(_end)`。**aarch64**：DTB `device_type=memory` 的 `reg`（按 2×u64），mem_rsvmap 只打印，游标=`PHY(map_end_virt_addr)`。区域为空或解析失败 → **`kernel_halt`**，不向 `phy_mm_init` 回错误码。 |
| `configure_pmm_zones_hook` | 在已知可用窗、切 zone 之前调用；填 `mem_zones[0..nr_mem_zones)` 与各 zone 的 `pmm`（静态实例，**每个 zone 一份**）。弱默认：单 `ZONE_NORMAL` + `buddy_pmm`。区间非法、`pmm` 为空、或两个 zone 共用同一 `pmm` 指针：回退默认，不硬失败。缺 `pmm_init` 不在这一层回退，见上表 `phy_mm_init`。 |

区域扣除经全局 `m_regions` 上的方法指针（非独立自由函数）：`memory_regions_reserve_region` / `memory_regions_reserve_region_with_length` 等，见 `struct memory_regions`。

### 7.3 Zone 与 `struct pmm`（默认 buddy）

```c
extern MemZone mem_zones[ZONE_NR_MAX];
extern int nr_mem_zones;
extern struct spin_lock_t pmm_spin_lock[ZONE_NR_MAX];

/* 方法在 zone->pmm 上；默认实现 buddy */
ppn_t  (*pmm_alloc)(struct pmm *, size_t page_number, size_t *alloced_page_number);
error_t (*pmm_free)(struct pmm *, ppn_t ppn, size_t page_number);
void    (*pmm_init)(struct pmm *, paddr start, paddr end);
size_t  (*pmm_calculate_manage_space)(size_t zone_page_number);
void pmm_set_reclaim_hook(struct pmm *, pmm_reclaim_fn_t);
void pmm_lock(struct pmm *); void pmm_unlock(struct pmm *);
error_t pmm_change_pages_ref(struct pmm *, ppn_t start, size_t n, bool inc);
```

| 接口 | 说明 |
|------|------|
| `pmm_alloc` | 分配至少 `page_number` 页；成功返回起始 **ppn**（`ppn_t`=`i64`），`*alloced_page_number` 为向上取整到 2ⁿ 的实际页数。`page_number==0` → 返回 `0` 且 alloced=0。失败返回**负** `error_t`：超 `2^BUDDY_MAXORDER` → `-E_RENDEZVOS`；无 hook / 重试耗尽 → `-E_REND_AGAIN`；hook 放弃 → `-E_REND_NO_MEM`。判断失败用 `invalid_ppn`（`ppn<=0`）；空请求的 `0` 也被该宏判为非法，调用方需区分。core **不**自动跨 zone 借用。 |
| `pmm_free` | 释放从 `ppn` 起的 `page_number` 页（须与先前分配跨度匹配）。成功 `0`，失败负错误码。先降 `Page` 引用计数，减到 0 再并回 buddy 桶。 |
| `pmm_set_reclaim_hook` | 安装/清空 reclaim；调用时 **zone 锁已放**；hook 可对本 `pmm` `pmm_free`，**禁止**再 `pmm_alloc`。最多 `PMM_RECLAIM_MAX_ATTEMPTS`（64）次。树内暂无生产注册方。 |
| `pmm_lock` / `pmm_unlock` | MCS；`me` 必须是**当前核**的 `pmm_spin_lock[zone_id]`。 |
| `pmm_change_pages_ref` | 对连续页增减 `Page` 引用计数；失败回滚本次已改部分。 |
| `BUDDY_MAXORDER` | `10`（最大 **4 MiB** / 次 = `2^10`×4 KiB；9 阶盖 2 MiB）。全局默认实例 `buddy_pmm`。 |

### 7.4 辅助约定

- `KERNEL_PHY_TO_VIRT` / `KERNEL_VIRT_TO_PHY`：各 arch `pmm.h`（依赖 `KERNEL_VIRT_OFFSET`）。
- `BIOS_MEM_UPPER`（x86 `0x100000`）：buddy 可用区下限，见平台启动篇 / x86 `arch_setup.h`。

---

## 8. 多架构（对照小结）

| | x86_64 | aarch64 |
|--|--------|---------|
| 描述来源 | Multiboot1 mmap / Multiboot2 mmap tag | DTB 中 `device_type="memory"` 的 `reg` |
| 过滤 | 仅 `AVAILABLE`，且 `addr+len > 1 MiB` | 无低地址过滤；`reg` 按 2×u64 解析 |
| 额外保留 | RSDP→RSDT 一个 2 MiB 窗口（rev0） | mem_rsvmap **仅打印** |
| 内核后游标 | `PHY(_end)` | `PHY(map_end_virt_addr)` |

buddy 算法本体与 ISA 无关。官方协议入口：Multiboot Spec 的 mmap 一节；Device Tree / [arm64 booting](https://docs.kernel.org/arch/arm64/booting.html) 对 DTB 传递与尺寸的约束见平台启动篇。

---

## 9. 测试

`modules/test/` 下有 pmm / single 相关测试用例。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```

（需打开测试用例相关配置，如 `RENDEZVOS_TEST`。）若旧测试用例假定 OOM 一定是 `-E_RENDEZVOS`，会与现今 `-E_REND_AGAIN`（reclaim 耗尽）路径不一致——以源码与测试用例更新为准。

---

## 10. 限制与后续

- 默认仍是单 `ZONE_NORMAL`。多 zone 的 manage 切片和「不得共用同一 `pmm`」已经落地；完整的 NUMA 策略、谁该用哪个 zone，以 evolution **E1** 为准。
- reclaim 钩子尚未接入。
- aarch64 上固件保留区（mem_rsvmap）尚未从 buddy 中扣除。
- aarch64 `reg` 解析写死「每区 4×u32」；若板子 `#address-cells/#size-cells` 不是 2/2，需要改解析。
- `pmm_show_info` 打印的桶可能不含最大阶。
- `reserve_region_with_length` 对区域长度更新的边界行为，以代码审查为准。

---

## 11. 变更记录

- 2026-10-04：全文中文表述整理——补「桶（bucket）」定义；去掉「腾地方」「打架」「半截」「就不搞合并的事情」等口语化用词；§6.1 改「下分现行主线路径」「entries」「低址」「PMM blob 压上去」等翻译腔；§4.3 题改为「把二叉树写进数组」（不再用易和 Fenwick 混淆的「树状数组」）。
- 2026-10-04：原 §4.4 与 §4.2 重合（总图 + 元数据内部顺序），并入 §4.2；Reclaim 改为 §4.4。
- 2026-10-04：§4.3 按阅读顺序重写（阶/块首/伙伴 → 排密下标 → 初始化 → 分配劈块 → 释放合并）；去掉对读者说话的括号和 Fenwick 抢戏。
- 2026-10-04：多 zone 的 `buddy_page[]` 按 `zone_pmm_manage_offset` 依次切片；两个 zone 不得共用同一 `pmm`（`legal` 回退默认）；缺 `pmm_init` 则 `phy_mm_init` 失败。
- 2026-10-02：§4.3 树状数组设计动机（隐式 buddy 树 + 桶共用 `buddy_page[]`；`order==-1`；MAXORDER=10）；§4.4 物理布局 / 越 1 GiB 映射动机。
- 2026-10-01：reclaim 耗尽错误码由 `-E_REND_RETRY` 改为 `-E_REND_AGAIN`（与 `error.h` 去重一致）。
- 2026-09-27：中文用语整理（契约→约定；§7「以头文件注释为准」；NUMA「以 E1 为准」）。
- 2026-09-26：纠正 `BUDDY_MAXORDER=10` 最大块为 **4 MiB**（非 2 MiB；order 9 才是 2 MiB）。
- 2026-09-26：§7 全文审阅——补 `phy_mm_init` / `arch_init_pmm` / `struct pmm` 头文件注释；写清 init 编排与 alloc 返回约定（含 `invalid_ppn` 与空请求 0）。
- 2026-09-25：补 §6.1——Multiboot mmap / DTB `memory`+`reg` 平台约定与本仓库解析步骤；修正 `arch_init_pmm` 签名说明。
- 2026-09-25：语言整理；纠正 §7 `pmm_alloc`/`pmm_free` 签名与返回约定。
- 2026-08-29：整篇重做——完整 init/PMM 布局；zone 回退；errno 与 reclaim；多 zone 骨架诚实说明。
- 2026-08-26：v0.1 初稿。
