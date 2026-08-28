# 无锁队列与 EBR 设计

v0.1 · 2026-08-27

本篇覆盖：`include/common/dsa/ms_queue.h`、`include/common/taggedptr.h`、`kernel/task/ebr.c`、`kernel/ipc/ipc.c`、`kernel/ipc/message.c`。

EBR 在调度与线程 teardown 中的用法见 `03-任务与调度/EBR与线程资源回收.md`；IPC 会合语义见 `Port与消息模型.md` 与 `阻塞与非阻塞收发.md`；tagged pointer 位布局亦见 `taggedptr.h` 注释。

---

## 1. 概述

RendezvOS v0.1 的并发队列核心是 **Michael–Scott 无锁 MSQ**（`ms_queue_t`），通过 **tagged pointer** 在指针低位编码 small tag（port 会合状态、check 条件等），配合 **EBR（Epoch-Based Reclamation）** 延迟释放节点，避免 dequeue 读者与 `kfree` 的 UAF。

三处主要用法：

1. **Port `thread_queue`** — 元素为 `Ipc_Request_t`；tail tag = `IPC_PORT_STATE_*`。
2. **Per-thread `send_msg_queue` / `recv_msg_queue`** — 元素为 `Message_t` 嵌入的 `ms_queue_node_t`。
3. **kmalloc 跨核 free 队列** 等（见 kmalloc 篇）— 同一 MSQ 原语。

本篇侧重 **设计意图、ABA/tag 策略、EBR 与 MSQ 的配合**；API 逐行说明以头文件为准。

---

## 2. 目标与边界

core 选择在 **单生产者/多消费者或 MPMC 会合场景** 用 MSQ + EBR，而不是全局锁保护链表，以便 IPC hot path 在 SMP 下扩展。

**有意不包含：** 通用阻塞队列；优先级队列；内核 malloc 层对 EBR 的隐藏（caller 显式 `ebr_retire_ref`）；跨进程队列。

**设计权衡：** MSQ dequeue 会 **移动 dummy 节点** 并 `ref_put` 旧 dummy，故 message 拆成 `Msg_Data_t` + `Message_t` shell（Port 篇 §4.2）。EBR retire 表 overflow 时 **leak** 而非 UAF（见 EBR 篇 §6.2）。

---

## 3. 分层与调用方

**IPC 实现**（`ipc.c`）— 只通过 `msq_enqueue` / `msq_dequeue` / `msq_enqueue_check_tail` / `msq_dequeue_check_head` 操作 port 队列；**不** 直接 malloc 队列节点（Ipc_Request 在外部分配）。

**Message**（`message.c`）— `free_message_ref` → EBR → `free_message_ref_real` 真正销毁。

**任意 MSQ 读者** — 必须在 `ebr_enter()` 与 `ebr_exit()` 之间完成指针遍历（`ms_queue.h` inline 已包裹）。

**集成方** — 一般 **不** 直接操作 port `thread_queue`；仅使用 send/recv API。若扩展新无锁结构，须同样配对 EBR 或证明无并发读。

---

## 4. 数据结构与不变量

### 4.1 ms_queue_node_t / ms_queue_t

```c
typedef struct {
        ref_count_t refcount;
        tagged_ptr_t next;
} ms_queue_node_t;

typedef struct Michael_Scott_Queue {
        tagged_ptr_t head;
        tagged_ptr_t tail;
        size_t append_info_bits;  /* tag 可用位数，≤15 */
} ms_queue_t;
```

- 队列 **不** 内嵌 payload；用 `container_of` 从 node 得 `Ipc_Request_t` / `Message_t`。
- **Dummy 节点** — `msq_init` 用 caller 提供的空 node 作初始 head/tail；dequeue 成功后旧 dummy 经 `free_func` 释放（可能 EBR）。

### 4.2 tagged pointer

`taggedptr.h` 将 user pointer 与 tag 打包（低位 tag 位数由 `append_info_bits` 限制）。Port IPC 用 **2 bit** 存 `IPC_PORT_STATE_*`；`msq_enqueue_check_tail` 要求 tail tag 与期望值 CAS 一致才 enqueue。

**ABA：** 依赖 tag 递增（MS 算法标准做法）与 EBR 延迟 reuse；v0.1 不使用 wide counter ABA 除 tag 外。

### 4.3  refcount 约定

- 入队节点：caller 分配，`ref_init` 或 `ref_get_not_zero` 后 enqueue；enqueue 内可能 claim refcount。
- dequeue 返回数据 node 时已 **increment** refcount；caller `ref_put(..., free_func)`。
- `free_func` 对 IPC/message 通常为 `free_ipc_request` / `free_message_ref` → **EBR retire**。

