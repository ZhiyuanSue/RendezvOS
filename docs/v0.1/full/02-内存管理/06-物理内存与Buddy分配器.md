# 物理内存与Buddy分配器

v0.1 · 2026-10-02

本篇覆盖：`kernel/mm/pmm.c`、`kernel/mm/buddy_pmm.c`、`include/rendezvos/mm/pmm.h`、`include/rendezvos/mm/buddy_pmm.h`、`arch/x86_64/mm/pmm.c`、`arch/aarch64/mm/pmm.c` 及对应 `include/arch/*/mm/pmm.h`。

调用时机见 `01-启动与初始化/02-启动流程总览.md`：`phy_mm_init` 在 `virt_mm_init` 之前。页表 / Map_Handler 见同分区虚拟地址篇；内核堆见 `09-kmalloc与内核堆.md`。平台侧 RSDP / DTB memory 的细节分别链到 ACPI 篇与 DTB 篇，本篇只写它们如何进入 `m_regions`。

---

## 1. 概述

物理内存子系统干的事很具体：把固件（Multiboot memmap 或 DTB `/memory`）给出的可用物理区间收进 `m_regions`，把内核镜像、percpu、PMM 自己的元数据从可用区里抠掉，再按 **zone** 切开，每个 zone 挂一个 `struct pmm` 实现（默认是 **buddy**）。之后谁要「物理上连续的若干页」——页表大页、kmalloc 整页等——都从选定 zone 的 `pmm->pmm_alloc` 来拿。

为什么用 buddy，而不是启动期 bump 一下就完事？因为后面要能 **释放、合并**，而且经常要 2ⁿ 大小的连续块。最大 order 定成 **10**，单次最多 **`2^10` 页 = 4 MiB**（4 KiB 页）；**order 9** 正好盖住一张早期常用的 2 MiB middle page，也和常见内核「最大 buddy order」的量级接近。

实现上有两套并行的元数据：`Page` 管引用计数、所属 section、rmap；`buddy_page` 管空闲链和 order。分配时先抬 `Page` 的 ref，再改 buddy 桶；若 ref 阶段失败，buddy 元数据还没动，可以直接返回——避免「半分配」。Zone 框架是可扩展的（弱符号 `configure_pmm_zones_hook`），但默认只有一个 `ZONE_NORMAL`；非法配置会 **回退** 到单 zone + buddy，而不是硬失败。多 zone 的 manage 元数据打包在源码里仍是骨架，文档不假装 NUMA 已完工（evolution **E1**）。OOM 时有 reclaim 钩子预留，放锁后再调；树内目前 **没有** 注册者。

---

## 2. 目标与边界

本篇要讲清：区域表怎么来、reserve 怎么抠、zone 怎么切、buddy 怎么 alloc/free、reclaim 约定是什么，以及 x86 / aarch64 各自从哪里读 memmap。

故意不做的：完整 NUMA / 多 zone 产品策略；页面换出；「NORMAL 不够就自动借别的 zone」——调用方必须自己选对 `zone->pmm`。

分配失败时返回值要分清（`pmm_alloc` 成功时返回 **ppn**，失败时返回负错误码；`invalid_ppn(ppn)` 把 `ppn <= 0` 都当非法）：

- 请求页数超过 `2^BUDDY_MAXORDER`：直接 `-E_RENDEZVOS`，不进 reclaim。
- 本 zone 空闲总量不够，或找不到合适 order 的连续块：解锁后走 reclaim；没有 hook 或重试耗尽则 `-E_REND_AGAIN`；hook 返回 false 则 `-E_REND_NO_MEM`。
- `page_number == 0`：返回 0，算成功空操作。
- 内部 cursor 失败、双重 free 一类：`-E_RENDEZVOS`。

---

## 3. 分层与调用方

架构侧 `arch_init_pmm` 负责两件事：**从平台描述读出可用物理区间填进 `m_regions`**，以及给出「内核镜像之后」的游标 `next_region_phy_start`（供后面摆 percpu / PMM blob）。可移植的 `phy_mm_init` 只管 reserve、zone、buddy，不解析 Multiboot tag 或 DTB 属性——那些约定见 §6.1。

