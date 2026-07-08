# page_slice — 内核稀疏页索引

> **文档角色：** 子系统参考（maintained）  
> **入口：** [`USING_CORE.md`](USING_CORE.md) · [`GUIDE.md`](GUIDE.md) §6 · [`memory.md`](memory.md)  
> **头文件：** `include/rendezvos/mm/page_slice.h`  
> **实现：** `kernel/mm/page_slice.c`  
> **测试：** `modules/test/single_page_slice_test.c`（`page_slice_test`）

---

## 1. 解决什么问题

Buddy PMM 单次连续分配上限约 **2 MiB**。内核线性映射区若拿不到更大连续物理块，仍需要按 **逻辑字节偏移** 访问“像连续缓冲区一样”的数据（大数组、page cache 等）。

**page_slice** 在 **单个 slice 内** 用固定 radix 把 **file page index（pgoff）** 映射到 **调用方提供的内核 kva**：

- **不**替代 `VSpace` / radix tree / `mm_user_utils`（用户地址空间仍走 [`memory.md`](memory.md)）。
- **不**替调用方分配内容页；只维护 **radix 壳页**（index/leaf 页）并在 remove/destroy 时按 flags 决定是否 `m_free` 内容 kva。
- 每个 slice 自带 **`cas_lock_t`**；API 内部加锁，**未**做跨 CPU 并发优化（上层若共享 slice，需自行串行或接受锁竞争）。

---

## 2. 与 kmalloc / radix 的分工

| 需求 | 用 |
|------|-----|
| 用户 VA、mmap、COW、页表 | `VSpace` + `mm_user_utils_*` |
| 小对象、整页内核堆 | `percpu(kallocator)` |
| **逻辑上连续、物理上按页稀疏** 的内核 buffer（按 pgoff 索引） | **page_slice** |

典型上层：**page cache** — 文件偏移 → pgoff → `page_slice_lookup` → 内容页 kva。

---

## 3. 核心对象

| 对象 | 含义 |
|------|------|
| `struct page_slice` | 容器：`root`（单 index entry）、`size`（逻辑字节长）、`mapped_entries`（统计）、`lock` |
| `struct page_slice_entry` | **Leaf 槽**：`kernel_virtual_address`、`flags`、可选 `page_list_node` |
| `page_slice_index_entry_t` | **Index 链**：`tagged_ptr` = ptr + **height** + **live** |

### 3.1 pgoff 与高度

pgoff = 逻辑页号，`0` 为 buffer 起点。`PAGE_SLICE_BYTE_TO_PGOFF` / `PAGE_SLICE_IN_PAGE_OFF` 用于字节偏移换算。

**`slice->root` 上的 height（stored height）** 表示当前 radix 深度：

| `PS_HEIGHT_*` | 值 | 含义 | 约可覆盖 pgoff |
|---------------|-----|------|----------------|
| `EMPTY` | 0 | 无映射 | — |
| `LEAF` | 1 | root 直接指向 **leaf 页**（无 index 父） | 0 … 127 |
| `INDEX1` | 2 | root 为 index 页，子为 leaf | … ≈ 64K 页 |
| `INDEX2` | 3 | 多一层 index | … ≈ 32M 页 |
| `INDEX3` | 4 | 再一层 index | … ≈ 16G 页（`PAGE_SLICE_MAX_BYTE_SIZE` 上限） |

**实现与测试重点在 height 0–2（EMPTY / direct leaf / INDEX1）**：grow、shrink、cascade、live 最容易出错的组合集中在这里。  
**INDEX2+** 是同结构的 **逐层加深**：每层 index 页 512 slot，多占几页 radix 壳，语义不变；稀疏映射时内存开销仍可控。

Radix 位布局（`page_slice.h` 宏）：

```text
[ index_L2 (9) | index_L1 (9) | index_L0 (9) | leaf_idx (7) | page_off (12) ]
```

### 3.2 live 语义（必读）

`live` 统计的是 **该 index entry 所指数页里，有多少个“占用 slot”**，含义随 **entry 的 height** 变化：

| entry height | live 计数对象 | 上限 |
|--------------|-----------------|------|
| 1（仅 direct leaf root） | leaf 页内已 bind 的 pgoff 数 | 128 |
| 2（指向 leaf 的 index 项） | 同上 | 128 |
| 2（`slice->root` 为 index 页） | index 页内非空 **子 slot** 数 | 512 |
| 3–4（指向 index 的项） | 子 index 页内非空 slot 数 | 512 |

`slice->mapped_entries` 仅 **上层统计**（带 `PAGE_SLICE_FLAG_VALID` 的 pgoff 数）；grow/shrink **不**读它。

**空 index slot 必须是 `tp_new_none()`（全 0）**。`ps_entry_new(NULL, height, 0)` **不是** empty：tag 非零会被当成“已占用但 ptr 为空”，导致 `-E_RENDEZVOS`。

---

## 4. 调用方契约（生命周期）

### 4.1 谁分配内容页

