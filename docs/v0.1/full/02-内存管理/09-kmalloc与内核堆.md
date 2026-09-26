# kmalloc 与内核堆

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/kmalloc.c`、`kernel/mm/string.c`、`include/rendezvos/mm/kmalloc.h`、`include/rendezvos/mm/allocator.h`。

物理页见 `06-物理内存与Buddy分配器.md`；在 `root_vspace` 上挂整页时用的 radix / `map` 见 Radix 篇与页表篇；跨核 free 用的 MSQ 机制见 `04-IPC/22-无锁队列与EBR设计.md`。

---

## 1. 概述

内核堆是 **每 CPU 一个 `kallocator`（`struct mem_allocator`）**：小对象走 chunk / slot；超过小对象档位的请求走整页，在 **`&root_vspace`** 上做 insert / bind / `map()`——**绝不**走 `mm_user_utils_*`（utils 显式拒绝 root）。

### 1.1 为什么不能再变回一把大锁

无锁 IPC 的常态是：对象在 A 核分配，消息到 B 核的 server，再在 B 上释放。若堆仍是全局一把锁，业务锁和 IPC 锁迁走之后，瓶颈只是换了个马甲回来。

所以做成每核本地堆：同核 alloc / free 不抢别人。异核释放时，**不要直接改他核的 chunk 链表**，而是把对象（或整页回收请求）丢进**归属核**的无锁队列，让归属核自己 drain。承认「所有权在分配核」，用和 IPC 同一套 MSQ 哲学把东西还回去。

### 1.2 两条跨核队列

| 队列 | 何时用 | 怎么找到主人 |
|------|--------|----------------|
| `buffer_msq` | 小对象（指针**不是** 4 KiB 对齐） | `object_header.allocator_id` |
| `kfree_page_msq` | 整页（指针 **4 KiB 对齐**） | `root_vspace` 上该 VA 的 **radix owner 标签**（插入时写入的分配 CPU），不是 object header |

drain 顺序：先清 `kfree_page_msq`，再清 `buffer_msq`。触发点有两处：**每次 `m_alloc` / `m_free` 入口**都会先 `mem_allocator_remote_frees`；另外 `schedule()` 里会调 `kalloc_process_cross_cpu_frees`，好让暂时不怎么分配的核也能把积压清掉。

---

## 2. 目标与边界

提供：per-CPU allocator vtable、chunk 小对象、整页 RB 跟踪、双 MSQ 跨核 free；成功分配路径上对载荷清零。

不做：用户态 malloc；跨进程共享堆；Linux SLUB 那套 sysfs。

权衡很清楚：去掉全局堆锁，就必须 drain、必须把 owner 标对；整页和小对象用「是否 4 KiB 对齐」区分，误判会走错 free 路径。

---

## 3. 分层与调用方

用法是 `percpu(kallocator)->m_alloc / m_free`（没有单独导出的全局 `kalloc()` 符号）。`kinit(cpu_id)` 在 `virt_mm_init` 里调用。线程、IPC、port、page_slice 都吃这个堆。

---

## 4. 数据结构与不变量

### 4.1 尺寸

- 合法请求：`1 ≤ Bytes ≤ MIDDLE_PAGE_SIZE`（2 MiB）。
- 小对象档：`slot_size[]` 约为 `8…2048` 共 12 档（`MAX_GROUP_SLOTS`）；**大于 2048** 走整页路径。

### 4.2 chunk / group

- `PAGE_PER_CHUNK == 4`：chunk 起始页对齐，但 **object 载荷刻意不按 4 KiB 对齐**，好让 free 时用对齐性区分「页还是小对象」。
- 每个 group 两套链表：`full_list` 与 `empty_list`（有空位的 chunk 也落在 empty 这一侧的计数/复用体系里，不是三套 partial/full/empty 命名）。
- chunk 元数据**只许 owner CPU 改**；跟踪整页的 `page_chunk_root` 红黑树用 `cas_lock`。

### 4.3 整页与 `root_vspace`

路径大致是：`pmm_alloc` → 在 root 上 radix `insert_range`（owner tag = 本 CPU）→ `map` → `leaf_bind` → 可选 zero。跟踪节点 `page_chunk_node` 本身也来自小对象分配。释放：unbind → unmap → DELETE 清叶 → `pmm_free`。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `kmalloc.h` / `kmalloc.c` | `mem_allocator`、双 MSQ、chunk / 页路径、`kinit` |
| `allocator.h` | vtable（`m_alloc` / `m_free` / `init`） |
| `string.c` | `memcpy` / `memset` 等工具，不是堆策略 |

---

## 6. 流程

### 6.1 `m_alloc`

1. `mem_allocator_remote_frees(this)`（先清积压）。
2. 若 `Bytes > 2048`：先分配一个 `page_chunk_node` 小对象，再 `core_get_free_pages` 拿到整页，memset，插入 RB。
3. 否则：从对应 group 的 `empty_list` 取；不够时可偷别 group 的全空 chunk；再不行就 `core_get_free_pages(PAGE_PER_CHUNK)` 建新 chunk，然后 `chunk_get_obj`。成功路径上对载荷清零。

### 6.2 `m_free`

1. 同样先 remote_frees。
2. 指针 **页对齐**：查 radix 上的 owner CPU；异核则往对方的 `kfree_page_msq` 投递；本核走 `kfree_page_local`。
3. 指针 **非对齐**：读 `object_header.allocator_id`；本核 `group_free_obj`（空 chunk 多了可以还给系统）；异核走 `buffer_msq`。

### 6.3 与 IPC / 调度

消息体、TCB 一类对象经常跨核释放，依赖本篇的 MSQ。idle 路径上只要还会 `schedule`，低分配核也能 drain，避免队列一直涨。

---

## 7. 公开 API

```c
percpu(kallocator)->m_alloc(percpu(kallocator), n);
percpu(kallocator)->m_free(percpu(kallocator), p);

struct allocator *kinit(int allocator_id); /* virt_mm_init 里按 cpu 调用 */
void kalloc_process_cross_cpu_frees(void); /* schedule 调用 */
```

`DEFINE_PER_CPU(struct allocator *, kallocator)` 在 `kmalloc.c`。引导期会先用静态的 `tmp_k_alloctor` 自举，再换成正式的 percpu 实例并建好两条 MSQ。

---

## 8. 多架构

逻辑与 ISA 无关；依赖 arch 原子操作，以及 `root_vspace` 上已经能 `map` 的内核半。

---

## 9. 测试

kmalloc / SMP 相关测例。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```

本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- owner 标错或对齐误判 → 进错队列或泄漏。
- 引导期自举与正式实例切换要留心：早期分配若落到 tmp，释放路径要对得上。
- 与 radix DELETE / owner 过滤的头文件叙述差异，以 `.c` 为准（见 Radix 篇）。

---

## 11. 变更记录

- 2026-09-25：语言整理；钉死双 MSQ、drain 触发点、slot 档与 utils 禁 root。
- 2026-08-29：整篇重做——双 MSQ、页走 radix owner、drain 在 alloc/free/schedule；堆不再变大锁。
- 2026-08-26：v0.1 初稿。
