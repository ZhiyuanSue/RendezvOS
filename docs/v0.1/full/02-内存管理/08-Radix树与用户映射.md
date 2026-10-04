# Radix 树与用户映射

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/vmm_radix_tree.c`、`kernel/mm/mm_user_utils.c`、`include/rendezvos/mm/vmm_radix_tree.h`、`include/rendezvos/mm/mm_user_utils.h`；与 clone / register 紧耦合处见 `kernel/mm/vmm.c`（完整 VSpace 生命周期仍以 `07-虚拟地址空间与页表.md` 为准）。

硬件 PTE 怎么改见同分区页表篇。page fault 填页 / 拆页的**策略**在兼容层；本篇只钉 core 侧约定。`register_vspace` 按页表根地址挂红黑树，和 port 的字符串 `name_index` 不是一回事。

---

## 1. 概述

用户地址空间里，**radix 是映射账本（以它为准）**：哪段 VA 已预约（LAZY）、权限意图、owner、以及和物理页之间的 rmap。硬件 PTE 只是 **MMU 此刻所见**——装进页表、供翻译用的那一面。core **不再**造一套 Linux 式 VMA 链表；兼容层在 radix + `map`/`unmap` 之上实现 mmap / fault / COW 策略。

### 1.1 为何对齐硬件页表树、而不是另造 VMA

软件 radix 的级数 / 扇出刻意贴近「4 KiB granule、每级 9 bit」的硬件翻译树：同一段 VA 在 radix 与 PTE 上切分方式一致，fork / fault / unmap 扫区间时少做「VMA 合并再拆到页」的二次翻译。代价是调用方要搞清 L0/L2 锁，以及 `mm_user_utils_*` 的编排；公开 VA 带还要先过 `vmm_radix_tree_calculate_end_check`。

若像经典宏内核那样维护独立 VMA 抽象 + 另一套页表 walker，会出现双账本漂移（CortenMM 一类讨论里常见的「策略层与硬件层各记一套」）。本仓库选择：**策略意图进 radix，落地翻译进 PTE**，fault 时两面都读，但以 radix 判定「该不该填 / 该不该 COW」。

这样做还避免「只看 PTE」分不清「已预约还没落页」和「根本没映射」；fork 做完 COW 准备之后，PTE 往往是只读的，但 radix 里仍记着「意图可写 + COW」——写故障时必须读 radix。

core 里的 `clone_vspace` 已经能做 `COW_PREP` / `COPY_PAGES`；真正在 fault 里填页或拆页，是兼容层调 utils 的事。

---

## 2. 目标与边界

提供：radix 树与 L0/L2 锁；INSERT / DELETE / QUERY 等语义；`mm_user_utils_*` 把 PMM、PTE、radix、rmap 编成可回滚的序列；`clone_vspace` 的用户半拷贝 / COW 准备；共享内核高半安装；`register_vspace`。

不做：page fault 策略本身；swap。DELETE 不按 owner 过滤——只拒绝仍带 VALID 的叶子（须先 unbind）；头文件 I5 已按此书写。

---

## 3. 分层与调用方

- **兼容层 / 加载器**：通常先 `vmm_radix_tree_lock_range_big`，再多次 `lock_range_small_with_big_locked` + utils；L0 可以跨多次调用一直握着。utils **不会**自己去抢 L0。
- **kmalloc 等**：可用 `vmm_radix_tree_lock_range_big_and_small`（内部取到 L2 后**放下 L0**），避免嵌在别人的 L0 临界区里。
- **utils**：一律拒绝 `&root_vspace`（只服务用户地址空间）。调用方已持 big 时，用 `lock_range_small_with_big_locked` 那条路径。

推荐生命周期：`insert_range`（LAZY）→ 调用方 `map` → `leaf_bind`（VALID + rmap）→ … → unbind → unmap → DELETE。

---

## 4. 数据结构与不变量

### 4.1 叶子与标志

- `PAGE_ENTRY_LAZY`：已预约，尚无 VALID / PPN。`insert_range` **强制**带上 LAZY，并剥掉 VALID。
- `leaf_bind`：拒绝同时 VALID|LAZY；成功后置 VALID、清 LAZY、挂 rmap；**不改写** `owner`。
- COW：radix 可以保留 `WRITE | PAGE_ENTRY_COW`；PTE 常被清掉 WRITE（`VSPACE_CLONE_F_COW_PREP`，或 `set_range_flags` 的 `DELTA_PTE_ONLY`）。

### 4.2 L0 / L2 锁

L0 按大约 512 GiB（`HUGE_PAGE_SIZE`）一档，管「谁在驱动这段 VA 跨度」、支撑长 interval 发现。L2 按 2 MiB 一档，真正做 mutate / grow、改叶标志、bind / rmap。跨多档 L2 时升序加锁、降序释放。

两种常见入口：

1. `lock_range_big` +（多次）`lock_range_small_with_big_locked` — 加载器 / fault / mmap 的走法；L0 跨多次 utils。
2. `lock_range_big_and_small` — 内部持住 L2 后**放下 L0**；clone 目标插入、kmalloc 整页映射等会用。

**Clone 窗口（源码强注释）：** 在源上持满用户半的 L0，可以挡住**新核再进入**该 radix，但**不表示**扫 interval 时没有别人握着 L2——他核可能已经走 `big_and_small` 放下了 L0、手里还捏着 L2。查找代码扫占用带时必须 **取 L2 并等待**。不要理解成「握着 L0 就包办一切」。

`vmm_radix_tree_calculate_end_check(start, page_count, &end)`：公开 VA 带的唯一门禁（溢出 + 页对齐）。锁和变更 API **不再**重复这道检查；调用方必须把同一个 `vaddr_end` 贯穿整个临界区。

### 4.3 共享内核高半

`create_vspace` / `init_root_vspace` 调 `vmm_radix_tree_install_shared_kernel_high_half`：L0\[256..\] 共享同一套 L1，不是整表 memcpy。用户清洁只走 L0 的 0..255。INSERT 不在 ≥256 的用户路径上生长。

### 4.4 按页表根地址挂到 `root_vspace`

`register_vspace` 在 `root_vs` 的红黑树上按 **`vspace_root_addr`** 登记，并打上 `registered` / `root_vs`。`create_vspace` **不会**自动登记——`thread_loader` 或兼容层创建后再调。

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

### 6.1 utils（顺序以 `.c` 为准）

`mm_user_utils_set_range_and_fill`：**先 `pmm_alloc`**，再（在调用方已持 L0 的前提下）`lock_range_small_with_big_locked(INSERT)` → `insert_range` → 逐页 `map` → `leaf_bind_range` → 解锁 → 用 `map_handler_zero_page` 清零（不要在错误的 CR3 下对用户 VA 直接 `memset`）。中途失败会回滚已 map 的前缀并尝试删掉 radix 预约。owner 写成 `vs->root_vs ? root_vs : vs`，并带上当前 CPU 的 tag。

`mm_user_utils_fill_page_with_exist_range`：针对**已有 LAZY** 的一页——QUERY → alloc → map → bind → zero。若 radix 已是 VALID，当作**成功空操作**（幂等）。不 grow 新预约。

`mm_user_utils_clean_range_and_unfill`：先 unbind → 解锁 → unmap + `pmm_free` → 再以 DELETE 清预约。unmap 失败时的策略见源码（可能保留 L0、不再 DELETE 余下）。

`mm_user_utils_remap_page`：**先**在 radix/rmap 上 `change_leaf_ppn`，再 `map(..., REMAP)`，最后释放旧页；`map` 失败要回滚 radix。COW split 走这条路。

`mm_user_utils_set_range_flags`：支持 ABSOLUTE / DELTA / **DELTA_PTE_ONLY**（fork COW 时只改 PTE）。先改 PTE 再改 radix；失败则尝试恢复前缀。

### 6.2 `clone_vspace`（core）

要求带 `VSPACE_CLONE_F_USER_4K_ONLY`，且 `COW_PREP` 与 `COPY_PAGES` **二选一**。源上锁住用户半 L0；按占用 interval：

- 目标侧 INSERT（VALID 剥掉，变成 LAZY）；
- 源叶已 VALID 且走 COW_PREP：共享 PPN，父/子双方 PTE 只读，radix 父叶与子叶都打 COW；
- COPY_PAGES：新页 + copy + bind + map；
- 纯 LAZY：只 insert，子地址空间继续 demand-page。

失败时：恢复已处理区间源侧的 WRITE / 清 COW，并 `del_vspace` 掉目标。

### 6.3 与 fault 的协作（兼容层约定，非 core 实现）

1. 查 radix（必要时再 `have_mapped`）。
2. 未映射，但在 radix 里且还不是 VALID → 持 L0，调 `fill_page_with_exist_range`。
3. 已映射 + 写故障 + radix 要 WRITE + PTE 只读 → COW split（`remap_page`），**不是** fill。
4. radix 里没有 → 按未映射处理（例如 SEGV）。

正文不要绑定某一具体兼容层目录名。

---

## 7. 公开 API

本篇拥有：`include/rendezvos/mm/vmm_radix_tree.h`、`include/rendezvos/mm/mm_user_utils.h`。接口说明以头文件注释为准，并已与 `vmm_radix_tree.c` / `mm_user_utils.c` 核对。`clone_vspace` / `register_vspace` 的完整生命周期约定见 **07** §7（本篇只写与 radix 的耦合）。硬件 `map`/`unmap` 见 07。

**纠正：** 不存在 `vmm_radix_tree_delete_range`；区间拆除是 `leaf_unbind*` + `RADIX_RL_DELETE` 锁路径上的内部 DELETE，整树拆除用 `clean_user` / `delete` / `destroy`。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序（半开区间 `[start, end)`；`end` 必须来自 `calculate_end_check`） |
|------|------|
| 门禁 | **`vmm_radix_tree_calculate_end_check(start, page_count, &end)`** → 后续锁/变更一律用同一 `end` |
| 兼容层 / 加载器（推荐） | **`lock_range_big`**（可跨多次调用一直握着）→ 选区间（或 `find_first_occupied_interval`）→ **`lock_range_small_with_big_locked(kind)`** → radix/utils → **`unlock_range_small`** → … → **`unlock_range_big`** |
| kmalloc 等短路径 | **`lock_range_big_and_small`**（内部取 L2 后**放下 L0**）→ 工作 → **`unlock_range_big_and_small`** |
| 预约→落页（手搓） | L2=`RADIX_RL_INSERT` → **`insert_range`** → 调用方 **`map`** → **`leaf_bind(_range)`** → unlock |
| 卸页→删账本 | L2 下 **`leaf_unbind(_range)`** → 调用方 **`unmap`** → L2=`RADIX_RL_DELETE`（内部拆空节点） |
| utils（已持 L0） | **不**再抢 L0；只用 `lock_range_small_with_big_locked`；拒绝 `&root_vspace` |

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
| `calculate_end_check` | **唯一**公开 VA 带门禁（溢出+对齐）；锁/变更 API **不再**重复检查。失败返回 false。 |
| `lock_range_big` | 只抢穿越的 L0 bit-lock（512 GiB 桶）；不生长下层。须配对 `unlock_range_big`。 |
| `lock_range_small_with_big_locked` | 调用方已持对应 L0；抢 L2 带锁。`INSERT`/`DELETE`/`QUERY_OR_CHANGE` 语义不同。 |
| `lock_range_big_and_small` | 一把做完 L0+L2，返回时**通常只持 L2**（L0 已放）。 |
| `find_first_occupied_interval` | 在已持 big 下找第一段连续占用；供 clone / `set_range_flags` 选均匀区间。 |

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
| `leaf_bind(_range)` | 叶已 LAZY：打 VALID + rmap；PPN 连续。须在调用方 `map` **之后**。 |
| `leaf_unbind(_range)` | 清 VALID、拆 rmap、恢复 LAZY；**拒绝仍 VALID 却未按约定解绑的路径由实现校验**；DELETE **不按 owner 过滤**。 |
| `change_leaf_ppn*` / `change_range_flag` | 只改影子元数据（+可选 flags）；**不**调 `map`/`unmap`。 |
| `query_range` | 只读影子；不分配。 |
| `init` / `install_shared_kernel_high_half` | 建 L0 元数据页；高半 L0[256..] 共享模板 L1（非整表拷）。 |
| `clean_user` / `delete` / `destroy` | 清用户低半；放 L0 页；或二者组合。 |

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
| `set_range_and_fill` | L2 INSERT → insert → map 循环 → bind → 放 L2 → `map_handler_zero_page`。成功返回 `start`，失败 **0**（回滚前缀）。区间内已有可 INSERT 重叠则整次失败。 |
| `fill_page_with_exist_range` | 单页：叶已 **VALID** → 成功空操作（幂等）；否则须已存在且 **LAZY**；其它情况失败（不隐式生长）。 |
| `clean_range_and_unfill` | unbind → 放 L2 → unmap + `pmm_free` → 再 `RADIX_RL_DELETE`。失败路径须仍 `unlock_range_big`。 |
| `remap_page` | 先 `change_leaf_ppn` 再 `map(..., PAGE_ENTRY_REMAP)`（**不是** unmap+map）。 |
| `set_range_flags` | 区间内每页须已 VALID+`have_mapped`，且 radix/PTE flags 均匀；模式见 `MM_USER_RANGE_FLAGS_*`。 |

handler 一律 `&percpu(Map_Handler)`。稀疏多孔 VA 由编排方多次调 utils，不在单次调用里吞掉。

### 7.5 与 07 交叉、本篇不重复展开的符号

`clone_vspace` / `register_vspace` / `unregister_vspace` / `del_vspace` / `vspace_clear_user_mappings` → **07** §7。

---

## 8. 多架构

radix / utils 与 ISA 无关；PTE 标志的编解码在 arch `vmm.c`（见页表篇 §4.5）。`USER_SPACE_TOP` 等常量见各 arch 的 mm 头。

---

## 9. 测试

以 `modules/test/` 现有 `*vmm*` / mm 相关测例为准。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```