调用方拿着 `struct pmm *`（通常是 `mem_zones[ZONE_NORMAL].pmm`）调方法即可。不要假设「这个 zone 失败了实现会自动试下一个」。

与启动篇的衔接：`prepare_arch` 只保证「能读到平台描述」（x86 认出 Multiboot；aarch64 映射好 DTB 头），真正把可用内存收成区域表是 `arch_init_pmm` 的工作。`phy_mm_init` 成功之后，才谈得上 `virt_mm_init` 和 `arch_start_platform` 去分配物理页。

---

## 4. 数据结构与不变量

### 4.1 区域与 zone

`m_regions` 最多 128 槽。reserve 用切分 / 掏空；删除是把槽置零，并不紧凑重排。`ZONE_NR_MAX` 为 16，是数组容量；真正启用的个数是 `nr_mem_zones`。当前枚举实质使用的是 `ZONE_NORMAL`。每个 zone 有上下界、`struct pmm *`、以及 section 链。

一条硬约束来自早期页表：内核物理范围和 percpu 物理范围必须落在 **同一个 1 GiB** 窗口里，否则 `phy_mm_init` 失败——boot 阶段往往只有一张 L1/PDPT 项在撑着这段映射。

### 4.2 PMM blob 的物理布局

`phy_mm_init` 从可用区里再抠出一块连续物理内存，自己用：

1. 先放扩展 boot L2 表页（让后续 PMM 元数据也能被早期大页映到）；
2. 再放各 zone 的 `MemSection` 与 `Page[]`；
3. 再放 buddy 的 `buddy_page[]` 管理元数据（多 zone 时偏移累加 **尚未做完**）。

抠完之后，还会把 `[pmm_end, ROUND_UP(pmm_end, 2 MiB))` 再 reserve 掉。原因写在源码注释里：若不抠掉这块尾页，buddy 可能把它们分出去，而它们仍落在「按 2 MiB 映着的 PMM 窗口」里，后面用 4 KiB 去映同一窗口会打架。

### 4.3 Buddy：树状数组设计

`BUDDY_MAXORDER` 为 **10**，最大一次 `2^10` 页 = **4 MiB**（order **9** = 2 MiB）。每 zone 一把 MCS 锁：`lock_mcs(..., &percpu(pmm_spin_lock[zone_id]))`，`me` 必须是 **当前核** 的 percpu slot。

#### 为何用一块 `buddy_page[]`，而不是「每阶独立链表节点」

逻辑上 buddy 是一棵按 2ⁿ 对齐的树；实现上本仓库用**一块连续数组**同时扮演两件事：

1. **隐式树**：下标 `i` 对应 zone 内线性页序；大小为 `2^k` 的块 = 连续下标 `[i, i+2^k)`，**块首** `pages[i]` 是代表元。父子 / 左右伙伴用 **ppn 与 order 的算术**推出，不另存树指针。  
2. **各阶空闲链的节点**：`buckets[order].avaliable_frame_list` 挂的就是这些数组元素的 `page_list`——**没有**为每个 order 再 malloc 一套 list node。

```text
pages[]:  [0][1][2][3][4]…     ← 与 zone 内页一一对应
order=2 空闲块首（4 页）: pages[0]，伙伴 = pages[0] ± 4
非首页: order == -1，不单独入桶
```

伙伴判定：`(ppn >> k) % 2 == 0` → 伙伴在右（`index + (1<<k)`），否则在左。合并 / 分裂只动块首的 `order` 与桶链表。

**权衡：** 每个 `buddy_page` 多一个 `order` 字段（及 list 链），换来「树 + 各阶 freelist」零额外分配、O(1) 找伙伴。对比「每阶独立节点」：后者要维护节点↔物理页映射，拆合时分配/释放节点本身，元数据更碎。

