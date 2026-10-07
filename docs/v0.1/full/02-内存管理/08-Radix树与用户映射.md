# Radix 树与用户映射

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/vmm_radix_tree.c`、`kernel/mm/mm_user_utils.c`、`include/rendezvos/mm/vmm_radix_tree.h`、`include/rendezvos/mm/mm_user_utils.h`；与 clone / register 紧耦合处见 `kernel/mm/vmm.c`（完整 VSpace 生命周期仍以 `07-虚拟地址空间与页表.md` 为准）。

整章内存栈总图见 `06-物理内存与Buddy分配器.md` 开篇。硬件 PTE 怎么改见同分区页表篇。page fault 填页 / 拆页的**策略**在兼容层；本篇只钉 core 侧约定。`register_vspace` 按页表根地址挂红黑树（见 §4.7），与 IPC 子系统按字符串名注册 port 是两套不同机制——此处只是提醒别混淆，不展开 IPC。

---

## 1. 概述

用户地址空间里，**radix 是映射账本（以它为准）**：哪段 VA 已预约（LAZY）、权限意图、owner、以及和物理页之间的反向映射（rmap，reverse map——把每个物理页指向所有映射它的 radix 叶子，用于 COW 分裂、回收时反向查找）。硬件 PTE 只是 **MMU 此刻所见**——装进页表、供翻译用的那一面。core **不再**构建一套 Linux 式 VMA（Virtual Memory Area，虚拟内存区间）链表；兼容层在 radix + `map`/`unmap` 之上实现 mmap / fault / COW 策略。

### 1.1 为何对齐硬件页表树、而不是另造 VMA

软件 radix 的级数 / 扇出刻意贴近「4 KiB 页粒度（granule）、每级 9 bit」的硬件翻译树：同一段 VA 在 radix 与 PTE 上切分方式一致，fork / fault / unmap 扫区间时少做「VMA 合并再拆到页」的二次翻译。代价是调用方要理清 L0/L2 锁，以及 `mm_user_utils_*` 的编排；公开 VA 区间还要先过 `vmm_radix_tree_calculate_end_check`。

若像经典宏内核那样维护独立 VMA 抽象 + 另一套页表 walker，会出现双账本漂移（SOSP 2025 的 CortenMM 一文讨论过「策略层与硬件层各记一套」的扩展性瓶颈）。本仓库选择：**策略意图存入 radix，落地翻译存入 PTE**，fault 时两面都读，但以 radix 判定「该不该填 / 该不该 COW」。

这样做还避免「只看 PTE」分不清「已预约还没填页」和「根本没映射」；fork 做完 COW 准备之后，PTE 往往是只读的，但 radix 里仍记着「意图可写 + COW」——写故障时必须读 radix。

core 里的 `clone_vspace` 已经能做 `COW_PREP` / `COPY_PAGES`；真正在 fault 里填页或拆页，由兼容层调 utils 完成。

### 1.2 为何要把锁做得这么复杂

读后面 §4.3 的五阶段、§4.4 的锁序、§4.5 的变更操作一览之前，先讲清楚整套机制的根本目的——否则初见者会觉得「为了改一个映射，要分五个阶段、两层锁、三种 kind，是不是过度工程」。

**核心诉求**：两个 CPU 同时操作**同一个地址空间**里**不重叠的 VA 区间**时，应当**真正并发**，而不是一个 CPU 拿着一把全局锁、另一个 CPU 干等它放锁。

举例：CPU 0 在 `[0x1000, 0x2000)` 上 demand-page，CPU 1 在 `[0x100000, 0x101000)` 上 demand-page，两者完全不相干。如果只有一把 vspace-wide 锁，CPU 1 必须等 CPU 0 放锁才能动——在多进程多线程的服务器上，这种争用会让 fault 路径退化成串行。本仓库的设计目标就是**不让这种不相干操作互相阻塞**。

要达到这个目标，需要解决三个问题：

1. **锁要分档，不能一把抓**。把锁拆成两层：L0 按 512 GiB 分桶（粗粒度，管「谁在驱动这段跨度」），L2 按 2 MiB 分带（细粒度，管「真正改这段 2 MiB」）。两个 CPU 落在不同 2 MiB 带里就互不干扰；只有落在同一带里才排队。这就是 §4.2 的 L0/L2 锁。

2. **树要能按需长，长的时候不能挡别人**。radix 树一开始只有 L0 根页，下层 L1/L2/L3 表页是按需创建的。如果「创建下层表页」要持一把全局锁，那两个 CPU 同时在不同的 512 GiB 桶里第一次建 L1 时也会互相挡。于是有了 §4.3 的**五阶段 + 预约/发布**：每个 CPU 在自己要走的路径上，自上而下逐级「没有就建一页、CAS 发布给其他 CPU 看」，发布用的是每项自己的 bit-lock，不挡别的项。这就是「预约」（叶子级记 `LAZY`）和「发布」（表级原子写指针 + `VALID`）的来由——它们不是装饰，是并发建树的必要机制。

3. **回滚要能精确到阶段**。五阶段里任何一步都可能失败（`pmm_alloc` 拿不到页、检测到 overlap、叶子仍 VALID 不能删）。如果失败要回滚，必须只回滚**已经改动过的部分**，不能把别的 CPU 已经发布的东西也撤掉。所以每个阶段都有自己的 `phase*_clean_*` 标签，按「后建的先撤」顺序释放，绝不碰别的 CPU 的发布结果。

**代价**：调用方要理清 L0/L2 锁的获取顺序、`mm_user_utils_*` 的编排、`calculate_end_check` 的门禁；读起来确实比「一把锁保一切」复杂。**回报**：单进程多线程的 fault / mmap / munmap 在多核上几乎不互相阻塞，争用点从「整个地址空间」收窄到「同一段 2 MiB 带」——这就是本篇后面所有复杂机制存在的理由。

---

## 2. 目标与边界

提供：radix 树与 L0/L2 锁；INSERT / DELETE / QUERY 等语义；`mm_user_utils_*` 把 PMM、PTE、radix、rmap 编成可回滚的序列；`clone_vspace` 的用户半拷贝 / COW 准备；共享内核高半安装；`register_vspace`。

不做：page fault 策略本身；swap。DELETE 不按 owner 过滤——只拒绝仍带 VALID 的叶子（须先 unbind）；头文件不变量第 5 条（I5）已按此书写，见 `vmm_radix_tree.h` 注释。

---

## 3. 分层与调用方

- **兼容层 / 加载器**：通常先 `vmm_radix_tree_lock_range_big`，再多次 `lock_range_small_with_big_locked` + utils；L0 可以跨多次调用一直持有。utils **不会**自己去获取 L0。
- **kmalloc 等**：可用 `vmm_radix_tree_lock_range_big_and_small`（内部取到 L2 后**释放 L0**），避免嵌套在已持有的 L0 临界区里。
- **utils**：一律拒绝 `&root_vspace`（内核的全局根地址空间，所有 VSpace 共享它的内核高半；只服务用户地址空间，见 §4.6）。调用方已持有 L0 时，用 `lock_range_small_with_big_locked` 那条路径。

推荐生命周期：`insert_range`（LAZY）→ 调用方 `map` → `leaf_bind`（VALID + rmap）→ … → unbind → unmap → DELETE。

---

## 4. 数据结构与不变量

### 4.1 术语、叶子状态与数据结构

本篇反复出现「预约」「发布」两个词，先把它们的含义钉死，再给出叶子的状态机和转换。

#### 4.1.1 术语

- **预约（reserve）**：在 radix 账本里**先记下意图**，但**不**触碰硬件 PTE、也**不**分配物理页。对应 `insert_range`：把叶子置成 `LAZY`、写上 `owner`，仅此而已。意思是「我打算把这段 VA 映射出来，但物理页还没准备好」。fault 时如果 PTE 无效而 radix 是 `LAZY`，就知道该填页（demand-page，按需填页）；如果 radix 里根本没有，就是未映射（SEGV）。
- **发布（publish）**：在加锁路径里**把一张新建的中间表页对其他 CPU 变得可见**。具体做法是 `pmm_alloc`（物理内存管理器分配一页）、清零、再用 CAS（Compare-And-Swap，原子比较并交换）把「子页指针 + VALID 位」原子写进父项。CAS 之前别的 CPU 看到的是旧（invalid）状态，CAS 之后看到新表。这是「树锁在没有下层时如何创建新树」的核心动作，见 §4.3 Phase 1–3。

两个词落在不同层面：**预约是叶子级的状态转换**（写 `LAZY`），**发布是表级的结构转换**（写 `VALID` + 指针）。不要混用。

#### 4.1.2 叶子状态与转换

一个 L3 槽（`Radix_node_t`）在生命周期里会经历下面几种状态。`PAGE_ENTRY_LAZY` / `PAGE_ENTRY_VALID` / `PAGE_ENTRY_COW` 都是软件标志，写在 `flags` 字段里；PTE 侧另有对应的硬件位（如 WRITE、PRESENT），由 `map` 同步。

```text
                       insert_range (INSERT)
          不存在 ─────────────────────────────→ LAZY
          (槽为 tp_new_none：                      │
           即「空 tagged pointer」                │ leaf_bind_range (调用方 map 之后)
           全 0 标记，见 §4.1.3)                    ▼
              ↑                                  VALID
              │ DELETE Phase 4+5                 (有 PPN，挂 rmap)
              │ (radix_node_clear +
              │  count 归零则释放)                  │
              │                                    │
              │             ┌──────────────────────┤
              │             │                      │
              │   leaf_unbind_range    change_leaf_ppn (COW split / remap)
              │   (调用方 unmap 之前)   (换 PPN，flags 可变)
              │             │                      │
              │             ▼                      ▼
              │           LAZY ←──────────────── VALID (新 PPN/flags)
              │
              └─ change_range_flag 可在 LAZY 或 VALID 上改 flags（不碰 PPN/rmap）
