# kmalloc 与内核堆

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/kmalloc.c`、`kernel/mm/string.c`、`include/rendezvos/mm/kmalloc.h`、`include/rendezvos/mm/allocator.h`。

整章内存栈总图见 `06-物理内存与Buddy分配器.md` 开篇（底层即本篇 `kallocator`，横向箭头即跨核归还）。物理页分配见 `06`；在 `root_vspace` 上挂整页时用的 radix / `map` 见 Radix 篇与页表篇；跨核 free 用的 MSQ 机制见 `04-IPC/18`。

---

## 1. 概述

内核堆是 **每 CPU 一个 `kallocator`（`struct mem_allocator`）**：小对象走 chunk / slot；超过小对象档位的请求走整页，在 **`&root_vspace`** 上做 insert / bind / `map()`——**绝不**走 `mm_user_utils_*`（utils 显式拒绝 root）。

### 1.1 为什么不能再变回一把大锁

无锁 IPC 的常态是：对象在 A 核分配，消息到 B 核的 server，再在 B 上释放。若堆仍是全局一把锁，业务锁和 IPC 锁迁走之后，瓶颈只是换个形式重新出现。

所以做成每核本地堆：同核 alloc / free 不与他人竞争。异核释放时，**不要直接改其他核的 chunk 链表**，而是把对象（或整页回收请求）投递到**归属核**的无锁队列，让归属核自己 drain。承认「所有权在分配核」，用和 IPC 同一套 MSQ 哲学把对象归还回去。

### 1.2 两条跨核队列

| 队列 | 何时用 | 如何确定归属核 |
|------|--------|----------------|
| `buffer_msq` | 小对象（指针**不是** 4 KiB 对齐） | `object_header.allocator_id` |
| `kfree_page_msq` | 整页（指针 **4 KiB 对齐**） | `root_vspace` 上该 VA 的 **radix owner 标签**（插入时写入的分配 CPU），不是 object header |

drain 顺序：先清 `kfree_page_msq`，再清 `buffer_msq`。触发点有两处：**每次 `m_alloc` / `m_free` 入口**都会先 `mem_allocator_remote_frees`；另外 `schedule()` 里会调 `kalloc_process_cross_cpu_frees`，让分配较少的核也能清理积压。

---

## 2. 目标与边界

提供：per-CPU allocator vtable、chunk 小对象、整页 RB 跟踪、双 MSQ 跨核 free；成功分配路径上对载荷清零。

不做：用户态 malloc；跨进程共享堆；Linux SLUB 那套 sysfs。

权衡很清楚：去掉全局堆锁，就必须 drain、必须把 owner 标正确；整页和小对象用「是否 4 KiB 对齐」区分，误判会走错 free 路径。

---

## 3. 分层与调用方

用法是 `percpu(kallocator)->m_alloc / m_free`（没有单独导出的全局 `kalloc()` 符号）。`kinit(cpu_id)` 在 `virt_mm_init` 里调用。线程、IPC、port、page_slice 都从这份堆分配。

---

## 4. 数据结构与不变量

### 4.1 尺寸

- 合法请求：`1 ≤ Bytes ≤ MIDDLE_PAGE_SIZE`（2 MiB）。
- 小对象档：`slot_size[]` 约为 `8…2048` 共 12 档（`MAX_GROUP_SLOTS`）；**大于 2048** 走整页路径。

```c
#define MAX_GROUP_SLOTS 12
#define PAGE_PER_CHUNK  4
```

### 4.2 chunk / group

- `PAGE_PER_CHUNK == 4`：chunk 起始页对齐，但 **object 载荷刻意不按 4 KiB 对齐**，以便 free 时用对齐性区分「页还是小对象」。
- 每个 group 两套链表：`full_list` 与 `empty_list`（有空位的 chunk 也归入 empty 侧的计数/复用体系，不是三套 partial/full/empty 命名）。
- chunk 元数据**只允许 owner CPU 修改**；跟踪整页的 `page_chunk_root` 红黑树用 `cas_lock`。

```c
struct object_header {
        struct list_entry obj_list;
        i64 allocator_id;
        ms_queue_node_t msq_node;
        char obj[];
};

struct mem_chunk {
        u64 magic; /* 0xa11ca11ca11ca11c */
        int allocator_id;
        int chunk_order;
        int nr_max_objs;
        int nr_used_objs;
        struct list_entry chunk_list;
        struct list_entry full_obj_list;
        struct list_entry empty_obj_list;
        char padding[];
};

