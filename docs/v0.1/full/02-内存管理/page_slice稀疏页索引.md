# page_slice 稀疏页索引

v0.1 · 2026-08-29

本篇覆盖：`kernel/mm/page_slice.c`、`kernel/mm/page_slice_copy.c`、`include/rendezvos/mm/page_slice.h`、`include/rendezvos/mm/page_slice_copy.h`。

内核堆见 `kmalloc与内核堆.md`；用户 VA 真源见 `Radix树与用户映射.md`（**page_slice 只服务内核侧逻辑缓冲，不替代用户 radix**）。

---

## 1. 概述

Buddy 单次连续物理分配大约顶在 **2 MiB**。内核还想按「文件偏移 / 大数组下标」那种**逻辑连续**方式摸一块可以远大于 2 MiB 的缓冲——但物理上、甚至 KVA 上都不要求连成一片。

**page_slice** 干的就是这件事：在一个 slice 里用稀疏多级索引，把 **pgoff（页号偏移）** 绑到调用方已经准备好的 **离散内核 KVA**。连续性只在「偏移算术」上成立。

分工：

| 你要 | 用 |
|------|-----|
| 用户 VA、mmap、COW | VSpace + `mm_user_utils_*` |
| 小对象 / 整页内核堆 | `percpu(kallocator)` |
| **逻辑连续、物理按页稀疏** 的内核缓冲（页缓存一类） | **page_slice** |

**谁分配什么：** 内容页由**调用方** `m_alloc` 再 `insert`；slice 只养自己的 **index/leaf 壳页**。不要指望 `insert` 顺手给你内容页。

---

## 2. 目标与边界

**提供：** create/destroy、按 pgoff insert/lookup/remove、逻辑 `size` 伸缩、copy/clone 辅助。

**不做：** 文件系统页缓存策略；用户 `mmap`；硬件 MMU pin（`PIN` 只表示 destroy/remove 时**不要** `m_free` 内容 kva）。

**不是 COW：** `page_slice_clone` 是对新页 **memcpy 深拷贝**，只物化 VALID pgoff；没有共享再 fault。

---

## 3. 分层与调用方

上层（例如兼容层页缓存）持 `struct page_slice*`：`insert` 绑定已有 kva → `lookup` / `copy_to_*` 读 → 需要副本时 `clone`。与用户缓冲交互另走 `map_handler_user_kernel_copy` 等，不进 `thread->vs` radix。

每 slice 一把 `cas_lock`；API 内部加锁。**lookup 返回前会放锁**——并发 remove 可能让指针失效，调用方勿假设指针长期有效。copy 路径同样是「lookup 后 memcpy」，不跨拷贝持锁。

---

## 4. 数据结构与不变量

### 4.1 位布局（逻辑）

```text
[ L2:9 | L1:9 | L0:9 | leaf_idx:7 | page_off:12 ]
```

叶页约 128 槽；索引页 512 个 tagged_ptr。高度上限使逻辑跨度可达约 64 TiB（以头文件常量为准）。

### 4.2 空槽与高度

空索引槽必须是 `tp_new_none()`，不要塞「空 entry 伪对象」。root height：`0` 空、`1` 直接叶、`2+` 索引。`live` 计数含义随 height 变（root height 2 数索引槽，非 root height 2 数叶绑定）——以 `ps_entry_live_max_table` 为准。

`mapped_entries`：上层统计用，grow/shrink **不**靠它回收。

FAM `append_page_slice_info[]` 给上层元数据；**clone 不拷 append**（`create(0, size)`）。

### 4.3 PIN

`PAGE_SLICE_FLAG_PIN`：默认 remove/destroy **跳过**对内容 kva 的 `m_free`。不是页表 pin。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `page_slice.h` / `.c` | 稀疏 radix、锁、insert/lookup/remove、size |
| `page_slice_copy.h` / `.c` | buffer/slice/user 拷贝与 clone |

测例：`modules/test/single_page_slice_test.c`。

---

## 6. 流程

### 6.1 绑定一页

调用方准备 kva → `page_slice_insert_page`（缺壳则分配 index/leaf）→ 槽标 VALID。

### 6.2 copy / clone

| API | 行为 |
|-----|------|
| `copy_to_buffer` | 区间须**全部已映射**，否则 `-E_RENDEZVOS`（**不**跳洞、不零填） |
| `copy_to_slice` | 目标 pgoff 须**已绑定**；拒绝危险自重叠 |
| `copy_to_user` | 同样要求已映射 + `map_handler_user_kernel_copy` |
| `clone` | 新 slice、同 `size`；只深拷 VALID；剥 PIN；洞不物化 |

### 6.3 shrink

`set_size` 变小：拆高 pgoff，再按规则 unwrap 空壳（零路径细节见实现）。

---

## 7. 公开 API

```c
struct page_slice *page_slice_create(usize append_info_size, u64 slice_size);
void page_slice_destroy(struct page_slice *);
u64 page_slice_get_size(...);
error_t page_slice_set_size(...);
error_t page_slice_insert_page(...);
error_t page_slice_lookup(...);   /* 返回前已解锁 */
error_t page_slice_remove_page(...);

error_t page_slice_copy_to_buffer(...);
error_t page_slice_copy_to_slice(...);
error_t page_slice_copy_to_user(...);
error_t page_slice_clone(...);
```

签名以头文件为准。

---

## 8. 多架构

与 ISA 无关；依赖本核 `kallocator` 与内核可访问的内容 kva。

---

## 9. 测试

`page_slice_test`；`make ARCH=x86_64 config && make all && make run`。本篇未复测。

---

## 10. 限制与后续

- lookup 指针生命周期短。  
- copy **遇洞即失败**。  
- 无内置 COW / 页缓存策略。  
- 跨 CPU 共享同一 slice 只有粗锁，上层宜串行化。

---

## 11. 变更记录

- 2026-08-29：整篇重做——纠正「insert 分配内容页」；copy 遇洞失败；clone=深拷非 COW；补全 API；分工表。
- 2026-08-26：v0.1 初稿。