```

要点：

- **不存在 → LAZY**：`insert_range` 强制带 `LAZY` 并清 `VALID`，**不**写 PPN、**不**挂 rmap。
- **LAZY → VALID**：`leaf_bind_range` 在调用方 `map` **之后**调用，置 `VALID`、清 `LAZY`、`link_rmap`。拒绝同时 `VALID|LAZY`——这是「先 insert 再 map 再 bind」顺序的硬保证。
- **VALID → LAZY**：`leaf_unbind_range` 在调用方 `unmap` **之前**调用，清 `VALID`、`unlink_rmap`、恢复 `LAZY`。
- **VALID → VALID（换 PPN）**：`change_leaf_ppn` 用于 COW split / remap，先摘旧 rmap 再加新 rmap，失败回滚到旧 PPN。
- **LAZY/VALID → 不存在**：DELETE 路径。Phase 4 检测仍 `VALID` 就拒绝（须先 unbind），`radix_node_clear` 清叶子；Phase 5 自下而上回填计数，某级 count 归零就 `free_level_table` 释放子表页。
- **COW**：`PAGE_ENTRY_COW` 与 `VALID` 正交，可叠加在 `VALID` 上。fork 走 `COW_PREP` 时父子叶子都置 `COW`，PTE 侧清 WRITE；写故障时由兼容层调 `remap_page` 分裂。

#### 4.1.3 数据结构

叶子本身不是第二套页表，只是影子账本。L0–L2 每个表项是一个打包的字；L3 每个 4 KiB 槽是一个 `Radix_node_t`：

```c
typedef struct {
        u64 value; /* lock / valid / count / 子页指针，打包在同一字里 */
} Radix_entry_t;

