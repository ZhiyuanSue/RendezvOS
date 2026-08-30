# kmalloc 与内核堆

v0.1 · 2026-08-29

本篇覆盖：`kernel/mm/kmalloc.c`、`kernel/mm/string.c`、`include/rendezvos/mm/kmalloc.h`、`include/rendezvos/mm/allocator.h`。

物理页见 `物理内存与Buddy分配器.md`；`root_vspace` radix/`map` 见 Radix 篇与页表篇；跨核 free 用的 MSQ 见 `04-IPC/无锁队列与EBR设计.md`。

---

## 1. 概述

内核堆是 **每 CPU 一个 `kallocator`（`struct mem_allocator`）**：小对象走 chunk/slot；大块走整页，在 **`&root_vspace`** 上 insert/bind/`map()`——**绝不**走 `mm_user_utils_*`（utils 显式拒绝 root）。

### 1.1 堆也不能再变成那把大锁

无锁 IPC 的常态是：**对象在 A 核分配，消息到 B 核 server，再在 B 上释放**。若堆仍是全局一把锁，业务锁迁走、IPC 无锁之后，瓶颈换个马甲回来。

所以每核本地堆：同核 alloc/free 不抢别人。异核释放时，**不要直接改他核的 chunk 链表**，而是把对象（或整页回收请求）丢进**归属核**的无锁队列，让归属核自己 drain。承认「所有权在分配核」，用和 IPC 同一套 MSQ 哲学归还。

### 1.2 两条跨核队列（以源码为准）

| 队列 | 何时用 | 怎么找到主人 |
|------|--------|----------------|
| `buffer_msq` | 小对象（指针**非** 4K 对齐） | `object_header.allocator_id` |
| `kfree_page_msq` | 整页（指针 **4K 对齐**） | `root_vspace` 上该 VA 的 **radix owner 标签**（插入时写入的 alloc CPU），不是 header |

drain 顺序：先 `kfree_page_msq`，再 `buffer_msq`。触发点：**每次 `kalloc`/`kfree` 入口**，以及 `schedule()` 里的 `kalloc_process_cross_cpu_frees`（闲核也要清）。

---

## 2. 目标与边界

**提供：** per-CPU allocator vtable、chunk 小对象、整页 RB 跟踪、双 MSQ 跨核 free、`m_alloc` 成功路径清零。

**不做：** 用户态 malloc；跨进程共享堆；SLUB sysfs。

**权衡：** 无全局堆锁 ↔ 必须 drain、必须标对 owner；整页与小对象用对齐区分，误判会走错 free 路径。

---

## 3. 分层与调用方

调用：`percpu(kallocator)->m_alloc / m_free`（没有单独导出的 `kalloc()` 符号）。`kinit(cpu_id)` 在 `virt_mm_init`。线程/IPC/port/page_slice 都吃这个堆。

---

## 4. 数据结构与不变量

### 4.1 尺寸

- `1 ≤ Bytes ≤ MIDDLE_PAGE_SIZE`（2 MiB）。  
- slot：约 `8…2048` 共 12 档；**`>2048` → 整页路径**。

### 4.2 chunk / group

- `PAGE_PER_CHUNK == 4`：chunk 起始页对齐，但 **object 载荷刻意非 4K 对齐**，供 free 分支。  
- 每 group：**`full_list` + `empty_list` 两套**（有空位的 chunk 也在 empty 侧计数体系里，不是三套 partial/full/empty 命名）。  
- chunk 元数据**只许 owner CPU 改**；`page_chunk_root` RB 用 `cas_lock`。

### 4.3 整页与 `root_vspace`

`pmm_alloc` → radix `insert_range`（owner tag = 本 CPU）→ `map` → `leaf_bind` → 可选 zero。跟踪节点 `page_chunk_node` 本身也来自小对象分配。free：unbind → unmap → DELETE 锁模式清叶 → `pmm_free`。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `kmalloc.h` / `kmalloc.c` | mem_allocator、双 MSQ、chunk/页路径、`kinit` |
| `allocator.h` | vtable |
| `string.c` | `memcpy`/`memset` 等（工具，非堆策略） |

---

## 6. 流程

### 6.1 `m_alloc`

1. `mem_allocator_remote_frees(this)`（先清积压）。  
2. `>2048`：分配 `page_chunk_node` → `core_get_free_pages` → memset → RB 插入。  
3. 否则：从 group/`empty_list` 取；可偷别 group 的全空 chunk；再不行 `core_get_free_pages(PAGE_PER_CHUNK)` 建 chunk → `chunk_get_obj`。

### 6.2 `m_free`

1. 先 remote_frees。  
2. **页对齐** → 查 radix owner CPU；异核则 `kfree_page_msq` 投递；本核 `kfree_page_local`。  
3. **非对齐** → 读 `allocator_id`；本核 `group_free_obj`（可回收多余空 chunk）；异核 `buffer_msq`。

### 6.3 与 IPC/调度

消息/TCB 常跨核释放 → 依赖本篇 MSQ；idle 循环 `schedule` 保证低分配核也会 drain。

---

## 7. 公开 API

```c
/* 使用方式 */
percpu(kallocator)->m_alloc(percpu(kallocator), n);
percpu(kallocator)->m_free(percpu(kallocator), p);

void kinit(cpu_id_t cpu_id);
void kalloc_process_cross_cpu_frees(void); /* schedule 调用 */
```

`DEFINE_PER_CPU(struct allocator *, kallocator)` 在 `kmalloc.c`。

---

## 8. 多架构

逻辑与 ISA 无关；依赖 arch 原子与 `root_vspace` 映射。

---

## 9. 测试

kmalloc/SMP 相关测例；`make ARCH=x86_64 config && make all && make run`。本篇未复测。

---

## 10. 限制与后续

- owner 标错 / 对齐误判 → 错队列或泄漏。  
- 引导期 `tmp_k_alloctor` 自举后再换成正式 percpu 实例。  
- 与 radix DELETE/owner 过滤的头文件叙述差异，以 `.c` 为准（见 Radix 篇）。

---

## 11. 变更记录

- 2026-08-29：整篇重做——双 MSQ、页走 radix owner、drain 在 kalloc/kfree/schedule；纠正 group 列表命名；堆不再变大锁。
- 2026-08-26：v0.1 初稿；曾定点补 percpu 动机。