`order` 语义：块首且空闲 → `0..BUDDY_MAXORDER`；已分配或块内非首页 → **`-1`**。初始化先当全体 order‑0，再自底向上合并到最大对齐块。

释放时只有 `Page.ref_count` 降到 0 才回空闲链；合并要求 buddy 索引合法且同属一个 section——跨洞不合并。`invalid_ppn(ppn)` 把 `ppn <= 0` 当非法，因此 **PPN 0 不可用**。注意 `phy_Page_first_addr` 一类辅助函数返回的其实是「该 Page 对应的物理地址起点」风格的值，调用时要对着头文件看，不要想当然当页帧号用。

### 4.4 物理布局直觉（内核后游标）

启动后早期恒等 / 大页映射通常先盖住 **第一段 ~1 GiB** 窗口。布局意图（具体偏移随平台 / mem 大小变，以 `arch_init_pmm` + `phy_mm_init` 为准）：

```text
低址 … 可用 RAM …
        ├─ 内核镜像
        ├─ percpu 区          ← 与镜像同落在「第一段 1 GiB」约束内（x86/aarch64 启动约定）
        ├─ PMM blob：扩展 L2 表页 + Section/Page[] + buddy_page[]
        └─ … 其余交给 buddy …

aarch64 另：镜像后常先 carve UART/DTB 等（见平台启动 / DTB 篇），再算 `next_region_phy_start`。
```

当物理内存很大时，**PMM blob 可能越过第一个 1 GiB**：`arch_map_pmm_data_space` 分阶段扩 L2——先填已有 L2 洞，再在 PMM 头部分出新 L2 页并挂新 L1 项，用 2 MiB 项映新增窗口。否则 `KERNEL_PHY_TO_VIRT` 访问越界元数据会缺页。`06` 只钉动机；逐步映射细节以 `arch/*/mm/pmm.c` 为准。

### 4.5 Reclaim

```c
typedef bool (*pmm_reclaim_fn_t)(struct pmm *pmm, size_t need_pages,
                                 unsigned have_tried_attempts);
```

调用时 **zone 锁已经释放**。hook 可以对这个 `pmm` 调 `pmm_free`，但 **禁止再调 `pmm_alloc`**（会死锁或重入）。最多约 `PMM_RECLAIM_MAX_ATTEMPTS`（64）次。树内目前没有 `pmm_set_reclaim_hook` 的生产调用方。

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

`arch_init_pmm(setup_info, &next_region_phy_start)` 是整条链的入口。各 ISA 最终都往同一个 `m_regions` 里 `insert(addr, len)`，但**输入格式因固件协议而异**（下分现行主线路径）。

#### x86_64：Multiboot memory map

平台约定见 [Multiboot Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)（Multiboot1）与 Multiboot2 的 mmap tag。装载器在 info 结构里给出一段 **mmap**：每项至少含 `addr`、`len`、`type`。`type == 1`（本仓库宏 `MULTIBOOT_MEMORY_AVAILABLE`）表示可用 RAM；其它类型（保留、ACPI、NVS 等）本实现**一律不插入** buddy。

代码路径（`arch/x86_64/mm/pmm.c`）：

1. 看 `setup_info->multiboot_magic`。Multiboot1 要求 info 的 `flags` 里同时有「基本 mem」和「mmap」位，否则直接失败；mmap 物理指针加 `KERNEL_VIRT_OFFSET` 后按 `for_each_multiboot_mmap` 遍历。Multiboot2 则扫 tag，找到 `MULTIBOOT2_TAG_TYPE_MMAP` 再遍历 entries。
2. 插入条件写在宏 `multiboot_insert_memory_region` 里：**`type == AVAILABLE`，且 `addr + len > BIOS_MEM_UPPER`（`0x100000`）**。也就是说：整段都落在 1 MiB 以下的可用区不要；跨过 1 MiB 的区整段插入（没有在插入时再裁掉低 1 MiB 前缀——低址占用靠后面的 kernel/percpu/ACPI reserve 与「可用窗口」计算消化）。
3. 随后 `reserve_arch_region`：在低址探测 RSDP，写回 `setup_info->rsdp_addr`；若 revision 0，再按 RSDT 地址对齐抠掉一个 2 MiB 窗口，避免 buddy 把 ACPI 表所在页分掉。更高 revision 当前直接报不支持。
4. `next_region_phy_start = PHY(_end)`——内核镜像物理末尾，percpu 紧挨着往后摆。