typedef struct {
        ENTRY_FLAGS_t flags;
        struct list_entry rmap_list;
        tagged_ptr_t owner;
} Radix_node_t;
```

radix 的级数 / 扇出刻意贴近硬件翻译树，同一段 VA 在两边切分方式一致：

```text
   VA:  ┌─────────┬─────────┬─────────┬─────────┬──────────┐
        │ L0 idx  │ L1 idx  │ L2 idx  │ L3 idx  │  offset  │
        │  9 bit  │  9 bit  │  9 bit  │  9 bit  │  12 bit  │
        └────┬────┴────┬────┴────┬────┴────┬────┴──────────┘
             │         │         │         │
             ▼         ▼         ▼         ▼

   硬件页表树（MMU 翻译用）        Radix 树（映射账本，以它为准）
   ┌───────────────────────┐      ┌───────────────────────┐
   │ L0  table             │      │ L0  entries           │
   │  └→ L1 table          │      │  └→ L1 entries        │
   │     └→ L2 table       │      │     └→ L2 entries     │ ← L2 锁 (2 MiB)
   │        └→ L3 table     │      │        └→ L3 slot    │
   │           └→ PTE      │      │           └→ node     │ ← Radix_node_t
   │  (PPN + 硬件 flags)    │      │  (flags + rmap + owner)│
   └───────────────────────┘      └───────────────────────┘
        MMU 此刻所见                  策略意图（LAZY/VALID/COW）

   fault 时两面都读，但由 radix 判定「该不该填 / 该不该 COW」：
     · PTE 只读 + radix 记 WRITE|COW  →  COW split（不是 fill）
     · radix 是 LAZY、PTE 无效        →  填页
     · radix 里没有                   →  未映射（SEGV）
```

### 4.2 L0 / L2 锁

L0 按大约 512 GiB（`HUGE_PAGE_SIZE`）一档，管的是「谁在驱动这段 VA 跨度」、支撑长区间发现。L2 按 2 MiB 一档，真正做 mutate / grow、改叶标志、bind / rmap。跨多档 L2 时升序加锁、降序释放。

```text
   用户 VA 低半
   ┌───────────────────┬───────────────────┬───────────────────┬───┐
   │   L0 桶 0         │   L0 桶 1         │   L0 桶 2         │...│  每桶 512 GiB
   │   (bit-lock)      │   (bit-lock)      │   (bit-lock)      │   │
   └───────────────────┴───────────────────┴───────────────────┴───┘
            │  L0 锁：谁在驱动这段跨度（跨多次 utils 一直持有）
            ▼
   ┌─────────┬─────────┬─────────┬─────────┬─────────┬───┐
   │ L2 区 0 │ L2 区 1 │ L2 区 2 │ L2 区 3 │ L2 区 4 │...│  每区 2 MiB
   │ (bit-lock) │ (bit-lock) │ (bit-lock) │ (bit-lock) │ (bit-lock) │...│  每区 2 MiB
   └─────────┴─────────┴─────────┴─────────┴─────────┴───┘
            │  L2 锁：真正 mutate / grow / bind / rmap
            ▼
        叶子（Radix_node_t：flags + rmap + owner）
```

两种常见入口：

1. `lock_range_big` +（多次）`lock_range_small_with_big_locked` —— 加载器 / fault / mmap 的路径；L0 跨多次 utils。
2. `lock_range_big_and_small` —— 内部持有 L2 后**释放 L0**；clone 目标插入、kmalloc 整页映射等会用。

**Clone 窗口（源码强调）：** 在源上持有用户半的 L0，可以挡住**其他核再进入**该 radix，但**不表示**扫 interval 时没有别的核持有 L2——他核可能已经走 `big_and_small` 释放了 L0、手里还持有 L2。查找代码扫占用区间时必须 **获取 L2 并等待**。不要理解成「持有 L0 就包办一切」。

`vmm_radix_tree_calculate_end_check(start, page_count, &end)`：公开 VA 区间的唯一门禁（溢出 + 页对齐）。锁和变更 API **不再**重复这道检查；调用方必须把同一个 `vaddr_end` 贯穿整个临界区。

加锁种类：

```c
typedef enum {
        RADIX_RL_INSERT = 0,
        RADIX_RL_DELETE,
        RADIX_RL_QUERY_OR_CHANGE,
} radix_lock_acquire_kind_t;
```

### 4.3 `radix_range_lock_acquire` 五阶段（树锁实现）

`vmm_radix_tree_lock_range_small_with_big_locked` 与 `lock_range_big_and_small` 都落到 `radix_range_lock_acquire`。它是「区间锁 + 树生长」的核心：在没有下层表页时按需创建，并按 L0 → L1 → L2 → L3 自上而下铺路、自下而上回滚。把它拆成五阶段看：

```text
   入口：root（L0 表已存在），区间 [start, end)

   Phase 1：L0 锁 + L1 表页
     ┌──────────────────────┐
     │  L0 entry (bit-lock)  │ ← 逐项 CAS bitlock；INSERT 时若 L1 表页不存在则
     │  └→ L1 table (alloc) │   pmm_alloc 一页、清零、原子发布到 L0 entry
     └──────────────────────┘
     失败 → phase1_clean_prev：释放已加锁的 L0 项、回收已分配 L1 页

   Phase 2：L1 槽 → L2 表页（不长期持 L1 锁）
     ┌──────────────────────┐
     │  L1 entry            │ ← 只在「写 L1 entry 发布 L2 指针」瞬间持 CAS；
     │  └→ L2 table (alloc) │   L2 表页同样 pmm_alloc + 清零 + 原子发布
     └──────────────────────┘
     失败 → phase2_clean_all：回滚 Phase 1 + Phase 2 已发布项

   Phase 3：L2 锁 + L3 数组（真正的「区间锁」）
     ┌──────────────────────┐
     │  L2 entry (bit-lock) │ ← 按 VA 升序逐条 2 MiB band 加锁；L3 数组（16 KiB）
     │  └→ L3 array (alloc) │   不存在则 pmm_alloc + 清零 + 发布
     └──────────────────────┘
     失败 → phase3_clean_prev：释放本阶段已加 L2 锁、回收 L3 页

   Phase 4：L3 叶级校验
     ┌──────────────────────┐
     │  L3 node（叶子）     │ ← INSERT：检测 overlap（已 VALID/LAZY/owner 则 -E_REND_OVERFLOW）
     │                       │   DELETE：检测 undeletable（须先 unbind，否则 -E_REND_NOFOUND）
     └──────────────────────┘
     失败 → phase3_clean_all：回滚 Phase 3 全部 L2 锁

   Phase 5：计数回填（不可能失败）
     ┌──────────────────────┐
     │  L0/L1/L2 count      │ ← 重新走 L2 walk，按 overlap 页数更新各级 valid/count；
     │  valid / count       │   L0 锁此时仍持有，最后统一释放
     └──────────────────────┘
     成功 → end：返回时调用方持有 L2 band 锁。
     `lock_range_big_and_small` 会在此释放 L0；`lock_range_small_with_big_locked` 保留调用方已持有的 L0。
