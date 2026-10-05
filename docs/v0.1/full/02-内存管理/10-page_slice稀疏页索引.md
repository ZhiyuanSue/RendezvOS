# page_slice 稀疏页索引

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/page_slice.c`、`kernel/mm/page_slice_copy.c`、`include/rendezvos/mm/page_slice.h`、`include/rendezvos/mm/page_slice_copy.h`。

内核堆见 `09-kmalloc与内核堆.md`；用户 VA 以 `08-Radix树与用户映射.md` 为准。**page_slice 只服务内核侧逻辑缓冲，不替代用户 radix**——两者不要混用。

---

## 1. 概述

Buddy（伙伴分配器，按 2 的幂次合并/拆分物理页，见 `09-kmalloc与内核堆.md`）单次连续物理分配的上限约为 **2 MiB**。内核有时还想按「文件偏移 / 大数组下标」这种**逻辑连续**的方式取一块远大于 2 MiB 的缓冲，但物理上、甚至 KVA 上并不要求连成一片。

**page_slice** 做的就是这件事：在一个 slice 里用稀疏多级索引，把 **pgoff（页号偏移）** 绑定到调用方已经准备好的 **离散内核虚拟地址（KVA，Kernel Virtual Address）**。连续性只在偏移算术上成立。

分工可以记成三句话：用户 VA、mmap、COW 走 VSpace + `mm_user_utils_*`；小对象 / 整页内核堆走 `percpu(kallocator)`；**逻辑连续、物理按页稀疏** 的内核缓冲（例如页缓存一类）才走 page_slice。

更关键的一条：**内容页由调用方自己 `m_alloc` 再 `insert`**；slice 只维护自己的 index / leaf **壳页**（壳页指 slice 自己的索引页和叶页，属于树结构本身，不是装用户数据的内容页）。不要期望 `insert` 同时分配内容页。

---

## 2. 目标与边界

提供：create / destroy、按 pgoff insert / lookup / remove、逻辑 `size` 伸缩、copy / clone 辅助。

不做：文件系统页缓存策略；用户 `mmap`；硬件 MMU pin——`PAGE_SLICE_FLAG_PIN` 只表示 destroy / remove 时**不要**对内容 kva 调 `m_free`，不是页表 pin。

也**不是 COW**：`page_slice_clone` 是对新页做 **memcpy 深拷贝**，只实现已 VALID 的 pgoff；没有「共享再 fault」。

---

## 3. 分层与调用方

上层（例如兼容层的页缓存）持 `struct page_slice *`：`insert` 绑定已有 kva → `lookup` / `copy_to_*` 读 → 需要副本时 `clone`。与用户缓冲交互另走 `map_handler_user_kernel_copy` 一类路径，**不**进 `thread->vs` 的 radix。

每 slice 一把 `cas_lock`，API 内部加锁。**lookup 返回前会释放锁**——并发 remove 可能让指针立刻失效，调用方不要假设指针长期有效。copy 路径同样是「lookup 之后再 memcpy」，不会跨整段拷贝一直持锁。

---

## 4. 数据结构与不变量

### 4.1 位布局（逻辑）

```text
[ L2:9 | L1:9 | L0:9 | leaf_idx:7 | page_off:12 ]
```

叶页大约 128 槽（`PAGE_SLICE_LEAF_CAPACITY`）；索引页 512 个 tagged_ptr（`PAGE_SLICE_INDEX_CAPACITY`）。高度上限使逻辑跨度大约到 64 TiB 量级（以头文件 `PAGE_SLICE_MAX_*` 为准）。

```c
struct page_slice_entry {
        vaddr kernel_virtual_address;
#define PAGE_SLICE_FLAG_VALID (1ULL << 0)
#define PAGE_SLICE_FLAG_PIN   (1ULL << 1)
        u64 flags;
        union {
                struct list_entry page_list_node;
                u64 padding[2];
        };
};

typedef tagged_ptr_t page_slice_index_entry_t;
/* 索引项是「带 tag 的指针」（tagged pointer）：把 48 位指针和 16 位
   额外信息打包进一个 64 位字。这里 16 位 tag 拆成 live:13 + height:3，
   即 ptr:48 | live:13 | height:3 */

#define PAGE_SLICE_LEAF_CAPACITY  (PAGE_SIZE / sizeof(struct page_slice_entry))
#define PAGE_SLICE_INDEX_CAPACITY (PAGE_SIZE / sizeof(page_slice_index_entry_t))

