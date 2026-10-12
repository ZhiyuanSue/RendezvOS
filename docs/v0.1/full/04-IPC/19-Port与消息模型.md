# Port 与消息模型

v0.1 · 2026-10-02

本篇覆盖：`kernel/ipc/port.c`、`kernel/ipc/message.c`、`include/rendezvos/ipc/port.h`、`include/rendezvos/ipc/message.h`、`kernel/registry/name_index.c`（经 `Port_Table` 使用的部分）。

**建议先读** `18-支持IPC的无锁MSQ队列实现.md` **§1**（整个无锁 IPC 的动机 §1.1–1.2；驱动 MSQ 改成现状的约束在 §1.3–1.8），再读本篇。收发状态机与 push/pull 见 `20`；kmsg 见 `21`；钩子见 `22`；MSQ/EBR 算法见 `18` §2 起的章节；`name_index` 通用约定见 `10-基础设施/41-名称索引注册表.md`。

---

## 1. 概述

在`18`里面我们提供了无锁队列的算法，但是我们只是提供了一些基本的无锁队列的原语，在此基础上如何实现IPC是另一个需要考虑的问题。本篇主要描述我们用于IPC对端发现的中介——`Port`。

### 1.1 对端发现和消息发送的解耦

在实现临界区线程化的转换之后之后，多核并发的问题关键变成「如何将请求转发给对应的等在那儿的单队列server」（`18` §1.1–1.2）。

第一个关键问题就是对端发现。

我们如何才能正确的找到目标server线程呢？同时，假设有多个同类线程都可以处理该消息，我应该发送给谁？就比如我一个网卡有多个interface，都可以发送网络包，而且都能发到，那实际上就是两个单执行流server的线程，我应该发给谁让他去发出去呢？

吸收 endpoint（端点，IPC 中代表可被寻址的通信端）一类设计：把消息交给另一个线程就两步——**先找到对方，再把载荷递过去**。core 把这两步拆成两层：

1. **Port 会合层** — port 上挂的是**在等的线程请求**（`Ipc_Request_t` → `Thread_Base`），不是消息本体。进行消息发送对端的一个配对会和。
2. **消息发送层** — 每个线程具有自己的 `send_msg_queue` / `recv_msg_queue` ，在这上面挂着的上才是 `Message_t`。接通之后由 `ipc_transfer_message` 做投递（ `Message_t` 相关可以看 `18` §1.5）。

两层模型示意（Port 会合层单状态队列：同一时刻 `Port` 的 `thread_queue` 里不会同时具有 SEND 与 RECV 两种类型的线程发送请求）：

```text
   ┌──────────── Port 会合层 ─────────────┐    ┌── Per-thread 消息层 ──┐
   │  port.thread_queue (单状态 MSQ)      │    │  Thread A            │
   │  例：全是 SEND waiter                │    │  send_pending_msg    │
   │  ┌────┬────┬────┐                   │    │  send_msg_queue      │
   │  │ReqS│ReqS│ReqS│  tag=SEND         │    │  recv_msg_queue      │
   │  └────┴────┴────┘                   │    │  recv_pending_cnt    │
   │  或全 RECV / 或仅 dummy（EMPTY）      │    └──────────────────────┘
   └─────────────────────────────────────┘
```

没有 `send_msg(port, msg)`这种模式的接口：消息先 `enqueue` 进本线程队列，再 `send_msg` 去 port 上配对并发送。一次发送的时序见 `20` §1.1。

### 1.2 单状态队列如何用于对端发现

对端发现的直观做法是 Port 里各挂一条 sender 队列与 receiver 队列。但「看对端的队列上有没有人，然后取出对端队列的头节点或把自己加入本端的等待队列」这个操作，会跨两个队列，**难以做成无锁的原子操作**。典型事故：发送方看 recv 空，准备把自己加入队列并阻塞；接收方同时看 send 空，也把自己加入队列并阻塞，尽管实际上发送队列和接收队列均有一个线程，但是双方都永远阻塞。