```

要点：

- **「没有就创建」是 INSERT 的常态**。Phase 1/2/3 都按 `entry_valid` 判断：valid 已发布就只加锁，不重分配；invalid 则 pmm_alloc 一页、清零、CAS 发布指针 + 置 valid，再继续向下。这就是「树锁在没有的时候如何创建新树」。
- **L1 不长期持锁**。Phase 2 注释明说「we do not lock the L1 nodes」——L1 项只在发布 L2 指针那一瞬 CAS，之后立即放掉；区间锁的真正粒度落在 Phase 3 的 L2 bit-lock 上（2 MiB band）。
- **Phase 4 才做语义校验**。前 3 阶段只搭骨架；到 L3 才看叶子是否冲突或可删。INSERT 失败回滚到 phase3_clean_all，DELETE 失败回滚到 phase3_clean_all。
- **Phase 5 不可失败**。计数回填在所有可能失败的步骤之后，因此可以放心复用 L0/L2 遍历迭代器（walk iter），不需要再保留回滚路径。
- **QUERY_OR_CHANGE 走捷径**：Phase 4 不做 overlap / undeletable 校验，Phase 5 直接 `goto end`，只持锁、不改 valid/count——`leaf_bind` / `leaf_unbind` / `change_*` 都走这条。

### 4.4 锁序与 PMM zone 锁

**锁序（禁止逆序）：** L0 big → L2 band → PMM zone → `vspace_lock`。持 PMM zone 锁时 **不要** 调用 `map()` / `unmap()`。

```text
   L0 big lock        L2 band lock       PMM zone lock      vspace_lock
   (512 GiB CAS)     (2 MiB CAS)        (per-zone MCS)     (per-VSpace MCS)
        │                  │                  │                  │
        ▼                  ▼                  ▼                  ▼
   跨多次 utils       mutate / grow       rmap link/unlink     map() / unmap()
   一直持有           叶级校验             Page 元数据           页表项写入
        │                  │                  │                  │
        └──────────────────┴──────────────────┴──────────────────┘
                          升序加锁，降序释放
```

`PMM zone lock` 是 per-zone 的 MCS 锁（MCS = Mellor-Crummey-Scott，一种自旋锁族，每 CPU 在自己的本地变量上自旋以减少总线争用；详见 `31-锁与内存屏障.md`）。它由 `pmm->spin_ptr` + `percpu(pmm_spin_lock[zone_id])` 组成。buddy 的 `pmm_alloc` / `pmm_free` **内部自锁**——调用方不需要、也不应当在外面包 zone 锁再调 alloc/free。zone 锁只用于一个场景：**持锁改 `Page` 元数据（rmap 链表）**。

| 持 zone 锁时 | 是否允许 |
|----|----|
| `radix_leaf_link_rmap` / `unlink_rmap`（改 `Page.rmap_list`） | ✅ 允许 |
| 遍历 `Page.rmap_list` 取快照 | ✅ 允许 |
| `pmm_alloc` / `pmm_free` | ❌ 禁止（会再取同一把锁 → 死锁） |
| `map()` / `unmap()` | ❌ 禁止（可能嵌套 zone 锁） |

调用方统一用 `pmm_lock` / `pmm_unlock` 或 `pmm_zone_lock` / `pmm_zone_unlock`，不要直接展开 `lock_mcs(&pmm->spin_ptr, &percpu(pmm_spin_lock[zone_id]))`——MCS 锁的 `me` 节点（本 CPU 在该锁队列里的槽位）必须按 (zone, cpu) 选对，封装保证一致。

### 4.5 变更操作一览（持锁后做什么）

§4.3 讲的是「锁 + 树生长」的前置阶段；真正改 radix 内容的是下面这些操作，它们都**只在 L3 叶子层**走 walk，不动 L0/L1/L2 结构（结构变更由 §4.3 的 Phase 1–3 负责）。状态转换总览见 §4.1.2。

#### 4.5.1 `insert_range`（INSERT，预约 LAZY）

```text
   for each leaf in [start, end) by 4 KiB:
     leaf->flags = (caller_flags & ~PAGE_ENTRY_VALID) | PAGE_ENTRY_LAZY
     leaf->owner = caller_owner
```

只写叶子的 `flags` 与 `owner`，**不**碰 rmap、**不**碰 PTE。LAZY 表示「已预约、无物理页」。`entry_flags_rm_sw_flags` 在写之前剥掉软件位，避免把 `REMAP` / `COW` 一类软件标志漏到叶子。

#### 4.5.2 `leaf_bind_range`（QUERY_OR_CHANGE，LAZY → VALID）

```text
   for each leaf in [start, end) by 4 KiB, ppn = ppn_first + i:
     校验 leaf 是 LAZY 且非 VALID（否则 rollback）
     radix_leaf_link_rmap(pmm, ppn, leaf)   ← 持 zone 锁加 rmap 链
     leaf->flags = (leaf_flags & ~软件位) | PAGE_ENTRY_VALID
   失败 → rollback：反向 walk，unlink 已加的 rmap，恢复 LAZY
```

`leaf_bind_range` 拒绝同时 VALID|LAZY——这是「先 insert 再 map 再 bind」顺序的硬保证。rmap 链表操作在 zone 锁下做（§4.4）。

#### 4.5.3 `leaf_unbind_range`（QUERY_OR_CHANGE，VALID → LAZY）

```text
   Pass 1（INC）：校验区间内每页都是 VALID，数总数 == page_number
   Pass 2（DEC，从高端 PPN 开始）：
     for each leaf in [end, start) by -4 KiB, ppn = ppn_first + (remain-1):
       radix_leaf_unlink_rmap(pmm, ppn, leaf)   ← 持 zone 锁摘 rmap
       leaf->flags = (leaf->flags & ~软件位) & ~VALID | LAZY