struct page_slice {
        page_slice_index_entry_t root;
        u64 size;
        u64 mapped_entries;
        cas_lock_t lock;
        u8 append_page_slice_info[];   /* 柔性数组尾：上层附加的私有元数据 */
};
```

> **关于 `append_page_slice_info[]`（柔性数组成员）**：这是 C99 的「柔性数组成员」（flexible array member）——结构体最后一个字段是 `u8[]`，长度不固定。`page_slice_create(append_info_size, …)` 会按 `append_info_size` 多分配一段内存紧跟在 `struct page_slice` 后面，供上层（例如页缓存）挂自己的私有字段（inode 指针、LRU 链节点等）。`append_info_size` 可为 0（不附加）。`clone` **不**拷贝这段——目标用 `create(0, size)`。

### 4.2 高度与覆盖范围

`slice->root` 上的 height（stored height，存储高度）表示当前 radix 深度。**`ps_entry_new(NULL, height, 0)` 不是空槽**——它构造一个 tag 非零但指针为空的 tagged pointer，会被当成「已占用但 ptr 为空」，导致后续 `-E_RENDEZVOS`。空槽必须是 `tp_new_none()`（全 0 的 tagged pointer，即「这里什么都没有」的标记）。

| `PS_HEIGHT_*` | 值 | 含义 | 约可覆盖 pgoff |
|---------------|-----|------|----------------|
| `EMPTY` | 0 | 无映射 | — |
| `LEAF` | 1 | root 直接指向 leaf 页（无 index 父） | 0 … 127 |
| `INDEX1` | 2 | root 为 index 页，子为 leaf | … ≈ 64K 页 |
| `INDEX2` | 3 | 多一层 index | … ≈ 32M 页 |
| `INDEX3` | 4 | 再一层 index | … ≈ 16G 页（`PAGE_SLICE_MAX_BYTE_SIZE` 上限） |

`live` 计数的对象随 entry 的 height 变化（以 `ps_entry_live_max_table` 为准）：

| entry height | live 计数对象 | 上限 |
|----|----|----|
| 1（direct leaf root） | leaf 页内已 bind 的 pgoff 数 | 128 |
| 2（指向 leaf 的 index 项） | 同上 | 128 |
| 2（`slice->root` 为 index 页） | index 页内非空子 slot 数 | 512 |
| 3–4（指向 index 的项） | 子 index 页内非空 slot 数 | 512 |

`slice->mapped_entries` 仅上层统计（带 `PAGE_SLICE_FLAG_VALID` 的 pgoff 数）；grow / shrink **不**读它，靠 `live` 驱动回收。

### 4.3 PIN

`PAGE_SLICE_FLAG_PIN`：默认 remove / destroy **跳过**对内容 kva 的 `m_free`。再次强调：这不是页表 pin。

### 4.4 树的生长：`ps_raise_height` + `ps_descend_pgoff`

整棵树**只在 `insert` 路径上长高**，分两步：先把 root 抬到目标高度（`ps_raise_height`），再按 pgoff 下降到叶、缺页就补（`ps_descend_pgoff`）。

**`ps_raise_height`**：循环到 `stored_height == target_height`，每轮「包一层」——新分配一页 index 页，旧 root 作为新 root 的第 0 个子项，新 root 的 live = 旧 root 是否非空。注意：**EMPTY 直接跳到 INDEX1**（不是先变 LEAF），因为 LEAF 只能由 `ps_descend_pgoff` 在「root 为空且要插第一页」时直接置上。

```text
   raise_height：每轮在现有 root 外包一层 index。
   从 EMPTY 起步时直接跳到 INDEX1（root 指向一张空 index 页，slot 全是 tp_new_none）。
   从 LEAF 起步时，旧 leaf 成为新 index 的 slot[0]：

   LEAF (h=1)         INDEX1 (h=2)          INDEX2 (h=3)
   root → leaf page   root → index          root → index
                      ┌──[0]→ 旧 leaf       ┌──[0]→ 旧 INDEX1 页
                      ├──[1]→ none          ├──[1..]→ none
                      └──[511]→ none        └──[511]→ none
                      live=1                live=1

   每轮：新 alloc 一页 → 旧 root 作为新 root[0] → 新 root 的 live = (旧 root 非空 ? 1 : 0)
   空槽一律 tp_new_none()，绝不 ps_entry_new(NULL, h, 0)
