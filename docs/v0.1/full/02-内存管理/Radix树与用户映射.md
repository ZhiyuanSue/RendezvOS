# Radix 树与用户映射

v0.1 · 2026-08-29

本篇覆盖：`kernel/mm/vmm_radix_tree.c`、`kernel/mm/mm_user_utils.c`、`include/rendezvos/mm/vmm_radix_tree.h`、`include/rendezvos/mm/mm_user_utils.h`；与 clone/register 紧耦合处见 `kernel/mm/vmm.c`（交叉说明，完整 VSpace 生命周期仍以 `虚拟地址空间与页表.md` 为准）。

PTE 手术见同分区页表篇；故障策略在兼容层（本篇只钉 core 契约）。名称字符串索引与 `register_vspace` **不是**同一机制——后者按页表根地址挂 RB，见文末。

---

## 1. 概述

用户地址空间里，**radix 是映射账本（真源）**：哪段 VA 已预约（LAZY）、权限意图、owner、与物理页的 rmap。硬件 PTE 是 **安装面**——MMU 此刻看见什么。core **不**再造一套 Linux 式 VMA；兼容层在 radix + `map`/`unmap` 之上实现 mmap/fault/COW 政策。

**为何有效：**

| 问 | 答 |
|----|----|
| 面对什么？ | 仅看 PTE 分不清「已预约未落页」与「未映射」；COW 后 PTE 只读但权限意图仍是可写——只信 PTE 会拆错页 |
| 对比？ | 双真源（VMA+PTE）易漂移；本设计单一账本 + 强制同锁临界区改 radix 与 PTE |
| 代价？ | L0/L2 锁模型与 utils 编排顺序；调用方必须 `calculate_end_check` |
| 反例？ | fork COW：radix 留 `WRITE\|COW`，PTE 清 WRITE；fault 写路径读 radix 决定 split，不能只看 PTE |
| 咬合？ | `clone_vspace` 在 core 已做 COW_PREP/COPY；fault 填页/拆页在兼容层调 utils |

---

## 2. 目标与边界

**提供：** radix 树与 L0/L2 锁；INSERT/DELETE/QUERY 语义；`mm_user_utils` 编排（PMM±PTE±radix±rmap）；`clone_vspace` 的用户半拷贝/COW 准备；共享内核高半安装；`register_vspace`。

**不做：** page fault 政策（compat）；swap；按 owner 过滤 DELETE（头文件 I5 若仍写「只清匹配 owner」，**实现是清扫路径上叶子，无 owner 过滤**——以 `.c` 为准）。

---

## 3. 分层与调用方

- **兼容层 / 加载器** — 持 L0 编排跨区间；调 utils（utils **不**自取 L0）。
- **kmalloc 等** — 可用 `lock_range_big_and_small`（取 L0→持 L2→**放 L0**），避免嵌在别人的 L0 下。
- **utils** — 拒 `&root_vspace`（只服务用户 AS）；调用方已持 big 时用 `lock_range_small_with_big_locked`。

推荐生命周期：`INSERT`(LAZY) → 调用方 `map` → `leaf_bind`(VALID+rmap) → … → unbind → unmap → `DELETE`。

---

## 4. 数据结构与不变量

### 4.1 叶子与标志

- `PAGE_ENTRY_LAZY`：已预约、尚无 VALID/PPN。`insert_range` **强制** LAZY 并剥 VALID。
- bind：拒绝同时 VALID|LAZY；成功后 VALID，清 LAZY，挂 rmap。
- COW：radix 可保留 `WRITE|PAGE_ENTRY_COW`；PTE 常被清 WRITE（`VSPACE_CLONE_F_COW_PREP` / `DELTA_PTE_ONLY`）。

### 4.2 L0 / L2

| | L0 | L2 |
|--|----|----|
| 粒度 | 512 GiB 档（`HUGE_PAGE_SIZE`） | 2 MiB 档 |
| 作用 | 准入/串行化谁驱动该 VA 跨度；支撑长 interval 发现 | 真正 mutate/grow、叶标志、bind/rmap |
| 多档 | — | 升序加锁 / 降序释放 |

**两种入口：**

1. `lock_range_big` +（多次）`lock_range_small_with_big_locked` — 加载器/fault/mmap 走法；L0 跨多次 utils。
2. `lock_range_big_and_small` — 内部持 L2 后**放下 L0**；clone 目标插入、kmalloc 用。

**Clone 窗口（源码强注释）：** 源上持满用户 L0 **阻止新核进入**该 radix，但**不表示**扫 interval 时无 L2——他核可能已 `big_and_small` 放下 L0 仍握 L2。查找代码必须在扫带时 **取 L2 并等待**。不是「L0 包办一切」。

`calculate_end_check`：公开 VA 带唯一门禁（溢出+页对齐）。锁/变更 API **不**再验；调用方须同一 `vaddr_end`。

### 4.3 共享内核高半

`create_vspace`：`install_shared_kernel_high_half`（L0[256..] 共享 L1，非整表 memcpy）。用户清洁只走 L0 0..255。INSERT 不在 ≥256 生长用户路径。