struct mem_group {
        int allocator_id;
        int chunk_order;
        size_t free_chunk_num;
        size_t empty_chunk_num;
        size_t full_chunk_num;
        struct list_entry full_list;
        struct list_entry empty_list;
};
```

### 4.3 整页与 `root_vspace`

路径大致是：`pmm_alloc` → 在 root 上 radix `insert_range`（owner tag = 本 CPU）→ `map` → `leaf_bind` → 可选 zero。跟踪节点 `page_chunk_node` 本身也来自小对象分配。释放：unbind → unmap → DELETE 清叶 → `pmm_free`。

```c
#define MM_COMMON                                                       \
        struct allocator *(*init)(int allocator_id);                    \
        void *(*m_alloc)(struct allocator * allocator_p, size_t Bytes); \
        void (*m_free)(struct allocator * allocator_p, void *p);        \
        i64 allocator_id

struct allocator {
        MM_COMMON;
};

struct page_chunk_node {
        struct rb_node _rb_node;
        vaddr page_addr;
        i64 page_num;
};

struct mem_allocator {
        MM_COMMON;
        struct mem_group groups[MAX_GROUP_SLOTS];
        struct rb_root page_chunk_root;
        ms_queue_t *buffer_msq;
        atomic64_t buffer_size;
        ms_queue_t *kfree_page_msq;
        atomic64_t kfree_page_pending;
        cas_lock_t lock;
};
```

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
2. 若 `Bytes > 2048`：先分配一个 `page_chunk_node` 小对象，再 `core_get_free_pages` 获取整页，memset，插入 RB。
3. 否则：从对应 group 的 `empty_list` 取；不够时可借用别 group 的全空 chunk；仍不够则 `core_get_free_pages(PAGE_PER_CHUNK)` 建新 chunk，然后 `chunk_get_obj`。成功路径上对载荷清零。

### 6.2 `m_free`

1. 同样先 remote_frees。
2. 指针 **页对齐**：查 radix 上的 owner CPU；异核则向归属核的 `kfree_page_msq` 投递；本核走 `kfree_page_local`。
3. 指针 **非对齐**：读 `object_header.allocator_id`；本核 `group_free_obj`（空 chunk 过多时可归还系统）；异核走 `buffer_msq`。

### 6.3 与 IPC / 调度

消息体、TCB 一类对象经常跨核释放，依赖本篇的 MSQ。idle 路径上只要还会 `schedule`，分配较少的核也能 drain，避免队列持续增长。

---

## 7. 公开 API

本篇涉及的接口分布在：`include/rendezvos/mm/allocator.h`、`include/rendezvos/mm/kmalloc.h`（实现于 `kernel/mm/kmalloc.c`）。接口说明以头文件注释为准，并已与 `kalloc`/`kfree`/`kinit` 核对。`memcpy`/`memset` 等声明在 `include/common/string.h`（实现可落在 `kernel/mm/string.c`），**不是**堆策略 API，本 §7 不展开。

没有导出名为 `kalloc` / `kfree` 的全局符号；调用方一律走 vtable。

### 7.1 编排顺序（与源码一致）

| 阶段 | 顺序 |
|------|------|
| 每核启动 | `virt_mm_init` → **`kinit(cpu_id)`** → `per_cpu(kallocator, cpu_id)` 可用 |
| 运行期分配 | **`percpu(kallocator)->m_alloc(a, n)`**：入口先 **`mem_allocator_remote_frees`** → 小对象或整页路径 → 成功则载荷已清零 |
| 运行期释放 | **`percpu(kallocator)->m_free(a, p)`**：先 remote_frees → 按是否 4 KiB 对齐分流（见下） |
| 调度旁路 drain | **`kalloc_process_cross_cpu_frees()`**（`schedule` 调用）≡ 对本核再执行一次 remote_frees |

跨核 free：小对象 → 归属核 `buffer_msq`；整页 → 归属核 `kfree_page_msq`（owner 来自 `root_vspace` radix tag）。归属核 drain：**先** `kfree_page_msq`，**再** `buffer_msq`。

### 7.2 Allocator vtable（`allocator.h` / `mem_allocator`）

```c
struct allocator {
        struct allocator *(*init)(int allocator_id);
        void *(*m_alloc)(struct allocator *a, size_t Bytes); /* 成功须清零 */
        void  (*m_free)(struct allocator *a, void *p);
        i64 allocator_id;
};
/* 生产路径：DEFINE_PER_CPU(struct allocator *, kallocator) */
```

| 接口 | 说明 |
|------|------|
| `m_alloc(a, Bytes)` | 合法：`1 ≤ Bytes ≤ MIDDLE_PAGE_SIZE`（2 MiB）；否则 NULL。`Bytes > 2048`（`slot_size[11]`）走整页：先小对象分配 `page_chunk_node`，再 `core_get_free_pages` 映入 `&root_vspace`，memset，插入 RB。否则走 chunk/`_k_alloc`。失败 NULL。 |
| `m_free(a, p)` | `p` 页对齐 → 查 radix owner；异核投 `kfree_page_msq`，本核 `kfree_page_local`。非对齐 → 读 `object_header.allocator_id`；本核归还 chunk，异核投 `buffer_msq`。`a`/`p` 为空则记录日志返回。 |
| `allocator_id` | 与 CPU id 对齐；写入小对象 header / chunk。 |

典型用法：`percpu(kallocator)->m_alloc(percpu(kallocator), n)`。

### 7.3 初始化与 drain（`kmalloc.h`）

```c
struct allocator *kinit(int allocator_id);
void kalloc_process_cross_cpu_frees(void);
```

| 接口 | 说明 |
|------|------|
| `kinit` | `allocator_id < 0` → NULL。用静态 `tmp_k_alloctor` 自举真正的 `mem_allocator` 与两条 MSQ，装入 `per_cpu(kallocator, id)`。同 id 已存在 → NULL。 |
| `kalloc_process_cross_cpu_frees` | 对本核 `kallocator` 调 `mem_allocator_remote_frees`；`kallocator` 为空则 no-op。 |

尺寸常量（实现）：`slot_size[] = {8…2048}` 共 `MAX_GROUP_SLOTS`(12)；`PAGE_PER_CHUNK == 4`。整页路径 **禁止** `mm_user_utils_*`（只操作 `root_vspace`）。

### 7.4 本篇覆盖但不列入堆约定的符号

| 符号 | 说明 |
|------|------|
| `memcpy` / `memset` / `strlen` 等 | `common/string.h`；与 alloc 策略无关 |
| `tmp_k_alloctor` | 仅 `kinit` 自举；不要作为正式堆长期使用 |

---

## 8. 多架构

逻辑与 ISA 无关；依赖 arch 原子操作，以及 `root_vspace` 上已经能 `map` 的内核半。

---

## 9. 测试

kmalloc / SMP 相关测试用例。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```