```

**`ps_descend_pgoff`**：按 pgoff 算出每层 slot 索引，从 root 向下走；遇到空 slot 且 `create=true` 就 alloc 一页、发布到父 slot、`ps_entry_live_inc` 父项；走到最底层拿到 leaf 槽。同时把走过的 structural path（root / index1 / index2）记下来，供后续 remove 的 cascade 使用。

```text
   descend_pgoff(pgoff=P, create=true)：

   stored_h=3 (INDEX2)
   root ──[idx_L2(P)]→ index1 page ──[idx_L1(P)]→ index2 page ──[idx_L0(P)]→ leaf page ──[leaf_idx(P)]→ entry
        │                          │                          │
        │ path0                    │ path1                    │ path2
        ▼                          ▼                          ▼
   structural_parent 链（remove 时用于 cascade_up）

   走到某层 slot 为空：
     alloc 一页 → 父 slot = ps_entry_new(child, page_h, 0) → ps_entry_live_inc(父)
   走到 leaf slot 为空：
     ps_alloc_leaf_page → leaf slot = ps_entry_new(leaf, LEAF, 0)
```

### 4.5 树的收缩：`ps_remove_page_locked` + `ps_cascade_up_empty_index` + `ps_shrink`

整棵树**只在 `remove` / `set_size` 路径上回收**，分三层：先清叶、再自下而上 cascade 释放空 index 页、最后 `ps_shrink` 把 root 一层层 unwrap 到合适高度。

**`ps_remove_page_locked`**：先 `ps_descend_pgoff(create=false)` 拿到 leaf entry 与 structural path；清叶（非 PIN 则 `m_free` kva）、`mapped_entries--`；然后 `ps_entry_live_dec(leaf_index)`——若 leaf 页 live 归零，释放 leaf 页、父项 `live_dec`，再调 `ps_cascade_up_empty_index`。

**`ps_cascade_up_empty_index`**：从 path 末端（最深层）向上走，遇到 `live == 0` 的 index 项就 `m_free` 子页、把该项置回 `tp_new_none()`、父项 `live_dec`；遇到 `live != 0` 就 break——上面还有别的子树，不能再释放。

```text
   remove(pgoff=P) 后的 cascade_up：

   删 P 之前：                       删 P 之后：
   root                              root
   └─[idx_L2(P)]→ index1 (live=2)    └─[idx_L2(P)]→ index1 (live=1) ← 还有别的子，break
      ├─[idx_L1(P)]→ index2 (live=1)     ├─[idx_L1(P)]→ tp_new_none() (释放 index2 页)
      │  └─[idx_L0(P)]→ leaf (live=1)    │  └─[idx_L0(P)]→ tp_new_none() (释放 leaf 页)
      │     └─[P]→ entry                │
      └─[other]→ ...                    └─[other]→ ...

   cascade_up 从 path2 末端开始：
     path2 (index2 项) live=0 → 释放 index2 页、置空、path1 live_dec
     path1 (index1 项) live=1 → break（上面还有 other 子树）
     path0 (root) 不动
```

**`ps_shrink`**：在 `set_size` 变小或 `remove` 之后调用。循环条件：root 是 index 且 live==1——意味着 root 只剩一个子项，可以「降一层」：把唯一非空子项提升为新 root，旧 root 页 `m_free`。直到 root 是 leaf 或 live≠1。最后若 root live==0，整棵树清空（`page_slice_root_clear`）。

```text
   shrink 一轮（root 是 index、live==1）：

   旧 root (index, live=1)              新 root (子项提升)
   ┌──[0]→ child (live=L)              child (live=L, height-1)
   ├──[1]→ tp_new_none()        →
   ├──...                              旧 root 页 m_free
   └──[511]→ tp_new_none()

   特例：若旧 root height=INDEX1，子项是 leaf_index（不是指针），
        直接把 leaf 页提升为 root（root 变 LEAF，非 INDEX）。

   终止条件：
     - root 是 leaf（不能再降）
     - root 是 index 但 live≠1（有多子或全空）
     - root live==0 → page_slice_root_clear（整树空）
