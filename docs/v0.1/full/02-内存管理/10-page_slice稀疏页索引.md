# page_slice 稀疏页索引

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/page_slice.c`、`kernel/mm/page_slice_copy.c`、`include/rendezvos/mm/page_slice.h`、`include/rendezvos/mm/page_slice_copy.h`。

内核堆见 `09-kmalloc与内核堆.md`；用户 VA 真源见 `08-Radix树与用户映射.md`。**page_slice 只服务内核侧逻辑缓冲，不替代用户 radix**——两者不要混用。

---

## 1. 概述

Buddy 单次连续物理分配大约顶在 **2 MiB**。内核有时还想按「文件偏移 / 大数组下标」那种**逻辑连续**的方式摸一块远大于 2 MiB 的缓冲，但物理上、甚至 KVA 上并不要求连成一片。

**page_slice** 干的就是这件事：在一个 slice 里用稀疏多级索引，把 **pgoff（页号偏移）** 绑到调用方已经准备好的 **离散内核 KVA**。连续性只在偏移算术上成立。

分工可以记成三句话：用户 VA、mmap、COW 走 VSpace + `mm_user_utils_*`；小对象 / 整页内核堆走 `percpu(kallocator)`；**逻辑连续、物理按页稀疏** 的内核缓冲（例如页缓存一类）才走 page_slice。

更关键的一条：**内容页由调用方自己 `m_alloc` 再 `insert`**；slice 只养自己的 index / leaf **壳页**。不要指望 `insert` 顺手给你内容页。

---

## 2. 目标与边界

提供：create / destroy、按 pgoff insert / lookup / remove、逻辑 `size` 伸缩、copy / clone 辅助。

不做：文件系统页缓存策略；用户 `mmap`；硬件 MMU pin——`PAGE_SLICE_FLAG_PIN` 只表示 destroy / remove 时**不要**对内容 kva 调 `m_free`，不是页表 pin。

也**不是 COW**：`page_slice_clone` 是对新页做 **memcpy 深拷贝**，只物化已 VALID 的 pgoff；没有「共享再 fault」。

---

## 3. 分层与调用方

上层（例如兼容层的页缓存）持 `struct page_slice *`：`insert` 绑定已有 kva → `lookup` / `copy_to_*` 读 → 需要副本时 `clone`。跟用户缓冲打交道另走 `map_handler_user_kernel_copy` 一类路径，**不**进 `thread->vs` 的 radix。

每 slice 一把 `cas_lock`，API 内部加锁。**lookup 返回前会放锁**——并发 remove 可能让指针立刻失效，调用方不要假设指针长期有效。copy 路径同样是「lookup 之后再 memcpy」，不会跨整段拷贝一直持锁。

---

## 4. 数据结构与不变量

### 4.1 位布局（逻辑）

```text
[ L2:9 | L1:9 | L0:9 | leaf_idx:7 | page_off:12 ]
```

叶页大约 128 槽（`PAGE_SLICE_LEAF_CAPACITY`）；索引页 512 个 tagged_ptr（`PAGE_SLICE_INDEX_CAPACITY`）。高度上限使逻辑跨度大约到 64 TiB 量级（以头文件 `PAGE_SLICE_MAX_*` 为准）。

### 4.2 空槽与高度

空索引槽必须是 `tp_new_none()`，不要塞「空 entry 伪对象」。root height：`0` 表示空，`1` 直接是叶，`2+` 是索引。`live` 计数的含义随 height 变化（例如 root height 2 数的是索引槽，非 root height 2 数的是叶绑定）——以 `ps_entry_live_max_table` 一类辅助为准。

`mapped_entries` 给上层做统计用；grow / shrink **不**靠它回收。FAM `append_page_slice_info[]` 给上层挂元数据；**clone 不拷 append**（目标用 `create(0, size)`）。

### 4.3 PIN

`PAGE_SLICE_FLAG_PIN`：默认 remove / destroy **跳过**对内容 kva 的 `m_free`。再次强调：这不是页表 pin。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `page_slice.h` / `.c` | 稀疏索引、锁、insert / lookup / remove、`set_size` |
| `page_slice_copy.h` / `.c` | buffer / slice / user 拷贝与 `clone` |

测例：`modules/test/single_page_slice_test.c`（以树内实际文件名为准）。

---

## 6. 流程

### 6.1 绑定一页

调用方准备好内核 kva → `page_slice_insert_page`（缺壳则分配 index / leaf）→ 槽上标 VALID。

### 6.2 copy / clone

- `page_slice_copy_to_buffer`：区间必须**全部已映射**，否则失败（**不**跳洞、不零填）。
- `page_slice_copy_to_slice`：目标 pgoff 须**已绑定**；拒绝危险自重叠。
- `page_slice_copy_to_user`：同样要求已映射，再经 `map_handler_user_kernel_copy`。
- `page_slice_clone`：新建同 `size` 的 slice；只深拷 VALID；剥掉 PIN；洞不物化。

### 6.3 shrink

`page_slice_set_size` 变小时：拆掉高 pgoff，再按规则 unwrap 空壳（细节见实现）。

---

## 7. 公开 API

```c
struct page_slice *page_slice_create(usize append_info_size, u64 slice_size);
error_t page_slice_destroy(struct page_slice **slice);
error_t page_slice_set_size(struct page_slice **slice, u64 new_size);
error_t page_slice_insert_page(struct page_slice *slice, u64 pgoff, ...);
error_t page_slice_lookup(...);    /* 返回前已解锁 */
error_t page_slice_remove_page(struct page_slice *slice, u64 pgoff);

error_t page_slice_copy_to_buffer(...);
error_t page_slice_copy_to_slice(...);
error_t page_slice_copy_to_user(struct VSpace *vs, u64 user_va, ...);
error_t page_slice_clone(struct page_slice **dst_out, struct page_slice *src);
```

完整参数以头文件为准。

---

## 8. 多架构

与 ISA 无关；依赖本核 `kallocator`，以及内容 kva 在内核侧可访问。

---

## 9. 测试

page_slice 相关测例。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```

本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- lookup 返回的指针生命周期短。
- copy **遇洞即失败**。
- 无内置 COW / 页缓存策略。
- 跨 CPU 共享同一 slice 只有粗锁，上层宜自己串行化。

---

## 11. 变更记录

- 2026-09-25：语言整理；纠正 `destroy` 签名为 `page_slice **`。
- 2026-08-29：整篇重做——纠正「insert 分配内容页」；copy 遇洞失败；clone=深拷非 COW。
- 2026-08-26：v0.1 初稿。