---

## 10. 限制与后续

- owner 标错或对齐误判 → 进错队列或泄漏。
- 引导期自举与正式实例切换需要注意：早期分配若落在 tmp，释放路径要能对应上。
- 与 radix DELETE / owner 过滤的头文件叙述差异，以 `.c` 为准（见 Radix 篇）。

---

## 11. 变更记录

- 2026-10-04：全文中文表述整理——「换了个马甲回来」→「换个形式重新出现」；「不抢别人」→「不与他人竞争」；「他核的」→「其他核的」；「丢进」→「投递到」；「把东西还回去」→「把对象归还」；「怎么找到主人」→「如何确定归属核」；「好让…把积压清掉」→「让…清理积压」；「低分配核」→「分配较少的核」；「一直涨」→「持续增长」；「标对」→「标正确」；「好让」→「以便」；「落在 empty 这一侧」→「归入 empty 侧」；「只许…改」→「只允许…修改」；「拿到」→「获取」；「偷」→「借用」；「再不行就」→「仍不够则」；「往对方的」→「向归属核的」；「空 chunk 多了可以还给系统」→「空 chunk 过多时可归还系统」；「本核回 chunk」→「本核归还 chunk」；「打日志」→「记录日志」；「再跑一轮」→「再执行一次」；「挂 RB」→「插入 RB」；「不要当正式堆长期使用」→「不要作为正式堆长期使用」；「要留心/落到/要对得上」→「需要注意/落在/要能对应上」；§7「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-04：§4 补 `mem_chunk` / `mem_group` / `mem_allocator` / `page_chunk_node` 结构体。
- 2026-09-27：中文用语整理（契约→约定；§7「以头文件注释为准」；「吃这个堆」→「从这份堆分配」）。
- 2026-09-26：§7 全文审阅——补 `kinit` / drain / `m_alloc`·`m_free` 头文件注释与接口说明；写清尺寸门禁、双 MSQ 与编排顺序。
- 2026-09-25：语言整理；钉死双 MSQ、drain 触发点、slot 档与 utils 禁 root。
- 2026-08-29：整篇重做——双 MSQ、页走 radix owner、drain 在 alloc/free/schedule；堆不再变大锁。
- 2026-08-26：v0.1 初稿。