```

**生长 / 收缩的对偶性**：`ps_raise_height` 在 root 之上「包一层 index」，`ps_shrink` 在 root 之下「剥一层 index」；`ps_descend_pgoff` 在下降时 alloc + `live_inc`，`ps_cascade_up_empty_index` 在上升时 `live_dec` + free。两套操作都靠 `live` 计数判定「这一层还有没有别的子」，从而决定能不能释放——这是整棵树动态支撑稀疏 pgoff 的核心算法。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `page_slice.h` / `.c` | 稀疏索引、锁、insert / lookup / remove、`set_size` |
| `page_slice_copy.h` / `.c` | buffer / slice / user 拷贝与 `clone` |

测试用例：`modules/test/single_page_slice_test.c`（以树内实际文件名为准）。

---

## 6. 流程

### 6.1 绑定一页

调用方准备好内核 kva → `page_slice_insert_page`（缺少壳页时分配 index / leaf）→ 槽上置位 VALID。

### 6.2 copy / clone

- `page_slice_copy_to_buffer`：区间必须**全部已映射**，否则失败（**不**跳过空洞、不零填）。
- `page_slice_copy_to_slice`：目标 pgoff 须**已绑定**；拒绝危险自重叠。
- `page_slice_copy_to_user`：同样要求已映射，再经 `map_handler_user_kernel_copy`。
- `page_slice_clone`：新建同 `size` 的 slice；只深拷贝 VALID；清除 PIN；空洞不实现。

### 6.3 shrink

`page_slice_set_size` 变小时：先拆除超出新长度的高 pgoff（`ps_remove_page_locked`），再 `ps_shrink` unwrap 只剩一个子项的 root。算法见 §4.5。

---

## 7. 公开 API

本篇涉及的接口分布在：`include/rendezvos/mm/page_slice.h`、`include/rendezvos/mm/page_slice_copy.h`。接口说明以头文件注释为准，并已与实现核对。内容页须调用方先 `m_alloc` 再 `insert`；slice 只维护 index/leaf 壳。不替代用户 radix（见 08）。

**纠正：** `page_slice_lookup` 返回 `struct page_slice_entry *`（不是 `error_t`）；`create` 参数类型为 `size_t`。

### 7.1 编排顺序（典型上层用法）

| 步骤 | API |
|------|-----|
| 建空 slice | **`page_slice_create(append_sz, logical_size)`**（`logical_size > 0`） |
| 绑内容页 | 调用方 `m_alloc` → **`page_slice_insert_page(slice, pgoff, kva, flags)`** |
| 读 | **`page_slice_lookup`**（返回前已放锁）或 **`copy_to_*`** |
| 伸缩逻辑长度 | **`page_slice_set_size(&slice, new_size)`**（`0` ≡ destroy） |
| 副本 | **`page_slice_clone(&dst, src)`**（深拷 VALID；不拷贝柔性数组尾） |
| 卸一页 / 整体销毁 | **`remove_page`** / **`destroy(&slice)`** |

每 slice 一把 `cas_lock`，上述 API 内部加锁。lookup/copy **不**保证返回指针在 unlock 后仍有效。

### 7.2 生命周期与索引（`page_slice.h`）

```c
struct page_slice *page_slice_create(size_t append_info_size, size_t slice_size);
error_t page_slice_destroy(struct page_slice **slice);
u64 page_slice_get_size(struct page_slice *slice);
error_t page_slice_set_size(struct page_slice **slice, u64 new_size);

error_t page_slice_insert_page(struct page_slice *slice, u64 pgoff, vaddr kva, u64 flags);
struct page_slice_entry *page_slice_lookup(struct page_slice *slice, u64 pgoff);
error_t page_slice_remove_page(struct page_slice *slice, u64 pgoff);
```

| 接口 | 说明 |
|------|------|
| `create` | 空树；`slice_size` 须 ∈ (0, `PAGE_SLICE_MAX_BYTE_SIZE]`。柔性数组尾 `append_page_slice_info[]` 长度=`append_info_size`（可为 0）。失败 NULL。 |
| `destroy` | 递归释放壳页；对叶 kva 调 `m_free`，除非 `PAGE_SLICE_FLAG_PIN`。成功 `*slice=NULL`。 |
| `set_size` | `new_size==0` → destroy。变大只改逻辑长度（树长高在后续 insert）。变小拆除高 pgoff 并可 unwrap 空壳。超上限 → `-E_REND_OVERFLOW`。 |
| `insert_page` | 绑定已有 `kva`（非 0）；成功必置 `VALID`。同 kva 幂等；异 kva 已 VALID → `-E_REND_AGAIN`。**不**分配内容页。 |
| `lookup` | 已绑定返回 entry 指针；缺失/无效/越界 → NULL。**返回前释放锁**。 |
| `remove_page` | 清叶；非 PIN 则 `m_free` kva；可级联释放空壳。未映射 → `-E_REND_NOFOUND`。 |

标志：`PAGE_SLICE_FLAG_VALID`（insert 置位）；`PAGE_SLICE_FLAG_PIN` = destroy/remove **跳过**内容 `m_free`（非 MMU pin）。`mapped_entries` 仅上层统计。

### 7.3 拷贝与 clone（`page_slice_copy.h`）

```c
error_t page_slice_copy_to_buffer(struct page_slice *, u64 byte_off, void *dst, size_t len);
error_t page_slice_copy_to_slice(struct page_slice *dst, u64 dst_off,
                                 struct page_slice *src, u64 src_off, size_t len);
