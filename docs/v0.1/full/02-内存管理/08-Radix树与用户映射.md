# Radix 树与用户映射

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/vmm_radix_tree.c`、`kernel/mm/mm_user_utils.c`、`include/rendezvos/mm/vmm_radix_tree.h`、`include/rendezvos/mm/mm_user_utils.h`；与 clone / register 紧耦合处见 `kernel/mm/vmm.c`（完整 VSpace 生命周期仍以 `07-虚拟地址空间与页表.md` 为准）。

硬件 PTE 怎么改见同分区页表篇。page fault 填页 / 拆页的**政策**在兼容层；本篇只钉 core 侧契约。`register_vspace` 按页表根地址挂红黑树，和 port 的字符串 `name_index` 不是一回事。

---

## 1. 概述

用户地址空间里，**radix 是映射账本（真源）**：哪段 VA 已预约（LAZY）、权限意图、owner、以及和物理页之间的 rmap。硬件 PTE 只是 **安装面**——MMU 此刻看见什么。core **不再**造一套 Linux 式 VMA 链表；兼容层在 radix + `map`/`unmap` 之上实现 mmap / fault / COW 政策。

这样做是为了避免「双真源」漂移：如果只看 PTE，分不清「已预约还没落页」和「根本没映射」；fork 做完 COW 准备之后，PTE 往往是只读的，但 radix 里仍记着「意图可写 + COW」——写故障时该不该拆页，必须读 radix，不能只信 PTE。代价是调用方要搞清 L0/L2 锁模型，以及 `mm_user_utils_*` 的编排顺序；公开 VA 带还要先过 `vmm_radix_tree_calculate_end_check`。

core 里的 `clone_vspace` 已经能做 `COW_PREP` / `COPY_PAGES`；真正在 fault 里填页或拆页，是兼容层调 utils 的事。

---

## 2. 目标与边界

提供：radix 树与 L0/L2 锁；INSERT / DELETE / QUERY 等语义；`mm_user_utils_*` 把 PMM、PTE、radix、rmap 编成可回滚的序列；`clone_vspace` 的用户半拷贝 / COW 准备；共享内核高半安装；`register_vspace`。

不做：page fault 政策本身；swap。DELETE 不按 owner 过滤——只拒绝仍带 VALID 的叶子（须先 unbind）；头文件 I5 已按此书写。

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

### 4.4 `register_vspace`

在 `root_vs` 的红黑树上按 **`vspace_root_addr`** 注册，并打上 `registered` / `root_vs`。`create_vspace` **不会**自动注册——`thread_loader` 或兼容层创建后再注册。

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
- 源叶已 VALID 且走 COW_PREP：共享 PPN，双方 PTE 只读，radix 两侧打 COW；
- COPY_PAGES：新页 + copy + bind + map；
- 纯 LAZY：只 insert，子地址空间继续 demand-page。

失败时：恢复已处理区间源侧的 WRITE / 清 COW，并 `del_vspace` 掉目标。

### 6.3 与 fault 的协作（兼容层契约，非 core 实现）

1. 查 radix（必要时再 `have_mapped`）。
2. 未映射，但在 radix 里且还不是 VALID → 持 L0，调 `fill_page_with_exist_range`。
3. 已映射 + 写故障 + radix 要 WRITE + PTE 只读 → COW split（`remap_page`），**不是** fill。
4. radix 里没有 → 按未映射处理（例如 SEGV）。

正文不要绑定某一具体兼容层目录名。

---

## 7. 公开 API

```c
Radix_entry_t *vmm_radix_tree_init(...); /* 返回根，不是 bool */

bool vmm_radix_tree_calculate_end_check(vaddr start, size_t page_count, vaddr *end);
error_t vmm_radix_tree_lock_range_big(...);
error_t vmm_radix_tree_lock_range_small_with_big_locked(...);
error_t vmm_radix_tree_lock_range_big_and_small(...); /* 返回时通常只持 L2 */

error_t vmm_radix_tree_insert_range(...);
error_t vmm_radix_tree_delete_range(...); /* 名称以头文件为准 */
error_t vmm_radix_tree_query_range(...);
error_t vmm_radix_tree_change_range_flag(...);
error_t vmm_radix_tree_change_leaf_ppn(...);
error_t vmm_radix_tree_find_first_occupied_interval(...);

vaddr mm_user_utils_set_range_and_fill(...); /* 失败返回 0 */
error_t mm_user_utils_fill_page_with_exist_range(...);
error_t mm_user_utils_clean_range_and_unfill(...);
error_t mm_user_utils_remap_page(...);
error_t mm_user_utils_set_range_flags(...);

error_t clone_vspace(VSpace *src, VSpace **dst, u64 flags);
error_t register_vspace(VSpace *, VSpace *root_vs);
```

完整参数与 `@note` 以头文件为准。

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
- fault / COW 产品策略在兼容层，勿把某一上层实现路径写进 core 契约正文。

---

## 11. 变更记录

- 2026-09-25：语言整理；API 统一为 `vmm_radix_tree_*` / `mm_user_utils_*`；核对 DELETE 与 owner、utils 真实顺序；头文件 I5 已随代码修正。
- 2026-08-29：整篇重做——radix vs PTE；L0/L2 与 clone 窗口；utils 顺序；register ≠ name_index。
- 2026-08-26：v0.1 初稿。