#### aarch64：设备树 `/memory`

平台约定见 Device Tree 惯例与 Linux 的 arm64 引导说明（DTB 由固件经 x0 传入，见平台启动篇 §4.6）。描述「系统 RAM」的常见写法是：存在一个或多个节点，带 **`device_type = "memory"`**，并用 **`reg`** 给出一个或多个 `(基址, 长度)` 对。`#address-cells` / `#size-cells` 决定每对里各几个 cell；在 QEMU virt 一类 64 位板上，常见是各 2 个 cell，于是 `reg` 里每 16 字节是一组 `u64 基址 + u64 长度`（FDT 里为大端，读取时要字节交换）。

代码路径（`arch/aarch64/mm/pmm.c`）：

1. 用 `setup_info->boot_dtb_header_base_addr`（`prepare_arch` 映射出的 DTB 虚地址）当 FDT 根。
2. `raw_get_prop_from_dtb` 按 **`device_type` 属性值为 `"memory"`** 查找（不是只认节点名 `/memory`——属性匹配更贴近 DT 惯例），对找到的 `reg` 调 `get_mem_prop_and_insert_region`。
3. 该回调假定 **每条区域 4×u32（两个 u64）**：大端解开后 `insert(addr, mem_len)`。**没有**像 x86 那样过滤「低 1 MiB」；板级可用区原样进表。
4. 接着遍历 FDT 的 **mem_rsvmap**（固件声明的保留项），目前 **只打印**，不 `reserve`——因此这些区间仍可能被 buddy 分出去，是已知缺口。
5. `next_region_phy_start = PHY(map_end_virt_addr)`——把早期 boot 在镜像后挂上的 UART 窗口、DTB 拷贝等算进「已占用游标」，避免 percpu/PMM blob 压上去。

上述各 ISA 路径都要求 `m_regions.region_count > 0`，否则 `arch_init_pmm` 直接 `kernel_halt`。

### 6.2 `phy_mm_init`（区域表之后）

`cmain` 传入的是 `boot.S` 填好、并经 `prepare_arch` 补过的 `struct setup_info *`。先跑完 §6.1 的 `arch_init_pmm`，再在可移植层做抠除与 buddy 初始化。本函数不建线程，也不建 Map_Handler。成功返回后 buddy 可分配；`virt_mm_init` 和 `arch_start_platform` 都依赖这一点。

骨架顺序：

1. `arch_init_pmm` → 填 `m_regions`，给出 `per_cpu_phy_start`（即上一节的游标）；
2. `reserve_per_cpu_region`；整段 `[kernel, percpu_end)` reserve；
3. 校验同 1 GiB；`arch_map_percpu_data_space` 并清零；
4. 算可用窗口；`pmm_configure_zones`（非法则默认 NORMAL）；
5. `split_pmm_zones`；估算 Page / section / manage / L2 页数；
6. `reserve_region_with_length` 拿 PMM blob；再 reserve 到 2 MiB 对齐；
7. `arch_map_pmm_data_space`；`generate_zone_data`；每 zone `pmm_init`。

### 6.3 alloc / free

alloc：持锁找 ≥ 所需 order 的块 → 分裂 → 先抬 `Page` ref 再改桶 → 返回起始 ppn，实际页数 **向上取整到 2ⁿ**（经 `alloced_page_number` 传出）。free：降 ref；到 0 再合并回链。空闲页总数够、但拼不出连续 2ⁿ 时，会进 reclaim（若已注册）。

### 6.4 与相邻子系统