单队列则更为简单：**队列中要么全是 sender 线程在等，要么全是 recv 线程在等，要么为空**；如果有另一种类型的线程，那么一定会两两匹配，最后剩下的仍然是同一种类型。而这可以依赖 `18` 中带状态的加入队列和弹出队列。

用于塞入队列中的是 **`Ipc_Request_t` 而非描述线程的 TCB 本体**，之所以这么实现，是因为里存在一个问题就是，假出队带来的自环问题，如果塞进去的是线程的 TCB ，那么节点 enqueue 后又被当 dummy 出队，再次 enqueue 的时候，就相当于把在头部的 dummy 指向自己形成环。

### 1.3 假出队如何影响 Msg 

MSQ 假出队以及如何影响`Msg_Data_t`可以参考`18` §1.4–1.5，具体来说，逻辑上「弹出」的节点仍可能当新 dummy 挂在队上，**不能**整段迁移到对方 recv 队列。因此：

- **`Msg_Data_t`** — 拥有载荷的指针与独立的 refcount，是一个消息数据的真正描述者。
- **`Message_t`** — 挂在 MSQ 上的节点，包括队列节点，以及指向 `Msg_Data_t`的指针。transfer 时给接收方生成一个 `Message_t`并拷贝过去，从而共享并增加对应的 `Msg_Data_t` 的引用计数。

字段细节见 §4.2；推拉谁跑 transfer、pending 等见 `20`。

---

## 2. 目标与边界

**提供：** 创建 / 注册 / 按名查找 / 关闭 port；Port 中的队列会和与 `Message_t` / `Msg_Data_t`的拆分，以及 refcount / EBR 的生命周期等内容。

**不描述：** 收发状态机与 system 投递（见`20`）；MSQ相关算法原语（见`18`）；RPC、reply 命名、具体 server 协议（兼容层）；capability 命名空间（可用钩子 `ops_allow` 由兼容层添加），见`22`；不做形式化验证声明。

---

## 3. 分层与调用方

典型 server：`create_message_port`创建port → `register_port`注册port → 循环 `recv_msg`尝试接收消息，接收成功则接收队列中有一条消息，否则阻塞住 → `dequeue_recv_msg`从接收队列中弹出一条消息并获得一个引用 → 用完消息之后`ref_put`该消息。

客户端：`thread_lookup_port`查找目标所在的port并获得引用 → `enqueue_msg_for_send`把消息放入发送队列 → `send_msg`尝试如果有接收方就发送，如果没有接收方就阻塞住自己 → `ref_put(port)`放弃对port的引用。

除了正常发送消息的唤醒之外，已在 `thread_queue` 上的等待者也可以由 `port_clean_thread_queue` 在清理队列的时候唤醒，但 **send / recv 两侧的通知方式不同**——因为醒来回到的 API 形态不同（因果见 §6.2；行为细节亦见 `20`）：

| 等待侧的状态（指示是发送者还是接收者） | 关闭 port 时的行为 | 阻塞 API 醒后 |
|--------|--------------|---------------|
| `block_on_send` | 只把自己的线程flag OR 一下`THREAD_FLAG_IPC_PORT_CLOSED` 并将线程状态置于ready；无 kmsg 发送（`send_msg` 不看 recv 队列） | `send_msg` 中被唤醒之后，看到flag被置位，则清理掉想发出去但是失败了的消息，并返回 `-E_REND_PORT_CLOSED`给上层 |
| `block_on_receive` | 先尝试使用 `ipc_system_deliver_to` 投递 `KMSG_OP_SYSTEM_PORT_CLOSED` 并修改线程状态为ready；仅当投递失败（recv_pending_cnt == 0）才同样 OR `THREAD_FLAG_IPC_PORT_CLOSED` flag | 有 kmsg → `REND_SUCCESS`，dequeue 看 opcode是否为 `KMSG_OP_SYSTEM_PORT_CLOSED` ；有 flag 则返回`-E_REND_PORT_CLOSED` |

---

## 4. 数据结构与不变量

### 4.1 `Message_Port_t`

