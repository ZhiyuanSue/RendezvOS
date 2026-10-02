# Port 与消息模型

v0.1 · 2026-09-27

本篇覆盖：`kernel/ipc/port.c`、`kernel/ipc/message.c`、`include/rendezvos/ipc/port.h`、`include/rendezvos/ipc/message.h`、`kernel/registry/name_index.c`（经 `Port_Table` 使用的部分）。

收发状态机与 push/pull 见 `19-阻塞与非阻塞收发.md`；kmsg 见 `20-kmsg与TLV序列化.md`；钩子见 `21-Port钩子与准入门.md`；MSQ/EBR 见 `22-无锁队列与EBR设计.md`；`name_index` 通用约定见 `10-基础设施/41-名称索引注册表.md`。

---

## 1. 概述

把消息交给另一个线程，就两步——**先找到对方，再把东西递过去**。core 把这两步拆成两层，而不是「一个邮箱队列里直接堆消息」：

1. **Port 会合层** — port 上挂的是**在等的线程**（`Ipc_Request_t` → `Thread_Base`），不是消息本体。像总机：先接通通话双方。
2. **Per-thread 消息层** — 每个线程自己的 `send_msg_queue` / `recv_msg_queue` 上才是 `Message_t`。接通之后由 `ipc_transfer_message` 做投递。

**为何不做成「纯消息邮箱」：** 同步阻塞语义（谁在等回复）、与线程状态 / 调度的绑定，都会变扭——还得另建一套「消息 ↔ 阻塞线程」索引。这里吸收 endpoint 一类设计：内核先做**路由与配对**，数据怎么拷由 transfer 路径决定。

**为何只有一条线程队列（不是 sender 队列 + recv 队列）：**
「看对面有没有人 → 取出或把自己挂上」若跨两个队列，**单次 CAS 做不到原子**。典型事故：发送方看 recv 空，准备挂自己；接收方同时看 send 空，也挂自己——两边都睡死。单队列不变量简单：**要么全是 sender 在等，要么全是 recv 在等（或空）**；一对上就能配对，剩下来的仍然同一侧。这才好做无锁扩展（单状态 MSQ，见无锁篇）。

**和「无锁 IPC」主张怎么接：** 混合内核把业务临界区线程化之后，多核同步沉到「怎么把请求正确交给 server 线程」。若 port 仍用大锁，扩展性故事在 IPC 层塌掉——动机全文在无锁篇 §1；本篇只钉对象模型。

本篇本身没有架构相关的硬件寄存器；会合靠软件 MSQ / CAS，调度唤醒走 `schedule`（任务篇）。全局按名找 port：`global_port_table`（内部 `name_index_t`）。

---

## 2. 目标与边界

**提供：** 创建 / 注册 / 按名查找 / 关闭 port；会合队列与消息壳；与 refcount / EBR 的生命周期。

**不做：** RPC、reply 命名、具体 server 协议；capability 命名空间（可用钩子 `ops_allow` 由兼容层加）；形式化验证声明。

---

## 3. 分层与调用方

典型 server：`create_message_port` → `register_port` → 循环 `recv_msg` → `dequeue_recv_msg` → `ref_put`。  
客户端：`thread_lookup_port` → `enqueue_msg_for_send` → `send_msg` → `ref_put(port)`。

须知：lookup / send / recv 拿到的 port 已 bump ref；未注册不能 `port_ops_begin`；`unregister` 后新操作返回 `-E_REND_PORT_CLOSED`。已在 `thread_queue` 上的等待者由 `port_clean_thread_queue` 唤醒，但 **send / recv 两侧的通知方式不同**——因为醒来回到的 API 形态不同（因果见 §6.2；行为细节亦见 `19`）：

| 等待侧 | 关闭 port 时 | 阻塞 API 醒后 |
|--------|--------------|---------------|
| `block_on_send` | **恒** OR `THREAD_FLAG_IPC_PORT_CLOSED` + ready；**无** kmsg（`send_msg` 不看 recv 队列） | `send_msg` → 清 orphan → `-E_REND_PORT_CLOSED` |
| `block_on_receive` | 先尝试 `KMSG_OP_SYSTEM_PORT_CLOSED`（`ipc_system_deliver_to`）+ ready；**仅当**事后 `recv_pending_cnt==0`（投递失败 / 未挂上消息）才 OR flag | 有 kmsg → `REND_SUCCESS`，dequeue 看 opcode；有 flag → `-E_REND_PORT_CLOSED` |

---

## 4. 数据结构与不变量

### 4.1 `Message_Port_t`