```

**两遍走**：第一遍只校验，第二遍才动手。从高端 PPN 开始拆是为了和 `pmm_free` 的顺序对齐（调用方通常按页升序释放，rmap 先拆高 PPN 减少中间态窗口）。`list_node_is_detached` 防重复摘。

#### 4.5.4 `change_leaf_ppn`（QUERY_OR_CHANGE，COW split / remap）

```text
   leaf = walk(start).curr_l3_node
   校验 leaf 是 VALID
   if old_ppn != new_ppn:
     radix_leaf_unlink_rmap(pmm, old_ppn, leaf)
     err = radix_leaf_link_rmap(pmm, new_ppn, leaf)
     if err: link 回 old_ppn，返回 err   ← 回滚
   leaf->flags = (new_flags & ~软件位) | VALID
```

**单页**操作（不是 range）。先摘旧 rmap 再加新 rmap，失败时把旧 rmap 链回去——这是 COW split 的原子性保证：要么 rmap 指向新页，要么仍指向旧页，不会「两边都没挂」或「两边都挂」。`change_leaf_ppn_flag` 是它的别名。

#### 4.5.5 `change_range_flag`（QUERY_OR_CHANGE，mprotect 批量）

```text
   for each leaf in [start, end) by 4 KiB:
     leaf->flags = new_flags
```

只改叶子 flags，**不**碰 rmap、**不**碰 PPN。调用方负责同步 PTE（`mm_user_utils_set_range_flags` 在 radix 改完后对同 PPN 调 `map()`）。

#### 4.5.6 `query_range`（QUERY_OR_CHANGE，只读）

```text
   leaf = walk(start).curr_l3_node
   if leaf 存在: *out_flags = leaf->flags; *out_owner = leaf->owner
   else: *out_flags = 0; 返回 -E_REND_NOFOUND
```

只读首叶。区间查询的语义是「这段 VA 的第一页是什么状态」，不是「逐页枚举」——枚举用 `find_first_occupied_interval`。

#### 4.5.7 DELETE 路径（结构收缩）

DELETE 不是独立函数，而是 `RADIX_RL_DELETE` 锁路径上的内部行为：

```text
   Phase 4（DELETE）：
     for each l3_node in [start, end):
       if radix_l3_undeletable(l3_node):   ← 仍 VALID（未 unbind）
         return -E_REND_NOFOUND
       radix_node_clear(l3_node)           ← 清叶子（flags/owner/rmap）

   Phase 5（DELETE，delta < 0）：
     for each level in [L2, L1, L0]（自下而上）:
       for each entry in walk:
         old_count = entry.count
         new_count = old_count + delta
         if new_count == 0 && old_count != 0:
           free_level_table(entry.child)   ← 释放子表页
           entry: VALID 清零、count 清零、ptr 清零
         else:
           entry: count += delta
```

**DELETE 必须先 unbind**：Phase 4 检测到仍 VALID 的叶子就拒绝（`-E_REND_NOFOUND`），调用方要先 `leaf_unbind_range` + `unmap` 再走 DELETE。Phase 5 自下而上回填计数：某级 entry 的 count 归零就 `free_level_table` 释放子表页并清指针——这是「树在没有内容时如何收缩」的对称面，与 §4.3 Phase 1–3 的「没有就创建」对偶。

#### 4.5.8 `clean_user` / `delete` / `destroy`（整树拆除）

```text
   clean_user：遍历 L0[0..255]（用户低半），逐级 free_level_table，
              对每个 leaf 调 radix_clean_user_leaf（unbind + unmap + pmm_free）
   delete：    free_level_table(root, L0)   ← 释放 L0 元数据页本身
   destroy：   clean_user + delete
```

`clean_user` **只动低半**（L0[0..255]），高半 L0[256..] 是共享内核模板，随 VSpace 一起销毁但不单独拆。`delete` 释放 L0 根页本身；`destroy` 是二者组合，给 `del_vspace` 用。

---

### 4.6 共享内核高半

`create_vspace` / `init_root_vspace` 调 `vmm_radix_tree_install_shared_kernel_high_half`：L0\[256..\] 共享同一套 L1，不是整表 memcpy。用户清理只走 L0 的 0..255。INSERT 不在 ≥256 的用户路径上生长。

```text
   L0 根表（512 项）
   ┌─────────────────────────────────────────────────────┐
   │ 0..255  │ 用户低半（每地址空间独立，radix + PTE）   │
   ├─────────┼───────────────────────────────────────────┤
   │ 256..   │ 共享内核高半（所有 VSpace 指向同一套 L1） │
   └─────────┴───────────────────────────────────────────┘
                ↑
                │ install_shared_kernel_high_half
                │ 让每个新 VSpace 的 L0[256..] 都指向
                │ root_vspace 已建好的内核 L1 模板
                │（浅拷贝指针，非整表拷贝）
   ┌─────────────────────────────────────────────────────┐
   │  root_vspace  ── L0[256..] ──→ 共享 L1 ──→ ... ──→ 内核页  │
   │  user_vs_A    ── L0[256..] ──↗                                │
   │  user_vs_B    ── L0[256..] ──↗                                │
   └─────────────────────────────────────────────────────┘
   清理用户映射（clean_user / clear_user_mappings）只动 0..255，
   永远不碰 256..——内核高半随 VSpace 一起销毁，不单独拆。