- `thread_queue` — 会合 MSQ；dummy 在创建时分配。
- `refcount` / `name` / `service_id`（名 FNV-1a 派生，供 kmsg 快检）/ `ops_life` / `ops_count` / `append_hooks`。
- 队列状态在 **tail 的 tagged ptr**：`EMPTY` / `SEND` / `RECV`。

### 4.2 `Msg_Data_t` 与 `Message_t`

拆分动机见 `18` §1.4–1.5 与本篇 §1.3。这里只钉对象字段：

- **`Msg_Data_t`** — 载荷指针 / 长度 / 类型、`free_data`、独立 refcount；与具体数据内容一一对应（载荷本身往往不能再嵌 refcount 头）。
- **`Message_t`** — `ms_queue_node_t` + 指向 `Msg_Data_t` 的指针（及可选字段）；是队列节点，不是载荷。transfer：接收方**再 new 一条** `Message_t`，共享同一份 `Msg_Data_t`。

谁跑 transfer、pending 兜底见 `20` §6.4；构造 / 释放 API 见 §7.3。

### 4.3 `Port_Table` / `global_port_table`

`name_index` 回调：`port_get_name` / hold / drop / on_register / on_unregister。查找在 name_index 锁下；MCS `me` 用本核槽。

`global_port_table` 是开机阶段由 `global_port_init` 创建的 `Port_Table` 实例，内含一个 `name_index_t` 字符串索引；`service_id` 由端口名经 FNV-1a（Fowler–Noll–Vo 哈希算法的 1a 变体）哈希派生，用作 kmsg 快检（不参与路由，路由仍按名）。结构示意：

```text
   global_port_table  ──►  Port_Table
                              │  name_index_t  ┌─ "vfs_listen"  ─► Message_Port_t { life, refcount,
                              │  (字符串→port)  │                     thread_queue(MSQ),
                              │                ├─ "proc_c0"     ─►  service_id, hooks, ... }
                              │                └─ "timer_c0"   ─►  ...
                              ▼
                          register_port()  : ACTIVE → REGISTERED，表持一 ref
                          unregister_port(): 摘名 → CLOSING → 等 ops_count==0 → 清队 → CLOSED → put 表 ref
                          port_table_lookup(name): 命中 bump ref → ops_allow(LOOKUP)
```

线程侧另有 LRU（Least Recently Used，最近最少使用）`port_cache`（`thread_lookup_port` 先查本线程缓存，未命中再查全局表并写回 token），但 token resolve 仍每次走 LOOKUP 门（见 `22`）。

### 4.4 线程侧

`send_msg_queue`、`recv_msg_queue`、`send_pending_msg`（系统 / IRQ 单槽）、`recv_pending_cnt`、`port_ptr`、`port_cache`（lookup LRU）。**消息不进 port 的 thread_queue。**

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

创建 →（可选 hooks）→ `register_port`（表 get + REGISTERED）→ 使用 → `unregister_port`：

1. 表锁内摘名 → life **REGISTERED→CLOSING**（此后 `port_ops_begin` 失败）
2. 解锁；自旋 `schedule` 直到 `ops_count==0`
3. **`port_clean_thread_queue`**（§6.2）
4. life **CLOSING→CLOSED**；`ref_put` 表持有的那一 ref

### 6.2 关闭 port 时的清队唤醒（`port_clean_thread_queue`）— **send / recv 通知方式不同**

循环 `msq_dequeue(thread_queue)`，按 waiter 当时 status 分流（非 `block_on_*` 的节点只释放 request）：

- **Send 等待者**：CAS 清 `port_ptr` → **恒** OR `THREAD_FLAG_IPC_PORT_CLOSED` → `block_on_send`→`ready`。不构造消息。
- **Recv 等待者**：CAS 清 `port_ptr` → `kmsg_create(service_id, PORT_CLOSED, "q", 0)` → `ipc_system_deliver_to(thread, msg, false)`（**不过** `port_ops`）→ `block_on_receive`→`ready` → 若此时 `recv_pending_cnt==0` 才 OR flag（投递成功则 pending≥1，**不**置 flag）。