1. 调用方 `kallocator->m_alloc(..., PAGE_SIZE)` 得到 **kva**。
2. `page_slice_insert_page(slice, pgoff, kva, flags)` 绑定。
3. 默认 `flags == 0`：`remove` / `destroy` 时 slice 对 kva 调用 **`m_free`**。
4. `PAGE_SLICE_FLAG_PIN`：remove/destroy 只 unmap **槽位**，**不** `m_free` kva；调用方自行释放。

**不要**在 remove 后再对同一 kva 手动 `m_free`（除非 PIN）。

### 4.2 推荐调用顺序

```c
struct allocator *alloc = percpu(kallocator);
struct page_slice *sl = page_slice_create(append_bytes, logical_size);
void *page = alloc->m_alloc(alloc, PAGE_SIZE);
page_slice_insert_page(sl, pgoff, (vaddr)page, 0);

struct page_slice_entry *e = page_slice_lookup(sl, pgoff);
/* 读写字节：e->kernel_virtual_address + PAGE_SLICE_IN_PAGE_OFF(byte_off) */

page_slice_remove_page(sl, pgoff);   /* 或 page_slice_destroy(&sl) */
```

### 4.3 append 区（FAM）

`page_slice_create(append_info_size, slice_size)` 在 `struct page_slice` 后附加 `append_info_size` 字节，供 page cache 等挂私有字段：

```c
struct my_cache_slice {
        struct page_slice base;
        /* 或使用 container_of / 强制转换 append 区 */
};
```

创建时 `append_info_size` 须覆盖调用方扩展结构（不含 `sizeof(struct page_slice)` 本身）。

### 4.4 逻辑长度

| API | 作用 |
|-----|------|
| `page_slice_get_size` | 读 `size` |
| `page_slice_set_size(&slice, new_size)` | 放大仅改 `size`；**缩小**会 remove 超出范围的 pgoff 并 `ps_shrink` |
| `new_size == 0` | 等价 `page_slice_destroy` |

缩小后，超出新 `page_slice_page_count` 的 pgoff **lookup 为 NULL**（可能仍曾短暂存在于树上，set_size 会主动 tear down 超出页）。

---

## 5. 公开 API 摘要

| 函数 | 说明 |
|------|------|
| `page_slice_create` | 空 slice；失败返回 NULL |
| `page_slice_destroy` | 释放整棵树 + 非 PIN 内容页 + header；`*slice = NULL` |
| `page_slice_get_size` / `page_slice_set_size` | 逻辑字节长 |
| `page_slice_lookup` | **不 grow**；无有效映射返回 NULL |
| `page_slice_insert_page` | 可 grow radix；同 pgoff 同 kva 幂等；不同 kva → `-E_REND_AGAIN` |
| `page_slice_remove_page` | 清槽、cascade、shrink（多 pass） |

### 5.1 常见 errno

| 返回值 | 含义 |
|--------|------|
| `REND_SUCCESS` | 成功 |
| `-E_IN_PARAM` | NULL、pgoff 越界、kva==0 |
| `-E_REND_AGAIN` | 该 pgoff 已绑定 **不同** kva |
| `-E_REND_NOFOUND` | remove/lookup 无有效叶 |
| `-E_REND_NO_MEM` | index/leaf 壳页 kmalloc 失败 |
| `-E_REND_OVERFLOW` | 超过 `PAGE_SLICE_MAX_BYTE_SIZE` 或树高上限 |
| `-E_RENDEZVOS` | 内部路径冲突（例如损坏/假占用 index slot） |

---

## 6. 与 page cache 的衔接（上层）

core **不**实现 cache 策略；上层典型分工：

1. **slice 一条** 对应一个 cache 对象 / inode 区段，`size` = 文件区段或映射窗口字节长。
2. **缺页**：分配 kva → `insert`；**evict**：`remove`（或 shrink 窗口 `set_size`）。
3. 用 `page_slice_entry.page_list_node` 挂 LRU 等链表（insert 成功后会 `INIT_LIST_HEAD`）。
4. 需要 pin 住底层页（别处仍持有 kva）时用 **`PAGE_SLICE_FLAG_PIN`**。

内存回收 / swap **尚未**与 slice 集成；evict 路径应走 `remove` 或上层释放 kva，不要假设 core 会 swap。

---

## 7. 测试

`page_slice_test`（single-CPU）包含：

- 固定边界：pgoff 0/127/128/129、INDEX2/INDEX3 边界、dual-branch、set_size shrink
- **多 seed** rand64 fuzz：small（256 页）、INDEX2 全范围、INDEX3 **稀疏**（映射页上限 32，控内存）

通过测试表示 **单核主路径 + 随机 insert/remove** 与 shadow 一致；**不**包含 SMP 压测或密集 INDEX3 满映射。

---

## 8. 实现索引

| 主题 | 位置 |
|------|------|
| grow / wrap | `ps_raise_height` |
| 按 pgoff 下降 / 创建路径 | `ps_descend_pgoff` |
| remove + cascade | `ps_remove_page_locked`, `ps_cascade_up_empty_index` |
| shrink / unwrap | `ps_shrink` |
| live 表 | `ps_entry_live_max_table` |

---

*以 `page_slice.h` / `page_slice.c` 为准；行为变更请同步更新本文与 [`GUIDE.md`](GUIDE.md) §6。*