本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- DELETE 路径不按 owner 过滤（只拒绝仍 VALID 的叶子）；头文件 I5 已与 `.c` 对齐。
- `find_first_occupied_*` 等辅助函数与头文件「持锁」叙述若有出入，以 `.c` 为准。
- 多 zone / NUMA 与 rmap 路由见 evolution **E1**。
- fault / COW 产品策略在兼容层，勿把某一上层实现路径写进 core 约定正文。

---

## 11. 变更记录

- 2026-10-04：§4.4 改题为「按页表根地址挂到 `root_vspace`」（不再以函数名起题）。
- 2026-10-02：§1.1 补「对齐硬件页表树 / 不做独立 VMA」动机。
- 2026-09-27：中文用语整理（真源→以…为准；契约→约定；政策→策略；安装面→MMU 所见；§7「以头文件注释为准」）。
- 2026-09-26：纠正 `fill_page` VALID 幂等与 `clean_range`（unmap/`pmm_free` 在 DELETE 前）与 `.c` 一致。
- 2026-09-26：§7 全文审阅——按头文件注释补接口说明与 L0/L2/utils 编排；删除不存在的 `delete_range`；`clone`/`register` 明确归 07。
- 2026-09-25：语言整理；API 统一为 `vmm_radix_tree_*` / `mm_user_utils_*`；核对 DELETE 与 owner、utils 真实顺序；头文件 I5 已随代码修正。
- 2026-08-29：整篇重做——radix vs PTE；L0/L2 与 clone 窗口；utils 顺序；register ≠ name_index。
- 2026-08-26：v0.1 初稿。