### 4.4 `register_vspace`

在 `root_vs` 的 RB 上按 **`vspace_root_addr`** 注册；设 `registered`/`root_vs`。`create_vspace` **不**自动注册——`thread_loader` / 兼容层创建后再注册。与 port 的字符串 `name_index` 无关。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `vmm_radix_tree.c` | 树、锁、insert/delete/query/change、interval 查找、占用 |
| `mm_user_utils.c` | fill/clean/remap/set_flags 编排与回滚 |
| `vmm.c` | `clone_vspace`、`register_vspace`、共享高半、`vspace_clear_user_*` |

---

## 6. 流程

### 6.1 utils 要点（以代码顺序为准）

| API | 要点 |
|-----|------|
| `set_range_and_fill` | **先 PMM**，再 L2 INSERT→insert→map→bind→解锁→`map_handler_zero_page`（勿在错误 CR3 下 `memset` 用户 VA）。重叠则整调用失败并回滚。 |
| `fill_page_with_exist_range` | 已有 LAZY：QUERY→alloc→map→bind→zero。**已 VALID → 成功空操作**（幂等）。不 grow。 |
| `clean_range_and_unfill` | unbind → 解锁 → unmap+`pmm_free` → 再 DELETE 清预约。unmap 失败策略见源码（可能保留 L0、不再 DELETE 余下）。 |
| `remap_page` | **先** radix/rmap `change_leaf_ppn`，再 `map(..., REMAP)`，再释放旧页；map 失败回滚 radix。COW split 走此路径。 |
| `set_range_flags` | ABSOLUTE / DELTA / **DELTA_PTE_ONLY**（fork COW 只改 PTE）。先 PTE 后 radix；失败前缀恢复。 |

owner：`set_range_and_fill` 用 `vs->root_vs ? root_vs : vs`（带 cpu tag）。头文件若写「按 L0 下标自动选 owner」视为 aspiration，以传入 `owner_info` 的实现为准。

### 6.2 `clone_vspace`（core）

要求 `USER_4K_ONLY` 且 `COW_PREP` / `COPY_PAGES` 二选一。源 L0 锁用户半；按 occupied interval：

- dst INSERT（VALID 剥掉 → LAZY）；
- 源叶 VALID + COW_PREP：共享 PPN、双方 PTE RO，radix 两侧打 COW；
- COPY：新页+copy+bind+map；
- 纯 LAZY：只 insert，子仍 demand-page。

失败：恢复已处理区间源侧 WRITE/清 COW，并 `del_vspace` dst。

### 6.3 与 fault 的协作（compat 契约，非 core 实现）

1. 查 radix（+ `have_mapped`）。  
2. 未映射但在 radix 且非 VALID → L0 + `fill_page_with_exist_range`。  
3. 已映射 + 写故障 + radix 要 WRITE + PTE RO → COW split（`remap`），**不是** fill。  
4. 无 radix → SEGV。

---

## 7. 公开 API

```c
Radix_entry_t *vmm_radix_tree_init(...); /* 非 bool */

error_t calculate_end_check(vaddr start, vaddr *end);
error_t lock_range_big(...);
error_t lock_range_small_with_big_locked(...);
error_t lock_range_big_and_small(...); /* 返回时通常只持 L2 */

error_t insert_range(...);
error_t delete_range(...);
error_t query_range(...);
error_t change_range_flag(...);
error_t change_leaf_ppn(...);
error_t find_first_occupied_interval(...); /* 扫带时持 L2；leaf 单点 helper 与头文件锁叙述可能不一致，以 .c 为准 */

error_t set_range_and_fill(...);
error_t fill_page_with_exist_range(...);
error_t clean_range_and_unfill(...);
error_t remap_page(...);
error_t set_range_flags(...);

error_t clone_vspace(VSpace *src, VSpace **dst, u64 flags);
error_t register_vspace(VSpace *);
```

签名以头文件为准。

---

## 8. 多架构

radix/utils 与 ISA 无关；PTE 标志编解码在 arch `vmm.c`。用户顶 `USER_SPACE_TOP` 等常量见 arch/mm 头。

---

## 9. 测试

旧名 `*nexus*` 测例可能已更名/移除；以 `modules/test/` 现有 `*vmm*` / mm 测例为准。`make ARCH=x86_64 config && make all && make run`。本篇未复测。

---

## 10. 限制与后续

- DELETE 无按 owner 过滤（与部分头注释不一致）→ 记 evolution 或改注释。  
- `find_first_occupied_leaf` 与头文件「持 L2」叙述可能过时。  
- 多 zone / NUMA 与 rmap 路由见 **E1**。  
- fault/COW 产品策略在兼容层，勿把 linux_layer 路径写进 core 契约正文。

---

## 11. 变更记录

- 2026-08-29：整篇重做——radix vs PTE 五问；L0/L2 与 clone 窗口；utils 真实顺序（PMM 先、VALID 幂等）；纠正 I5/行数/返回类型/测例名；register ≠ name_index；core 已含 COW_PREP。
- 2026-08-29：曾定点补丁（已被本整篇吸收）。
- 2026-08-26：v0.1 初稿。