#### 为何不对称（因果，不是审美）

send / recv 等待者醒来后所处的 API **形态不同**，所以通知通道只能不同：

| | Send 等待者 | Recv 等待者 |
|--|-------------|-------------|
| 醒在哪 | `send_msg` | `recv_msg` |
| 正常成功意味着 | 对手已 transfer；本侧 send 队列空了 | 本侧 recv 队列上有一条可 `dequeue` 的消息 |
| 关闭时该怎么说「完了」 | **errno**：丢弃 send 队列孤儿 → `-E_REND_PORT_CLOSED` | **优先仍是一条消息**：`KMSG_OP_SYSTEM_PORT_CLOSED`（与 TIMER_EXPIRE 一类，server 循环按 opcode 处理） |
| 为何不用对方路径的手段 | 往本线程 recv 队列投 kmsg 也没用——`send_msg` **从不** dequeue recv | 仅置 flag → 永远 `-E_REND_PORT_CLOSED`、无消息可 dequeue；能投递时反而破坏「事件走消息通道」的习惯。flag 只做 **投递失败兜底**（OOM / `deliver_to` 失败 → `pending==0`），避免 SUCCESS 空队列阻塞不返回，也避免「已投上 kmsg 又 OR flag」把消息滞留在队列里却走了 errno |

**不是**「无锁下 flag 来不及置位所以要发消息」。若只求「别永远阻塞 / 返回已关闭」，recv 也可以像 send 一样 **先 OR flag 再 ready**（send 路径已经证明够用）。发 `PORT_CLOSED` kmsg 的意义是：让 `recv_msg` 走 **SUCCESS + dequeue**，把「port 关了」放入与其它系统事件同一条 opcode 通道；flag 是 deliver 失败时的备用 errno，不是并发补丁。

send / recv 等待者都变成 `ready`：unregister 必须在此清队（不能拖到 last-ref），否则 `ops` 门外的阻塞者会永远阻塞——历史问题见 Pattern Log「unregister must wake waiters」。

调用方约定（`ipc.c`）：`send_msg` 见 flag → orphan drop + `-E_REND_PORT_CLOSED`；`recv_msg` 见 flag → 同 errno，见无 flag → `REND_SUCCESS`（须 dequeue；关闭 port 的路径上常为 `KMSG_OP_SYSTEM_PORT_CLOSED`）。Doxygen：`port.h` `unregister_port`、`thread.h` `THREAD_FLAG_IPC_PORT_CLOSED`、`ipc.h` `recv_msg`；源码注释：`port.c` `port_clean_thread_queue`。

### 6.3 会合成功后的 transfer（摘要）

配对成功后，**没有在 port 上阻塞的那一侧**跑 `ipc_transfer_message`（推 / 拉平衡，见收发篇）：从 sender 的 send 队列取出 `Message_t`，给 receiver 的 recv 队列**再 new 一条** `Message_t`（共享 `Msg_Data_t`）。成功路径上可把对方从 `block_on_*` 置为 ready。

### 6.4 查找

`thread_lookup_port`：先线程 LRU，未命中再全局表并缓存 token。

---

## 7. 公开 API

本篇涉及的接口分布在：`port.h`（port 对象 / `Port_Table` / `global_port_init`）与 `message.h`（`Msg_Data_t` / `Message_t`）。说明以头文件 Doxygen 为准（已与 `.c` 核对）。

**本篇不涉及：** `send_msg` / `recv_msg` / `ipc_transfer_message` → `20`；kmsg → `21`；`ops_allow` / hooks 深挖 → `22`；MSQ/EBR 算法 → `18` / `17`；`name_index` 通用约定 → `41`；`thread_lookup_port` → `13`（本篇只链典型用法）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 开机表 | `cmain`：`init_core_id_system` → **`global_port_init`** → `init_proc` → … → initcall 里 `register_port` |
| Server port | **`create_message_port(name, hooks)`** → **`register_port(table, port)`** → 循环 recv（见 `20`）→ 用完 **`unregister_port`** → `ref_put` 本地引用 |
| 造消息 | `create_message_data`（吞 buffer）→ `create_message_with_msg`（bump data）→ 可选 put 多余 data 引用 → enqueue/send（`20`） |
| 收完 | `dequeue_recv_msg` → 用完 **`ref_put(..., free_message_ref)`**（进 EBR） |