```

### 4.7 按页表根地址挂到 `root_vspace`

`register_vspace` 在 `root_vs` 的红黑树上按 **`vspace_root_addr`** 登记，并置上 `registered` / `root_vs`。`create_vspace` **不会**自动登记——`thread_loader` 或兼容层创建后再调。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `vmm_radix_tree.c` / `.h` | 树、L0/L2 锁、insert/delete/query/change、interval、占用、共享高半 |
| `mm_user_utils.c` / `.h` | fill / clean / remap / set_flags 编排与回滚 |
| `vmm.c` | `clone_vspace`、`register_vspace`、`vspace_clear_user_*` |

API 一律带前缀：`vmm_radix_tree_*`、`mm_user_utils_*`。

---

## 6. 流程

### 6.1 utils 编排顺序（顺序以 `.c` 为准）

本节只给调用方的**编排顺序**——先调谁、后调谁、谁负责回滚。每个操作内部改了 radix 的什么状态、回滚怎么做，见 §4.5；锁的五阶段见 §4.3。

一页从「预约」到「落页」再到「拆除」的生命周期，radix 与 PTE 两面同步推进：

```text
   预约→落页（set_range_and_fill / fill_page_with_exist_range）

     radix:   ── insert_range ──→ [LAZY, 无PPN] ── leaf_bind ──→ [VALID + rmap]
                                          │                            │
                                          │ 调用方 map                  │
                                          ▼                            ▼
     PTE:    ──────────────────────→ [VALID, PPN] ←──── 写入叶 PTE ────┘
                                                                  + zero page

   卸页→删账本（clean_range_and_unfill）

     radix:   [VALID + rmap] ── leaf_unbind ──→ [LAZY, 无PPN] ── DELETE ──→ 空
                      │                                          ↑
                      │ unmap + pmm_free                          │
                      ▼                                          │
     PTE:    [VALID, PPN] ──────────────────→ [无效] ────────────┘

   COW split（remap_page）：先改 radix rmap（change_leaf_ppn），再 map(REMAP)，
   最后释放旧页——不是 unmap+map，避免窗口里出现「无映射」的中间态。
```

各 utils 的编排（细节回链 §4.5）：

| utils | 编排（已持 L0） | 失败回滚 |
|------|------|------|
| `set_range_and_fill` | `pmm_alloc` → L2=INSERT → `insert_range` → 逐页 `map` → `leaf_bind_range` → 释放 L2 → `map_handler_zero_page` | 回滚已 map 前缀 + 删 radix 预约；返回 0 |
| `fill_page_with_exist_range` | 单页：QUERY → `pmm_alloc` → `map` → `leaf_bind` → zero。叶已 VALID → 幂等成功 | 不生长新预约 |
| `clean_range_and_unfill` | `leaf_unbind_range` → 释放 L2 → `unmap` + `pmm_free` → L2=DELETE 清预约 | 失败路径仍 `unlock_range_big` |
| `remap_page` | `change_leaf_ppn` → `map(REMAP)` → 释放旧页 | `map` 失败回滚 radix |
| `set_range_flags` | ABSOLUTE/DELTA/`DELTA_PTE_ONLY`；先改 PTE 再改 radix | 尝试恢复前缀 |

owner 写成 `vs->root_vs ? root_vs : vs`，并带当前 CPU tag。`set_range_flags` 区间内每页须已 VALID + `have_mapped`，且 radix/PTE flags 一致；`DELTA_PTE_ONLY` 用于 fork COW 只改 PTE。

### 6.2 `clone_vspace`（core）

要求带 `VSPACE_CLONE_F_USER_4K_ONLY`，且 `COW_PREP` 与 `COPY_PAGES` **二选一**。源上锁住用户半 L0；按占用 interval：

- 目标侧 INSERT（清除 VALID，变成 LAZY）；
- 源叶已 VALID 且走 COW_PREP：共享 PPN，父/子双方 PTE 只读，radix 父叶与子叶都置 COW；
- COPY_PAGES：新页 + copy + bind + map；
- 纯 LAZY：只 insert，子地址空间继续 demand-page。

失败时：恢复已处理区间源侧的 WRITE / 清 COW，并 `del_vspace` 目标。

### 6.3 与 fault 的协作（兼容层约定，非 core 实现）

1. 查 radix（必要时再 `have_mapped`——它检查 PTE 是否已实际写入，与 radix 的 VALID 区分：radix VALID 表示账本上已绑 PPN，`have_mapped` 表示硬件 PTE 已装好）。
2. 未映射，但在 radix 里且还不是 VALID → 持 L0，调 `fill_page_with_exist_range`。
3. 已映射 + 写故障 + radix 要 WRITE + PTE 只读 → COW split（`remap_page`），**不是** fill。
4. radix 里没有 → 按未映射处理（例如 SEGV）。

正文不要绑定某一具体兼容层目录名。

---

## 7. 公开 API

本篇涉及的接口分布在：`include/rendezvos/mm/vmm_radix_tree.h`、`include/rendezvos/mm/mm_user_utils.h`。接口说明以头文件注释为准，并已与 `vmm_radix_tree.c` / `mm_user_utils.c` 核对。`clone_vspace` / `register_vspace` 的完整生命周期约定见 **07** §7（本篇只写与 radix 的耦合）。硬件 `map`/`unmap` 见 07。

**纠正：** 不存在 `vmm_radix_tree_delete_range`；区间拆除是 `leaf_unbind*` + `RADIX_RL_DELETE` 锁路径上的内部 DELETE，整树拆除用 `clean_user` / `delete` / `destroy`。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序（半开区间 `[start, end)`；`end` 必须来自 `calculate_end_check`） |
|------|------|
| 门禁 | **`vmm_radix_tree_calculate_end_check(start, page_count, &end)`** → 后续锁/变更一律用同一 `end` |
| 兼容层 / 加载器（推荐） | **`lock_range_big`**（可跨多次调用一直持有）→ 选区间（或 `find_first_occupied_interval`）→ **`lock_range_small_with_big_locked(kind)`** → radix/utils → **`unlock_range_small`** → … → **`unlock_range_big`** |
| kmalloc 等短路径 | **`lock_range_big_and_small`**（内部取 L2 后**释放 L0**）→ 工作 → **`unlock_range_big_and_small`** |
| 预约→落页（手动编排） | L2=`RADIX_RL_INSERT` → **`insert_range`** → 调用方 **`map`** → **`leaf_bind(_range)`** → unlock |
| 卸页→删账本 | L2 下 **`leaf_unbind(_range)`** → 调用方 **`unmap`** → L2=`RADIX_RL_DELETE`（内部拆空节点） |
| utils（已持有 L0） | **不**再获取 L0；只用 `lock_range_small_with_big_locked`；拒绝 `&root_vspace` |

`kind`：`RADIX_RL_INSERT` / `RADIX_RL_DELETE` / `RADIX_RL_QUERY_OR_CHANGE`。

### 7.2 Radix 锁与查询

```c
bool vmm_radix_tree_calculate_end_check(vaddr start, size_t page_number, vaddr *end_out);