error_t page_slice_copy_to_user(struct VSpace *vs, u64 user_va,
                                struct page_slice *, u64 file_byte_off, size_t len);
error_t page_slice_clone(struct page_slice **dst_out, struct page_slice *src);
```

| 接口 | 说明 |
|------|------|
| `copy_to_buffer` | 区间须**全部已映射**，遇到空洞 → `-E_RENDEZVOS`（不零填）。`len==0` 成功。 |
| `copy_to_slice` | 目标 pgoff 须已绑定；不为 dst 插入；拒绝危险自重叠。 |
| `copy_to_user` | 同源须已映射；经 `map_handler_user_kernel_copy` 写入 `vs`。 |
| `clone` | 同 `size` 新 slice；只深拷贝 VALID（新页归 dst）；空洞不实现；清除 PIN；**不**拷贝 `append_page_slice_info[]` 柔性数组尾。**不是 COW**。 |

壳页与 clone 新内容页均来自 `percpu(kallocator)`。

---

## 8. 多架构

与 ISA 无关；依赖本核 `kallocator`，以及内容 kva 在内核侧可访问。

---

## 9. 测试

page_slice 相关测试用例。在 `core/` 内：

```bash
make ARCH=x86_64 config && make all && make run
```


---

## 10. 限制与后续

- lookup 返回的指针生命周期短。
- copy **遇到空洞即失败**。
- 无内置 COW / 页缓存策略。
- 跨 CPU 共享同一 slice 只有粗锁，上层宜自己串行化。

---

## 11. 变更记录

- 2026-10-05（可读性二轮）：展开首次出现未解释的术语——Buddy（伙伴分配器）、KVA（内核虚拟地址）、tagged pointer（带 tag 的指针，补 bit 布局说明）、`tp_new_none()`（全 0 的空 tagged pointer 标记）；§4.1「壳页」补一句括注说明是树结构自己的索引页/叶页而非内容页；§7 表里「FAM append」改成「柔性数组尾」与 §4.1 一致。
- 2026-10-05：补 §4.2 高度覆盖表（EMPTY=0/LEAF=1/INDEX1=2/INDEX2=3/INDEX3=4）与 `ps_entry_new(NULL,height,0)` 反例；补 §4.4 树的生长（`ps_raise_height` 包一层 index、`ps_descend_pgoff` 下降时 alloc + live_inc，含 ASCII 图示）；补 §4.5 树的收缩（`ps_remove_page_locked` 清叶 + `ps_cascade_up_empty_index` 自下而上释放空 index 页 + `ps_shrink` root 降层，含 cascade 与 shrink 图示，点出 grow/shrink 与 descend/cascade 的对偶性）。恢复 `struct page_slice` 定义。来源：`core/docs/old/page-slice.md` 迁移审核。
- 2026-10-04：全文中文表述整理——「顶在」→「上限约为」；「摸一块」→「取一块」；「绑到」→「绑定到」；「指望…顺手给你」→「期望…同时分配」；「物化」→「实现」；「打交道」→「交互」；「放锁」→「释放锁」；「塞」→「放入」；「数的是」→「统计的是」；「靠它回收」→「依赖它回收」；「挂元数据」→「附加元数据」；「不拷」→「不拷贝」；「缺壳」→「缺少壳页时」；「标 VALID」→「置位 VALID」；「跳洞」→「跳过空洞」；「深拷/剥掉/物化」→「深拷贝/清除/实现」；「拆掉」→「拆除」；「整拆」→「整体销毁」；「放壳页/放空壳」→「释放壳页/释放空壳」；「拆高 pgoff」→「拆除高 pgoff」；「返回前解锁」→「返回前释放锁」；「遇洞」→「遇到空洞」；「不替 dst insert」→「不为 dst 插入」；「拒」→「拒绝」；「剥 PIN」→「清除 PIN」；§7「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-04：§4 补 `page_slice_entry` / `page_slice` 与容量宏。
- 2026-09-27：中文用语整理（真源→以…为准；§7「以头文件注释为准」；「养壳」→「维护壳页」）。
- 2026-09-26：§7 全文审阅——按头文件注释补接口说明与编排；纠正 `lookup` 返回类型；补强 `page_slice_copy.h` 注释（遇洞失败、clone≠COW）。
- 2026-09-25：语言整理；纠正 `destroy` 签名为 `page_slice **`。
- 2026-08-29：整篇重做——纠正「insert 分配内容页」；copy 遇洞失败；clone=深拷非 COW。
- 2026-08-26：v0.1 初稿。
