# Port 与消息模型

v0.1 · 2026-08-29

本篇覆盖：`kernel/ipc/port.c`、`kernel/ipc/message.c`、`include/rendezvos/ipc/port.h`、`include/rendezvos/ipc/message.h`、`kernel/registry/name_index.c`（经 `Port_Table` 使用的部分）。

收发状态机与 push/pull 见 `阻塞与非阻塞收发.md`；kmsg 见 `kmsg与TLV序列化.md`；钩子见 `Port钩子与准入门.md`；MSQ/EBR 见 `无锁队列与EBR设计.md`；name_index 通用契约见 `10-基础设施/名称索引注册表.md`。

---

## 1. 概述

把消息交给另一个线程，就两步——**先找到对方，再把东西递过去**（「大象放进冰箱」那两步）。core 把这两步拆成两层，而不是「一个邮箱队列里直接堆消息」：

1. **Port 会合层** — port 上挂的是**在等的线程**（`Ipc_Request_t` → `Thread_Base`），不是消息本体。像总机：先接通通话双方。  
2. **Per-thread 消息层** — 每个线程自己的 `send_msg_queue` / `recv_msg_queue` 上才是 `Message_t`。接通之后由 `ipc_transfer_message` 做投递。

**为何不做成「纯消息邮箱」：** 同步阻塞语义（谁在等回复）、与线程状态/调度的绑定，都会变扭——你还得另建一套「消息 ↔ 阻塞线程」索引。吸收 endpoint 一类设计：内核先做**路由与配对**，数据怎么拷由 transfer 路径决定。

**为何只有一条线程队列（不是 sender 队列 + recv 队列）：**  
「看对面有没有人 → 取出或把自己挂上」若跨两个队列，**单次 CAS 做不到原子**。典型事故：发送方看 recv 空，准备挂自己；接收方同时看 send 空，也挂自己——两边都睡死。单队列不变量简单：**要么全是 sender 在等，要么全是 recv 在等（或空）**；一对上就能配对，剩下来的仍然同一侧。这才好做无锁扩展（单状态 MSQ，见无锁篇）。

**和「无锁 IPC」主张怎么接：** 混合内核把业务临界区线程化之后，多核同步沉到「怎么把请求正确交给 server 线程」。若 port 仍用大锁，扩展性故事在 IPC 层塌掉——动机全文在无锁篇 §1；本篇只钉对象模型。

全局按名找 port：`global_port_table`（内部 `name_index_t`）。

---

## 2. 目标与边界

**提供：** 创建/注册/按名查找/关闭 port；会合队列与消息壳；与 refcount/EBR 的生命周期。

**不做：** RPC、reply 命名、具体 server 协议；capability 命名空间（可用钩子 `ops_allow` 由兼容层加）；形式化验证声明。

---

## 3. 分层与调用方

典型 server：`create_message_port` → `register_port` → 循环 `recv_msg` → `dequeue_recv_msg` → `ref_put`。  
客户端：`thread_lookup_port` → `enqueue_msg_for_send` → `send_msg` → `ref_put(port)`。

须知：lookup/send/recv 拿到的 port 已 bump ref；未注册不能 `port_ops_begin`；`unregister` 后新操作 `-E_REND_PORT_CLOSED`，已等待者由 `port_clean_thread_queue` 唤醒并打 `THREAD_FLAG_IPC_PORT_CLOSED`。

---

## 4. 数据结构与不变量

### 4.1 `Message_Port_t`

- `thread_queue` — 会合 MSQ；dummy 在创建时分配。  
- `refcount` / `name` / `service_id`（名 FNV-1a 派生，供 kmsg 快检）/ `ops_life` / `ops_count` / `append_hooks`。  
- 队列状态在 **tail 的 tagged ptr**：`EMPTY` / `SEND` / `RECV`。

### 4.2 为何 `Msg_Data_t` 与 `Message_t` 拆开

MSQ 的出队**不是**把节点从结构里「拿走」——旧 dummy 出队后，跟在后面的节点变成**新 dummy，还必须留在队列里**。若消息内容嵌在队列节点里，你没法把「逻辑上已出队」的那块内存整段挪到对方 recv 队列。

因此：

- **`Msg_Data_t`** — 真正的载荷与 `free_data`；独立 refcount。  
- **`Message_t`** — 只含 `ms_queue_node_t` + 指向 `Msg_Data_t` 的指针（再加可选字段）。transfer 时：**新建**接收方 `Message_t` 壳，**共享/转移**对 `Msg_Data_t` 的引用，而不是挪 MSQ 节点本身。

这是 debug 里踩过坑之后定下来的结构，不是审美拆分。

### 4.3 `Port_Table` / `global_port_table`

`name_index` 回调：`port_get_name` / hold / drop / on_register / on_unregister。查找在 name_index 锁下；MCS `me` 用本核槽。

### 4.4 线程侧

`send_msg_queue`、`recv_msg_queue`、`send_pending_msg`（系统/IRQ 单槽）、`recv_pending_cnt`、`port_ptr`、`port_cache`（lookup LRU）。**消息不进 port 的 thread_queue。**

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `port.h` / `port.c` | port 对象、表、清队、`global_port_init` |
| `message.h` / `message.c` | Msg_Data / Message、EBR 释放 |
| `name_index.c` | 字符串索引 |
| `ipc.h` | `Ipc_Request_t`；收发在 `ipc.c`（下一篇） |

---

## 6. 流程

### 6.1 注册与关闭

创建 →（可选 hooks）→ `register_port`（表 get + REGISTERED）→ 使用 → `unregister_port`（等 `ops_count==0` → 清队列 → CLOSING/CLOSED）。

### 6.2 会合成功后的 transfer（摘要）

配对成功后，**没有在 port 上阻塞的那一侧**跑 `ipc_transfer_message`（推/拉平衡，见收发篇）：从 sender 的 send 队列取逻辑消息，在 receiver 的 recv 队列挂上新壳。成功路径上可把对方从 `block_on_*` 打成 ready。

### 6.3 查找

`thread_lookup_port`：先线程 LRU，未命中再全局表并缓存 token。

---

## 7. 公开 API

```c
Message_Port_t *create_message_port(const char *name, ...);
error_t register_port(struct Port_Table *, Message_Port_t *);
error_t unregister_port(...);
Message_Port_t *thread_lookup_port(const char *name);
void free_message_port_ref(ref_count_t *);

Msg_Data_t *create_message_data(...);
Message_t *create_message_with_msg(...);
void free_message_ref(ref_count_t *);
```

完整签名以头文件为准。`global_port_init` 在 `cmain` 中、`init_proc` 前调用。

---

## 8. 多架构

对象模型与 ISA 无关；原子与 EBR 实现随 arch，语义相同。

---

## 9. 测试

`modules/test/` 下 port/ipc 相关；`make ARCH=x86_64 config && make all && make run`。本篇未复测。

---

## 10. 限制与后续

- 字符串名 ≠ 权能系统；钩子可加门。  
- 队列性能与扩展属**未实现增强**时见 evolution **E2**；现行两层模型与拆分理由以本篇为准。  
- try 路径与 blocking 路径在实现上是分支拷贝，勿想当然合并重构（见 `ipc.c` 注释）。

---

## 11. 变更记录

- 2026-08-29：整篇按源码+设计笔记重做——两步模型、单队列 vs 双队列事故、Msg_Data 拆分与 MSQ dummy 真相；动机与无锁篇分工。
- 2026-08-26：v0.1 初稿。