error_t vmm_radix_tree_lock_range_big(VSpace *, vaddr start, vaddr end);
error_t vmm_radix_tree_unlock_range_big(VSpace *, vaddr start, vaddr end);
error_t vmm_radix_tree_lock_range_small_with_big_locked(struct map_handler *, VSpace *,
        vaddr start, vaddr end, radix_lock_acquire_kind_t kind);
error_t vmm_radix_tree_unlock_range_small(VSpace *, vaddr start, vaddr end);
error_t vmm_radix_tree_lock_range_big_and_small(struct map_handler *, VSpace *,
        vaddr start, vaddr end, radix_lock_acquire_kind_t kind);
error_t vmm_radix_tree_unlock_range_big_and_small(...); /* ≈ unlock_range_small */

bool vmm_radix_tree_find_first_occupied_interval(VSpace *, vaddr search_start,
        vaddr search_end, vaddr *out_start, vaddr *out_end, ENTRY_FLAGS_t *flags_out);
Radix_node_t *vmm_radix_tree_find_first_occupied_leaf(...);
```

| 接口 | 说明 |
|------|------|
| `calculate_end_check` | **唯一**公开 VA 区间门禁（溢出+对齐）；锁/变更 API **不再**重复检查。失败返回 false。 |
| `lock_range_big` | 只获取穿越的 L0 bit-lock（512 GiB 桶）；不生长下层。须配对 `unlock_range_big`。 |
| `lock_range_small_with_big_locked` | 调用方已持有对应 L0；获取 L2 带锁。`INSERT`/`DELETE`/`QUERY_OR_CHANGE` 语义不同。 |
| `lock_range_big_and_small` | 一次性获取 L0+L2，返回时**通常只持 L2**（L0 已释放）。 |
| `find_first_occupied_interval` | 在已持有 L0 下找第一段连续占用；供 clone / `set_range_flags` 选取区间。 |

### 7.3 Radix 变更与生命周期

```c
error_t vmm_radix_tree_insert_range(VSpace *, tagged_ptr_t owner, vaddr start,
                                    ENTRY_FLAGS_t flags, vaddr end);
error_t vmm_radix_tree_leaf_bind_range(VSpace *, vaddr start, ppn_t ppn_first, vaddr end,
                                       ENTRY_FLAGS_t leaf_flags);
error_t vmm_radix_tree_leaf_unbind_range(VSpace *, vaddr start, ppn_t ppn_first, vaddr end);
error_t vmm_radix_tree_leaf_bind / leaf_unbind(...); /* 单页封装 */
error_t vmm_radix_tree_change_leaf_ppn(VSpace *, vaddr, ppn_t old, ppn_t new);
error_t vmm_radix_tree_change_leaf_ppn_flag(...);
error_t vmm_radix_tree_change_range_flag(VSpace *, vaddr start, ENTRY_FLAGS_t, vaddr end);
error_t vmm_radix_tree_query_range(VSpace *, vaddr start, ..., vaddr end);

Radix_entry_t *vmm_radix_tree_init(struct map_handler *, VSpace *); /* 返回 L0 根指针，失败 NULL */
error_t vmm_radix_tree_install_shared_kernel_high_half(VSpace *);
error_t vmm_radix_tree_clean_user(struct map_handler *, VSpace *);
error_t vmm_radix_tree_delete(struct map_handler *, VSpace *);   /* 放 L0 元数据页 */
error_t vmm_radix_tree_destroy(...); /* clean_user + delete */
```

| 接口 | 说明 |
|------|------|
| `insert_range` | 生长元数据并 LAZY 预约叶；`owner` **原样**写入，不按 L0 索引改 owner。须持 `RADIX_RL_INSERT`。 |
| `leaf_bind(_range)` | 叶已 LAZY：置 VALID + rmap；PPN 连续。须在调用方 `map` **之后**。 |
| `leaf_unbind(_range)` | 清 VALID、拆 rmap、恢复 LAZY；实现会校验仍 VALID 但未按约定解绑的路径；DELETE **不按 owner 过滤**。 |
| `change_leaf_ppn*` / `change_range_flag` | 只改影子元数据（+可选 flags）；**不**调 `map`/`unmap`。 |
| `query_range` | 只读影子；不分配。 |
| `init` / `install_shared_kernel_high_half` | 建 L0 元数据页；高半 L0[256..] 共享模板 L1（非整表拷贝）。 |
| `clean_user` / `delete` / `destroy` | 清用户低半；释放 L0 页；或二者组合。 |

### 7.4 `mm_user_utils_*`（编排模板；调用前已持 L0）

```c
vaddr mm_user_utils_set_range_and_fill(VSpace *, vaddr start, size_t page_count, ENTRY_FLAGS_t);
error_t mm_user_utils_fill_page_with_exist_range(VSpace *, vaddr page_va, ENTRY_FLAGS_t);
error_t mm_user_utils_clean_range_and_unfill(VSpace *, vaddr start, size_t page_count, ppn_t ppn_first);
error_t mm_user_utils_remap_page(VSpace *, vaddr page_va, ppn_t new_ppn, ENTRY_FLAGS_t, ppn_t expect_old);
error_t mm_user_utils_set_range_flags(VSpace *, vaddr start, u64 length_bytes,
        mm_user_range_flags_mode_t mode, ENTRY_FLAGS_t set_mask, ENTRY_FLAGS_t clear_mask);