### 4.4 EBR 与 MSQ 协作（摘要）

| 角色 | 动作 |
|------|------|
| MSQ 读者 | `ebr_enter` … 读 next 指针 … `ebr_exit` |
| 节点最后一个 ref_put | `ebr_retire_ref(ref, real_free)` |
| 任意 schedule | `ebr_try_reclaim` 扫描 retire_epoch < safe_epoch |

**safe epoch** — 所有 active CPU 的 `local_epoch` 最小值与 global epoch 的关系见 EBR 篇 §4.3。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `ms_queue.h` | MSQ 全部 inline 操作 + EBR 包裹 |
| `taggedptr.h` | tag/ptr 打包与 CAS |
| `ebr.c` / `ebr.h` | epoch 与 retire 表 |
| `ipc.c` | port check_head/dequeue、request free |
| `message.c` | message free retire |
| `task_manager.c` | schedule → reclaim |

主要 MSQ 入口：`msq_init`、`msq_enqueue`、`msq_dequeue`、`msq_enqueue_check_tail`、`msq_dequeue_check_head`、`msq_clean_queue`。

---

## 6. 流程

### 6.1 标准 enqueue / dequeue

```mermaid
flowchart LR
  subgraph writer
    W1[alloc node] --> W2[msq_enqueue]
  end
  subgraph reader
    R1[ebr_enter] --> R2[read head/tail CAS loop]
    R2 --> R3[ebr_exit]
    R3 --> R4[ref_put old dummy via free_func]
  end
```

Writer 不需 EBR enter（仅链入新 node）；Reader 必须 enter 保护对 **已 dequeue 但尚未 reclaim** 节点的间接访问。

### 6.2 Port 会合 enqueue（check_tail）

`ipc_port_enqueue_wait` 调用 `msq_enqueue_check_tail(&port->thread_queue, &req->ms_queue_node, my_ipc_state, tp_new(NULL, expected_ipc_state), free_ipc_request)`：

- 仅当 tail tag 为 `expected_ipc_state`（EMPTY 或同侧 SEND/RECV）才链接；
- 成功后 tail tag 更新为 `my_ipc_state`；
- 失败 `-E_REND_AGAIN` → 调用方重试（并发侧可能已 match）。

### 6.3 try_match（check_head）

`ipc_port_try_match` → `msq_dequeue_check_head(..., MSQ_CHECK_FIELD_APPEND, tp_new(NULL, target_ipc_state), NULL)`：

- 校验 head 后首个真实节点的 append tag 为 **对侧** 状态；
- 不匹配则 continue  dequeue  stale request。

### 6.4 msq_clean_queue（teardown）

`thread_release_owned_resources` 用 `msq_clean_queue(..., free_message_ref)` 排空 thread 队列。注释强调：**不要** 对 dummy 二次 `ref_put`；空队列 dequeue 路径已处理 dummy refcount。

---

## 7. 公开 API

MSQ 为 **header inline API**，无独立 `.c`。EBR 公开 API 见 `ebr.h`（EBR 篇 §7）。

IPC 层不暴露 MSQ；扩展阅读 `ms_queue.h` 中各 `msq_*` 参数：`free_func`、`refcount_is_zero`、check 字段枚举 `MSQ_CHECK_FIELD_APPEND`。

---

## 8. 多架构

MSQ/EBR 纯 C + atomics，**arch 无关**。`tagged_ptr` 假设指针低位对齐留出 tag 位（内核 heap 对齐满足）。

---

## 9. 测试

- SMP IPC 测例对 MSQ + EBR 施加并发压力。
- `ebr_dump_stats` — 调试 retire/overflow（非正式测例）。

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

与当前源码一致，尚未复测。

---

## 10. 限制与后续

- **MPMC 假设** — 依赖 MS 算法与正确 tag；错误 `free_func` 或缺 EBR enter 会导致 rare crash。
- **EBR_RETIRE_SLOTS=512** — 极高 churn 可能 overflow leak（见 EBR 篇）。
- **append_info_bits 上限 15** — tag 空间受限；port 仅用 2 bit。
- **无 hazard pointer 备选** — 全库统一 EBR；其他子系统复用须遵守 enter/exit 纪律。

更形式化正确性论证不在 v0.1 文档范围；若替换队列实现须更新 IPC 与 EBR 篇。

---

## 11. 变更记录

| 日期 | 摘要 |
|------|------|
| 2026-08-27 | 初稿：MSQ、tagged ptr、EBR 与 IPC 结合 |