`virt_mm_init` / Map_Handler / kmalloc 都假定本阶段已经能分配物理页；平台 ACPI / PCI 映射也走 NORMAL pmm。因果链：本篇 → 虚拟地址篇 → kmalloc 篇。DTB 属性解码的更细约定见 `09-平台模块/36-DTB与设备树-aarch64.md`；ACPI / RSDP 见 `35-ACPI与MADT-x86_64.md`。

---

## 7. 公开 API

本篇拥有：可移植 `include/rendezvos/mm/pmm.h`（+ buddy 默认实现 `buddy_pmm.h` / `buddy_pmm.c`），以及架构侧 `include/arch/<isa>/mm/pmm.h` 的 `arch_init_pmm`。接口说明以头文件注释为准，并已与 `pmm.c` / `buddy_pmm.c` / 各 ISA 的 `arch/*/mm/pmm.c` 核对。页表 / Map_Handler / kmalloc 见同分区后续篇。

### 7.1 编排顺序（与源码一致）

| 阶段 | 顺序 |
|------|------|
| `cmain` | `prepare_arch` → **`phy_mm_init`** → … → `virt_mm_init` → … |
| `phy_mm_init` 内部 | **`arch_init_pmm`** → reserve percpu+kernel → 同 1 GiB 校验 → map/clean percpu → 算可用窗 → **`configure_pmm_zones_hook`**（或默认 NORMAL）→ `split_pmm_zones` → reserve PMM blob（+ 2 MiB 尾）→ map/clean → `generate_zone_data` → 每 zone **`pmm->pmm_init`** |
| 运行期 | 调用方选 `zone->pmm`，再 `pmm->pmm_alloc` / `pmm->pmm_free`（可先 `pmm_set_reclaim_hook`） |

### 7.2 初始化

```c
error_t phy_mm_init(struct setup_info *arch_setup_info);
void arch_init_pmm(struct setup_info *, vaddr *next_region_phy_start);
void configure_pmm_zones_hook(paddr avail_lo, paddr avail_hi); /* weak */
```

| 接口 | 说明 |
|------|------|
| `phy_mm_init` | BSP 物理内存总入口。成功 `REND_SUCCESS`；reserve/布局失败 `-E_RENDEZVOS`。不建线程、不建 Map_Handler。成功后各 zone 的 buddy 可分配。 |
| `arch_init_pmm` | 填 `m_regions`，写出 percpu 起点游标。**x86**：Multiboot mmap（AVAILABLE 且越过 1 MiB），可保留 RSDP 窗，游标=`PHY(_end)`。**aarch64**：DTB `device_type=memory` 的 `reg`（按 2×u64），mem_rsvmap 只打印，游标=`PHY(map_end_virt_addr)`。区域为空或解析失败 → **`kernel_halt`**，不向 `phy_mm_init` 回错误码。 |
| `configure_pmm_zones_hook` | 在已知可用窗、切 zone 之前调用；填 `mem_zones[0..nr_mem_zones)` 与各 zone 的 `pmm`。弱默认：单 `ZONE_NORMAL` + `buddy_pmm`。非法配置会回退默认，不硬失败。 |

区域抠除经全局 `m_regions` 上的方法指针（非独立自由函数）：`memory_regions_reserve_region` / `memory_regions_reserve_region_with_length` 等，见 `struct memory_regions`。

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
| `pmm_free` | 释放从 `ppn` 起的 `page_number` 页（须与先前分配跨度匹配）。成功 `0`，失败负错误码。先降 `Page` ref，到 0 再并回 buddy 桶。 |
| `pmm_set_reclaim_hook` | 安装/清空 reclaim；调用时 **zone 锁已放**；hook 可对本 `pmm` `pmm_free`，**禁止**再 `pmm_alloc`。最多 `PMM_RECLAIM_MAX_ATTEMPTS`（64）次。树内暂无生产注册方。 |
| `pmm_lock` / `pmm_unlock` | MCS；`me` 必须是**当前核**的 `pmm_spin_lock[zone_id]`。 |
| `pmm_change_pages_ref` | 对连续页增减 `Page` ref；失败回滚本次已改部分。 |
| `BUDDY_MAXORDER` | `10`（最大 **4 MiB** / 次 = `2^10`×4 KiB；order 9 盖 2 MiB）。全局默认实例 `buddy_pmm`。 |