### 7.2 Port 与表（`port.h`）

```c
error_t global_port_init(void);
Message_Port_t *create_message_port(const char *name,
                                    const port_append_hooks_t *hooks);
error_t register_port(struct Port_Table *table, Message_Port_t *port);
error_t unregister_port(struct Port_Table *table, const char *name);
Message_Port_t *port_table_lookup(struct Port_Table *, const char *name);
Message_Port_t *port_table_lookup_with_token(...);
Message_Port_t *port_table_resolve_token(...);
bool port_table_port_is_live(...);
error_t free_message_port_ref(ref_count_t *);
void delete_message_port_structure(Message_Port_t *);
struct Port_Table *port_table_create(void);
void port_table_init(struct Port_Table *);
void delete_port_table_structure(struct Port_Table *);
```

| 接口 | 说明 |
|------|------|
| `global_port_init` | `global_port_table = port_table_create()`；空表。失败 `-E_RENDEZVOS`（`cmain` panic）。不创建 `"kernel_port"`。 |
| `create_message_port` | 名非空且 `< PORT_NAME_LEN_MAX`；life=`ACTIVE`；分配会合 MSQ dummy；`service_id`=名哈希（≠0）。**不**注册。hooks.init 失败回滚。 |
| `register_port` | 表锁内：可选 `ops_allow(REGISTER)` → index 注册（ACTIVE→REGISTERED）→ 表持一 ref。重名且非同一 port → `-E_RENDEZVOS`。 |
| `unregister_port` | 从表中摘除→CLOSING → 等 `ops_count==0` → **`port_clean_thread_queue`（§6.2：两侧通知方式不同）** → CLOSED → put 表 ref。缺名 / 非 REGISTERED 也 SUCCESS。 |
| `port_table_lookup*` | 命中后 bump ref；再经 `ops_allow(LOOKUP)`（拒绝则 put→NULL）。token 变体供线程 LRU。 |
| `free_message_port_ref` | last-ref → `delete_message_port_structure`（先 hooks.fini）。 |

`port_ops_begin` / `end`：send/recv 门禁，深挖见 `22`。典型客户端查找：`thread_lookup_port`（`13`）。

### 7.3 `Message_t` 与 `Msg_Data_t`（`message.h`）

```c
Msg_Data_t *create_message_data(i64 msg_type, u64 data_len, void **data_ptr,
                                error_t (*free_data)(ref_count_t *));
error_t free_msgdata_ref_default(ref_count_t *);
Message_t *create_message_with_msg(Msg_Data_t *msgdata);
Message_t *create_message_structure(void);
error_t fill_message_data(Message_t *msg, Msg_Data_t *msgdata);
error_t free_message_ref(ref_count_t *); /* → ebr_retire_ref */
```

| 接口 | 说明 |
|------|------|
| `create_message_data` | 成功**吞** `*data_ptr`（置 NULL，即取得其所有权）；须 `data_len>0` 且指针有效，否则释放刚建的结构并返回 NULL。 |
| `free_msgdata_ref_default` | free payload buffer + 结构。 |
| `create_message_with_msg` | bump `msgdata`；新 `Message_t` ref=1。调用方若不再持 data 指针须再 put 一次。 |
| `create_message_structure` / `fill_message_data` | 先分配空的 `Message_t` 再挂上 `Msg_Data`（fill 会 bump）。 |
| `free_message_ref` | **EBR** 推迟真释放；real 路径 put `Msg_Data` 再 free 这条 `Message_t`。 |

为何拆 `Msg_Data` / `Message_t`：见 §1.3 / §4.2（动机权威在 `18` §1.5）。

