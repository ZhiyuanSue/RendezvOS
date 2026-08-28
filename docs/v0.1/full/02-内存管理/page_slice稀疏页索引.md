# page_slice 稀疏页索引

v0.1 · 2026-08-26

本篇覆盖：`kernel/mm/page_slice.c`、`kernel/mm/page_slice_copy.c`、`include/rendezvos/mm/page_slice.h`、`include/rendezvos/mm/page_slice_copy.h`。

内核堆页来源见 `kmalloc与内核堆.md`；VSpace 用户映射边界见 `Radix树与用户映射.md`（page_slice **仅内核用**，不替代用户 radix）。

---

## 1. 概述

**page_slice** 解决 buddy **最大 2 MiB 连续物理分配**与内核需要 **大于 2 MiB 的逻辑连续虚拟缓冲**之间的矛盾：在固定大小的内核虚拟窗口内，用 **稀疏多级索引**（leaf + index 页，tagged_ptr 高度）把 **pgoff（页文件偏移）** 映射到 **离散内核 KVA**。

典型用途：大块内核缓冲区、页缓存式稀疏映射；**不**用于用户进程 VA（用户用 VSpace radix + `mm_user_utils_*`）。

---

## 2. 目标与边界

core 提供：slice 创建/销毁、按 pgoff insert/lookup/remove、grow/shrink 索引树、**copy/clone** 辅助（`page_slice_copy.c`）。

core 不做：文件系统 page cache 策略；用户态 `mmap`；MMU pin（`PIN` flag 仅为 slice 层语义，非硬件 PIN）。

---

## 3. 分层与调用方

兼容层（如 future 页缓存或大块 kernel API）持有 `struct page_slice*`：

- 绑定 pgoff → 先 `page_slice` insert（分配 backing 页，通常 kallocator + 内核 direct map 或等价）。
- 读连续字节 → `page_slice_copy_to_buffer` 或逐 pgoff lookup。
- fork 式复制稀疏内容 → `page_slice_clone`（只复制 VALID pgoff）。

**与 VSpace 边界**：slice 使用 **内核虚拟地址** 与 pgoff 键，不进入 `thread->vs` 的用户 radix；若需与用户缓冲交互，由兼容层通过 `map_handler_user_kernel_copy` 等单独路径。

---

## 4. 数据结构与不变量

### 4.1 地址布局（逻辑）

索引位划分（见 `page_slice.h` 注释）：最多 3 层 index + leaf，pgoff 拆为 L2/L1/L0/leaf_idx + page_off（12 bit 页内）。

### 4.2 条目类型

- **leaf entry** — `page_slice_entry`：KVA、`VALID`/`PIN` flags、可选 `page_list_node`。
- **index entry** — `tagged_ptr`：`ptr` + `live` 计数 + `height`（0=空 root，1=直接 leaf root，2+=索引页）。

`slice->mapped_entries` — 兼容层统计 VALID pgoff 数，不参与 radix 内部 reclaim。

### 4.3 锁

slice 带 `cas_lock`；grow/shrink 与 insert/remove 须遵守头文件中的 live 计数规则，避免释放仍被引用的 index 页。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `page_slice.h` | 布局常量、entry、API 声明 |
| `page_slice.c` | 索引树 grow/shrink、insert/lookup/remove（radix-only） |
| `page_slice_copy.h` | 组合 copy API |
| `page_slice_copy.c` | buffer/slice 间拷贝、deep clone |

---

## 6. 流程

### 6.1 插入 pgoff

1. 根据 pgoff 计算各级 index；必要时 allocate index 页（kallocator）并 grow 树。
2. 在 leaf 页找槽位，填 KVA + `PAGE_SLICE_FLAG_VALID`。
3. 更新父级 `live` 与 `mapped_entries`。

### 6.2 copy_to_buffer

 walk 源 slice 的 pgoff 范围，对每个 VALID 叶 map/lookup KVA，`memcpy` 到连续 `dst`（跨洞跳过或零填由 API 定义，见实现注释）。

### 6.3 page_slice_clone

新建 slice（同 `size`），仅对 src 中 VALID pgoff 分配新页并复制内容；**不**复制 `append_page_slice_info`（兼容层 hook 自行 copy 元数据）。

---

## 7. 公开 API

```c
/* 见 page_slice.h — 创建/销毁/insert/lookup/remove 等 */

error_t page_slice_copy_to_buffer(struct page_slice* slice, u64 byte_off,
                                  void* dst, size_t len);
error_t page_slice_copy_to_slice(struct page_slice* dst, u64 dst_byte_off,
                                 struct page_slice* src, u64 src_byte_off,
                                 size_t len);
error_t page_slice_clone(struct page_slice** dst_out, struct page_slice* src);
```

完整符号列表以头文件为准。

---

## 8. 多架构

pgoff 与 KVA 均为 64 位逻辑地址；backing 页来自 buddy/kmalloc，与 ISA 无关。页大小假定 4 KiB（`PAGE_SIZE`）。

---

## 9. 测试

`page_slice` 专用测例若启用则在 core test 套件中；本篇与当前源码一致，尚未单独复测。

---

## 10. 限制与后续

- 仅内核；与用户 VSpace 正交。
- 深度与容量受 `PAGE_SLICE_MAX_INDEX_HEIGHT` 等编译常量限制。
- 大稀疏区遍历效率依赖 index 形状；无硬件 huge page 聚合。

---

## 11. 变更记录

- 2026-08-26：v0.1 初稿，稀疏 pgoff 索引与 copy/clone 辅助。
