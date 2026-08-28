# kmalloc 与内核堆

v0.1 · 2026-08-26

本篇覆盖：`kernel/mm/kmalloc.c`、`kernel/mm/string.c`、`include/rendezvos/mm/kmalloc.h`、`include/rendezvos/mm/allocator.h`。

物理页来源见 `物理内存与Buddy分配器.md`；`root_vspace` 上 radix 操作见 `Radix树与用户映射.md` 与 `虚拟地址空间与页表.md`。

---

## 1. 概述

内核堆由 **per-CPU `kallocator`**（`struct mem_allocator`）实现：小对象走 **slab 式 chunk**（多 slot 尺寸、每 chunk 多页），大分配走 **整页路径**，在 **`&root_vspace`** 的 radix 上 insert/bind 并 `map()`，不经 `mm_user_utils_*`。

`string.c` 提供内核侧 `memcpy`/`memset` 等，供 kmalloc 与 map 窗口 zero 使用。

---

## 2. 目标与边界

core 提供：`kalloc`/`kfree` 族（通过 `struct allocator` vtable）、跨 CPU 释放队列（MSQ + `kalloc_process_cross_cpu_frees`）、`root_vspace` 内核堆 radix 路径。

core 不做：用户态 malloc；general-purpose 跨进程共享堆；SLUB  sysfs 统计。

---

## 3. 分层与调用方

- **线程/IPC/PMM** — 通过 `percpu(kallocator)->m_alloc/m_free` 分配 `Thread_Base`、消息、port 等。
- **page_slice** — 索引页/叶页通过 kallocator 获取 backing 页。
- **schedule** — 每次上下文切换调用 `kalloc_process_cross_cpu_frees()` 处理它核投递的 free。

调用方 **must** 在正确 CPU 上 free 归属该 CPU allocator 的对象，或使用已路由的 `kfree` 路径；跨核 raw free 依赖 MSQ  drain。

---

## 4. 数据结构与不变量

### 4.1 allocator 接口

`include/rendezvos/mm/allocator.h` 定义 `struct allocator` 函数指针：`m_alloc`、`m_free` 等。`mem_allocator` 为 kmalloc 具体实现。

### 4.2 chunk 与 slot

- `MAX_GROUP_SLOTS`（12）种对象尺寸；每 group 有 partial/full/empty 链表。
- `mem_chunk` 带 magic `CHUNK_MAGIC`；chunk 内多页（`PAGE_PER_CHUNK` 等）减少内部碎片。
- 大于约 2048 字节的请求走 **整页 RB 树** `page_chunk_root` 跟踪（`page_chunk_node`）。

### 4.3 root_vspace 关系

整页分配：`vmm_radix_tree_insert_range` + `map()` + `leaf_bind` 在 **`&root_vspace`** 上执行；L0 锁由 kmalloc 内部按范围持有。与 user AS 的 radix 树分离，但共享内核高半 PTE。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `kmalloc.h` | chunk、object_header、mem_allocator 布局 |
| `kmalloc.c` | per-CPU 初始化、alloc/free、整页 radix、跨核 free 队列 |
| `string.c` | 内核字符串/内存例程 |
| `allocator.h` | 通用 allocator vtable |

---

## 6. 流程

### 6.1 小对象分配

1. 按 size 选 slot group。
2. 从 partial 链表取 `object_header`；无则向 empty 或新 chunk 要页。
3. chunk 缺页时可能从其他 group 偷页或 `pmm_alloc` + root_vspace 映射。

### 6.2 整页分配（core_free_pages 反向）

1. buddy 分配连续物理页（可能多于请求，多余立即归还）。
2. 在 root_vspace radix 上 reserve + map + bind。
3. 在 RB 树登记 `[page_addr, page_num)` 供 free 查找。

### 6.3 跨 CPU 释放

远程 CPU `m_free` 可能把 chunk/object 入 MSQ；owner CPU 在 **`schedule()`** 或 kalloc 入口 drain，避免 idle 核永不回收。

---

## 7. 公开 API

```c
extern DEFINE_PER_CPU(struct allocator*, kallocator);

/* 典型用法 */
void* p = percpu(kallocator)->m_alloc(percpu(kallocator), size);
percpu(kallocator)->m_free(percpu(kallocator), p);

void kalloc_process_cross_cpu_frees(void);
```

具体 `kalloc`/`kfree` 符号与 size class 表见 `kmalloc.h` / `kmalloc.c` 实现；兼容层多通过 `allocator` vtable 间接调用。

---

## 8. 多架构

kmalloc 主体架构无关；依赖 `Map_Handler`、`root_vspace` 与 buddy，均已 per-arch 初始化。`string.c` 可能含 arch 优化拷贝（若有）。

---

## 9. 测试

`single_kmalloc_test.c`、`smp_kmalloc_test.c`。本篇与当前源码一致，尚未单独复测。

---

## 10. 限制与后续

- 单次整页请求上限 2 MiB（buddy 限制）；更大需拆分。
- 跨 allocator 误 free 未定义（依赖 magic/allocator_id 检测有限）。
- 与 user radix 锁顺序：见 INVARIANTS / AI_CHECKLIST（zone lock vs vspace）。

---

## 11. 变更记录

- 2026-08-26：v0.1 初稿，per-CPU kallocator、chunk/整页路径与 root_vspace。