- `thread_queue` — 会合 MSQ；dummy 在创建时分配。  
- `refcount` / `name` / `service_id`（名 FNV-1a 派生，供 kmsg 快检）/ `ops_life` / `ops_count` / `append_hooks`。  
- 队列状态在 **tail 的 tagged ptr**：`EMPTY` / `SEND` / `RECV`。

### 4.2 为何 `Msg_Data_t` 与 `Message_t` 拆开

MSQ 的出队**不是**把节点从结构里「拿走」——旧 dummy 出队后，跟在后面的节点变成**新 dummy，还必须留在队列里**。若消息内容嵌在队列节点里，没法把「逻辑上已出队」的那块内存整段挪到对方 recv 队列。

因此：

- **`Msg_Data_t`** — 真正的载荷与 `free_data`；独立 refcount。  
- **`Message_t`** — 只含 `ms_queue_node_t` + 指向 `Msg_Data_t` 的指针（再加可选字段）。transfer 时：**新建**接收方 `Message_t` 壳，**共享 / 转移**对 `Msg_Data_t` 的引用，而不是挪 MSQ 节点本身。

这是 debug 里踩过坑之后定下来的结构，不是审美拆分。

### 4.3 `Port_Table` / `global_port_table`

`name_index` 回调：`port_get_name` / hold / drop / on_register / on_unregister。查找在 name_index 锁下；MCS `me` 用本核槽。

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

循环 `msq_dequeue(thread_queue)`，按 waiter 当时 status 分流（非 `block_on_*` 的节点只放掉 request）：

- **Send 等待者**：CAS 清 `port_ptr` → **恒** OR `THREAD_FLAG_IPC_PORT_CLOSED` → `block_on_send`→`ready`。不构造消息。
- **Recv 等待者**：CAS 清 `port_ptr` → `kmsg_create(service_id, PORT_CLOSED, "q", 0)` → `ipc_system_deliver_to(thread, msg, false)`（**不过** `port_ops`）→ `block_on_receive`→`ready` → 若此时 `recv_pending_cnt==0` 才 OR flag（投递成功则 pending≥1，**不**打 flag）。

#### 为何不对称（因果，不是审美）

两边醒来后回到的 API **形态不同**，所以通知通道只能不同：

| | Send 等待者 | Recv 等待者 |
|--|-------------|-------------|
| 醒在哪 | `send_msg` | `recv_msg` |
| 正常成功意味着 | 对手已 transfer；本侧 send 队列空了 | 本侧 recv 队列上有一条可 `dequeue` 的消息 |
| 关闭时该怎么说「完了」 | **errno**：丢掉 send 队列孤儿 → `-E_REND_PORT_CLOSED` | **优先仍是一条消息**：`KMSG_OP_SYSTEM_PORT_CLOSED`（与 TIMER_EXPIRE 一类，server 循环按 opcode 处理） |
| 为何不用另一边的手段 | 往本线程 recv 塞 kmsg 也没用——`send_msg` **从不** dequeue recv | 仅打 flag → 永远 `-E_REND_PORT_CLOSED`、无消息可看；能投递时反而破坏「事件走消息通道」的习惯。flag 只做 **投递失败兜底**（OOM / `deliver_to` 失败 → `pending==0`），避免 SUCCESS 空队列挂死，也避免「已投上 kmsg 又 OR flag」把消息晾在队列里却走了 errno |

**不是**「无锁下 flag 来不及置位所以要发消息」。若只求「别睡死 / 返回已关闭」，recv 也可以像 send 一样 **先 OR flag 再 ready**（send 路径已经证明够用）。发 `PORT_CLOSED` kmsg 的意义是：让 `recv_msg` 走 **SUCCESS + dequeue**，把「port 关了」塞进与其它系统事件同一条 opcode 通道；flag 是 deliver 失败时的备用 errno，不是并发补丁。

两端都变成 `ready`：unregister 必须在此清队（不能拖到 last-ref），否则 `ops` 门外的阻塞者会永远睡死——历史坑见 Pattern Log「unregister must wake waiters」。

调用方约定（`ipc.c`）：`send_msg` 见 flag → orphan drop + `-E_REND_PORT_CLOSED`；`recv_msg` 见 flag → 同 errno，见无 flag → `REND_SUCCESS`（须 dequeue；关闭 port 的路径上常为 `KMSG_OP_SYSTEM_PORT_CLOSED`）。Doxygen：`port.h` `unregister_port`、`thread.h` `THREAD_FLAG_IPC_PORT_CLOSED`、`ipc.h` `recv_msg`；源码注释：`port.c` `port_clean_thread_queue`。