---

## 8. 多架构

对象模型与 ISA 无关；原子与 EBR 实现随 arch，语义相同。

---

## 9. 测试

`modules/test/` 下 port/ipc 相关；`make ARCH=x86_64 config && make all && make run`。

---

## 10. 限制与后续

- 字符串名 ≠ 权能系统；钩子可加门禁。
- 队列性能、广播、批量绕开会合等 **未实现** 项：行为侧见 `20` §10.1；跟踪见 evolution **E2**（调度依赖见 **E4**）。现行两层模型与拆分理由以本篇为准。
- try 路径与 blocking 路径在实现上是分支拷贝，不要想当然合并重构（见 `ipc.c` 注释）。

---

## 11. 变更记录

- 2026-10-12：§4.2 收成字段钉点；假出队因果只回指 `18` §1.4–1.5 / 本篇 §1.3，避免三重复述。
- 2026-10-10：文件名随 `18` 更名为 `18-支持IPC的无锁MSQ队列实现.md`。
- 2026-10-09：开篇与单状态交叉引用对齐 `18` §1 新结构（单状态钉 §1.7）。
- 2026-10-05：§1.1 只留两层对象 ASCII（单状态 waiter）；发送时序图挪到 `20`（本章不是收发状态机）。§10 演进转到 `20` §10.1。
- 2026-10-04：补两层模型 ASCII 图（§1.1）与 `global_port_table` 结构示意图（§4.3）；为「假出队」首现处加名词解释；口语动词「塞/丢掉」改为「投/丢弃」；与 `22` 的 token→LOOKUP 门关系补一句交叉引用。
- 2026-10-05：任务 1/3/5 精读——口语动词与翻译腔清理（睡死→永远阻塞、挂上→加入队列/投递/挂载、挪/搬走→迁移、拿到→取得、放掉→释放、晾在→滞留、挂死→阻塞不返回、摘表→从表中摘除、门闩→门禁、加门→加门禁、踩过坑→调试中遇到问题、勿想当然→不要想当然）；为首现缩写补全称（CAS、FNV-1a、LRU、endpoint）；为「自环」补简短定义；§6.3 标题与内容一致，无重复调整。
- 2026-10-05：最终词句顺畅。
- 2026-10-02：§1 按「先读 18§1 五图 → 本篇对象」重写：两层模型 / 单队列 / Msg 拆分都写成从设计框架往下推；§4.2 因果归无锁篇、本篇只钉字段。
- 2026-10-02：§4.2 链无锁篇 §1.5 图解（`ipc_transfer_message.png`）。
- 2026-10-01：§6.2 补「为何不对称」因果表；明确 **不是** 无锁 flag 竞态补丁（kmsg = recv 成功路径上的可 dequeue 事件；flag = deliver 失败兜底）；与 `port.c` / `port.h` 注释对齐。
- 2026-09-27：中文表述润色（母语习惯）；「关港」改为「关闭 port」；「真源 / 契约」改为「以…为准 / 约定」；「不对称」改为更顺口的「通知方式不同」。
- 2026-09-26：对照 `port_clean_thread_queue` 纠正关闭 port 时唤醒两侧不同（send=flag；recv=优先 PORT_CLOSED kmsg，flag 仅 pending==0）；修正 unregister 序（先 CLOSING 再等 count）；§3/§6.2/§7.2 与 `port.h`/`thread.h`/`ipc.h` Doxygen 对齐。
- 2026-09-26：§7 全文审阅——按 `port.h`/`message.h` Doxygen 补接口说明；写清 create→register→unregister 与 `global_port_init` 编排；纠正旧稿签名省略。
- 2026-09-25：语言整理；标明无硬件寄存器依赖，与任务 / 无锁篇分工不变。
- 2026-08-29：整篇按源码+设计笔记重做——两步模型、单队列 vs 双队列事故、Msg_Data 拆分与 MSQ dummy 真相；动机与无锁篇分工。
- 2026-08-26：v0.1 初稿。