### 7.4 辅助约定

- `KERNEL_PHY_TO_VIRT` / `KERNEL_VIRT_TO_PHY`：各 arch `pmm.h`（依赖 `KERNEL_VIRT_OFFSET`）。
- `BIOS_MEM_UPPER`（x86 `0x100000`）：buddy 可用区下限，见平台启动篇 / x86 `arch_setup.h`。

---

## 8. 多架构（对照小结）

| | x86_64 | aarch64 |
|--|--------|---------|
| 描述来源 | Multiboot1 mmap / Multiboot2 mmap tag | DTB 中 `device_type="memory"` 的 `reg` |
| 过滤 | 仅 `AVAILABLE`，且 `addr+len > 1 MiB` | 无低址过滤；`reg` 按 2×u64 解析 |
| 额外保留 | RSDP→RSDT 一个 2 MiB 窗口（rev0） | mem_rsvmap **仅打印** |
| 内核后游标 | `PHY(_end)` | `PHY(map_end_virt_addr)` |

buddy 算法本体与 ISA 无关。官方协议入口：Multiboot Spec 的 mmap 一节；Device Tree / [arm64 booting](https://docs.kernel.org/arch/arm64/booting.html) 对 DTB 传递与尺寸的约束见平台启动篇。

---

## 9. 测试

`modules/test/` 下有 pmm / single 相关测例。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```

（需打开测例相关配置，如 `RENDEZVOS_TEST`。）若旧测例假定 OOM 一定是 `-E_RENDEZVOS`，会与现今 `-E_REND_AGAIN`（reclaim 耗尽）路径不一致——以源码与测例更新为准。本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- 多 zone 的 manage 偏移累加未完成；NUMA 规划以 evolution **E1** 为准。
- reclaim 钩子未接线。
- aarch64 firmware reserved（mem_rsvmap）尚未从 buddy 挖掉。
- aarch64 `reg` 解析写死「每区 4×u32」；若板子 `#address-cells/#size-cells` 不是 2/2，需要改解析。
- `pmm_show_info` 打印的桶可能不含最大 order。
- `reserve_region_with_length` 对区域长度更新的边界行为，以代码审查为准。

---

## 11. 变更记录

- 2026-10-02：§4.3 树状数组设计动机（隐式 buddy 树 + 桶共用 `buddy_page[]`；`order==-1`；MAXORDER=10）；§4.4 物理布局 / 越 1 GiB 映射动机。
- 2026-10-01：reclaim 耗尽错误码由 `-E_REND_RETRY` 改为 `-E_REND_AGAIN`（与 `error.h` 去重一致）。
- 2026-09-27：中文用语整理（契约→约定；§7「以头文件注释为准」；NUMA「以 E1 为准」）。
- 2026-09-26：纠正 `BUDDY_MAXORDER=10` 最大块为 **4 MiB**（非 2 MiB；order 9 才是 2 MiB）。
- 2026-09-26：§7 全文审阅——补 `phy_mm_init` / `arch_init_pmm` / `struct pmm` 头文件注释；写清 init 编排与 alloc 返回约定（含 `invalid_ppn` 与空请求 0）。
- 2026-09-25：补 §6.1——Multiboot mmap / DTB `memory`+`reg` 平台约定与本仓库解析步骤；修正 `arch_init_pmm` 签名说明。
- 2026-09-25：语言整理；纠正 §7 `pmm_alloc`/`pmm_free` 签名与返回约定。
- 2026-08-29：整篇重做——完整 init/PMM 布局；zone 回退；errno 与 reclaim；多 zone 骨架诚实说明。
- 2026-08-26：v0.1 初稿。