### 6.3 会合成功后的 transfer（摘要）

配对成功后，**没有在 port 上阻塞的那一侧**跑 `ipc_transfer_message`（推 / 拉平衡，见收发篇）：从 sender 的 send 队列取逻辑消息，在 receiver 的 recv 队列挂上新壳。成功路径上可把对方从 `block_on_*` 打成 ready。

### 6.4 查找

`thread_lookup_port`：先线程 LRU，未命中再全局表并缓存 token。

---

## 7. 公开 API

本篇拥有：`port.h`（port 对象 / `Port_Table` / `global_port_init`）与 `message.h`（`Msg_Data` / `Message_t` 壳）。说明以头文件 Doxygen 为准（已与 `.c` 核对）。

**本篇不拥有：** `send_msg` / `recv_msg` / `ipc_transfer_message` → `19`；kmsg → `20`；`ops_allow` / hooks 深挖 → `21`；MSQ/EBR 算法 → `22` / `17`；`name_index` 通用约定 → `41`；`thread_lookup_port` → `13`（本篇只链典型用法）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 开机表 | `cmain`：`init_core_id_system` → **`global_port_init`** → `init_proc` → … → initcall 里 `register_port` |
| Server port | **`create_message_port(name, hooks)`** → **`register_port(table, port)`** → 循环 recv（见 `19`）→ 用完 **`unregister_port`** → `ref_put` 本地引用 |
| 造消息 | `create_message_data`（吞 buffer）→ `create_message_with_msg`（bump data）→ 可选 put 多余 data 引用 → enqueue/send（`19`） |
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
| `unregister_port` | 摘表→CLOSING → 等 `ops_count==0` → **`port_clean_thread_queue`（§6.2：两侧通知方式不同）** → CLOSED → put 表 ref。缺名 / 非 REGISTERED 也 SUCCESS。 |
| `port_table_lookup*` | 命中后 bump ref；再经 `ops_allow(LOOKUP)`（拒绝则 put→NULL）。token 变体供线程 LRU。 |
| `free_message_port_ref` | last-ref → `delete_message_port_structure`（先 hooks.fini）。 |

`port_ops_begin` / `end`：send/recv 门闩，深挖见 `21`。典型客户端查找：`thread_lookup_port`（`13`）。

### 7.3 消息壳（`message.h`）

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
| `create_message_data` | 成功**吞** `*data_ptr`（置 NULL）；须 `data_len>0` 且指针有效，否则 put 掉刚建的结构并 NULL。 |
| `free_msgdata_ref_default` | free payload buffer + 结构。 |
| `create_message_with_msg` | bump `msgdata`；新 `Message_t` ref=1。调用方若不再持 data 指针须再 put 一次。 |
| `create_message_structure` / `fill_message_data` | 空壳后再挂 data（fill 会 bump）。 |
| `free_message_ref` | **EBR** 推迟真释放；real 路径 put `Msg_Data` 再 free 壳。 |

为何拆 `Msg_Data` / `Message_t`：见 §4.2（MSQ dummy 不能整节点搬走）。

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

- 2026-10-01：§6.2 补「为何不对称」因果表；明确 **不是** 无锁 flag 竞态补丁（kmsg = recv 成功路径上的可 dequeue 事件；flag = deliver 失败兜底）；与 `port.c` / `port.h` 注释对齐。
- 2026-09-27：中文表述润色（母语习惯）；「关港」改为「关闭 port」；「真源 / 契约」改为「以…为准 / 约定」；「不对称」改为更顺口的「通知方式不同」。
- 2026-09-26：对照 `port_clean_thread_queue` 纠正关闭 port 时唤醒两侧不同（send=flag；recv=优先 PORT_CLOSED kmsg，flag 仅 pending==0）；修正 unregister 序（先 CLOSING 再等 count）；§3/§6.2/§7.2 与 `port.h`/`thread.h`/`ipc.h` Doxygen 对齐。
- 2026-09-26：§7 全文审阅——按 `port.h`/`message.h` Doxygen 补接口说明；写清 create→register→unregister 与 `global_port_init` 编排；纠正旧稿签名省略。
- 2026-09-25：语言整理；标明无硬件寄存器依赖，与任务 / 无锁篇分工不变。
- 2026-08-29：整篇按源码+设计笔记重做——两步模型、单队列 vs 双队列事故、Msg_Data 拆分与 MSQ dummy 真相；动机与无锁篇分工。
- 2026-08-26：v0.1 初稿。