```

| 接口 | 说明 |
|------|------|
| `set_range_and_fill` | L2 INSERT → insert → map 循环 → bind → 释放 L2 → `map_handler_zero_page`。成功返回 `start`，失败 **0**（回滚前缀）。区间内已有可 INSERT 重叠则整次失败。 |
| `fill_page_with_exist_range` | 单页：叶已 **VALID** → 成功空操作（幂等）；否则须已存在且 **LAZY**；其它情况失败（不隐式生长）。 |
| `clean_range_and_unfill` | unbind → 释放 L2 → unmap + `pmm_free` → 再 `RADIX_RL_DELETE`。失败路径须仍 `unlock_range_big`。 |
| `remap_page` | 先 `change_leaf_ppn` 再 `map(..., PAGE_ENTRY_REMAP)`（**不是** unmap+map）。 |
| `set_range_flags` | 区间内每页须已 VALID+`have_mapped`，且 radix/PTE flags 一致；模式见 `MM_USER_RANGE_FLAGS_*`。 |

handler 一律 `&percpu(Map_Handler)`。稀疏多孔 VA 由编排方多次调 utils，不在单次调用里一并处理。

### 7.5 与 07 交叉、本篇不重复展开的符号

`clone_vspace` / `register_vspace` / `unregister_vspace` / `del_vspace` / `vspace_clear_user_mappings` → **07** §7。

---

## 8. 多架构

radix / utils 与 ISA 无关；PTE 标志的编解码在 arch `vmm.c`（见页表篇 §4.5）。`USER_SPACE_TOP` 等常量见各 arch 的 mm 头。

---

## 9. 测试

以 `modules/test/` 现有 `*vmm*` / mm 相关测试用例为准。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```


---

## 10. 限制与后续

- DELETE 路径不按 owner 过滤（只拒绝仍 VALID 的叶子）；头文件不变量第 5 条（I5）已与 `.c` 对齐，见 `vmm_radix_tree.h` 注释。
- `find_first_occupied_*` 等辅助函数与头文件「持锁」叙述若有出入，以 `.c` 为准。
- 多 zone / NUMA 与 rmap 路由见 evolution **E1**。
- fault / COW 具体策略在兼容层，不要把某一上层实现路径写进 core 约定正文。

---

## 11. 变更记录

- 2026-10-05（可读性二轮）：展开首次出现未解释的术语——rmap（反向映射）、VMA（虚拟内存区间）、granule（页粒度）、CAS（Compare-And-Swap）、MCS 锁（Mellor-Crummey-Scott）、`tp_new_none`（空 tagged pointer 标记）、`root_vspace`（§3 首次出现补一句指向 §4.6）、`have_mapped`（§6.3 补一句区分 radix VALID）；「port 的 name_index」改成不展开 IPC 的提醒；「头文件 I5」两次都补成「不变量第 5 条，见头文件注释」；CortenMM 补 SOSP 2025 出处；「interval 发现」→「区间发现」；「walk iter」→「遍历迭代器」。
- 2026-10-05：补 §4.3 五阶段树锁、§4.4 锁序与 zone 锁、§4.5 变更操作一览（含 DELETE 收缩）；顺手校正章节号（原插入把 4.3/4.4 挤到后面）、INSERT overlap 条件（VALID|LAZY|owner）、Phase 5 L0 是否释放（分 big_and_small / with_big_locked）、L2 是 packed bit-lock 不是 mutex。来源：`core/docs/old/memory.md` 迁移审核 + `vmm_radix_tree.c`。
- 2026-10-04：为辅助理解补示意图——§4.1 radix 树与硬件页表对齐（同一段 VA 两边切分一致，radix 记策略意图、PTE 记 MMU 所见，fault 时两面都读但以 radix 判定）；§4.2 L0（512 GiB bit-lock）/ L2（2 MiB mutex）两级锁粒度；§4.3 共享内核高半（L0[256..] 指向同一套 L1 模板，非整表拷贝）；§6.1 一页「预约→落页→卸页→删账本」生命周期里 radix 与 PTE 两面同步推进的流程。
- 2026-10-04：全文中文表述整理——「造一套」→「构建一套」；「搞清」→「理清」；「VA 带」→「VA 区间」；「进 radix/进 PTE」→「存入 radix/存入 PTE」；「落页」→「填页」；「是兼容层调 utils 的事」→「由兼容层调 utils 完成」；「握着/放下/抢」→「持有/释放/获取」；「嵌在别人的」→「嵌套在已持有的」；「剥掉/清掉」→「清除」；「打包字」→「打包的字」；「走法」→「路径」；「持满」→「持有」；「新核」→「其他核」；「别人握着/他核捏着」→「别的核持有/他核持有」；「占用带」→「占用区间」；「包办一切」→「包办一切」保留；「清洁只走」→「清理只走」；「打上」→「置上」；「删掉」→「删除」；「走这条路」→「走此路径」；「grow 新预约」→「生长新预约」；「剥掉 VALID」→「清除 VALID」；「打 COW」→「置 COW」；「del_vspace 掉目标」→「del_vspace 目标」；「手搓」→「手动编排」；「一把做完」→「一次性获取」；「选均匀区间」→「选取区间」；「打 VALID」→「置 VALID」；「实现校验仍 VALID 却未按约定解绑的路径由实现校验」→「实现会校验仍 VALID 但未按约定解绑的路径」；「非整表拷」→「非整表拷贝」；「放 L0 页」→「释放 L0 页」；「均匀」→「一致」；「吞掉」→「一并处理」；「产品策略」→「具体策略」；「勿把」→「不要把」；§7「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-04：§4.1 / §4.2 补 `Radix_entry_t` / `Radix_node_t` / 加锁种类。
- 2026-10-04：§4.4 改题为「按页表根地址挂到 `root_vspace`」（不再以函数名起题）。
- 2026-10-02：§1.1 补「对齐硬件页表树 / 不做独立 VMA」动机。
- 2026-09-27：中文用语整理（真源→以…为准；契约→约定；政策→策略；安装面→MMU 所见；§7「以头文件注释为准」）。
- 2026-09-26：纠正 `fill_page` VALID 幂等与 `clean_range`（unmap/`pmm_free` 在 DELETE 前）与 `.c` 一致。
- 2026-09-26：§7 全文审阅——按头文件注释补接口说明与 L0/L2/utils 编排；删除不存在的 `delete_range`；`clone`/`register` 明确归 07。
- 2026-09-25：语言整理；API 统一为 `vmm_radix_tree_*` / `mm_user_utils_*`；核对 DELETE 与 owner、utils 真实顺序；头文件 I5 已随代码修正。
- 2026-08-29：整篇重做——radix vs PTE；L0/L2 与 clone 窗口；utils 顺序；register ≠ name_index。
- 2026-08-26：v0.1 初稿。
