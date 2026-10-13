# Port 与消息模型

v0.1 · 2026-10-02

本篇覆盖：`kernel/ipc/port.c`、`kernel/ipc/message.c`、`include/rendezvos/ipc/port.h`、`include/rendezvos/ipc/message.h`、`kernel/registry/name_index.c`（经 `Port_Table` 使用的部分）。

**建议先读** `18-支持IPC的无锁MSQ队列实现.md` **§1**（整个无锁 IPC 的动机 §1.1–1.2；驱动 MSQ 改成现状的约束在 §1.3–1.8），再读本篇。收发状态机与 push/pull 见 `20`；kmsg 见 `21`；钩子见 `22`；MSQ/EBR 算法见 `18` §2 起的章节；`name_index` 通用约定见 `10-基础设施/41-名称索引注册表.md`。

---

## 1. 概述

`18` 给出了无锁队列原语；在此之上，还要回答「消息怎么找到对端、怎么交到对方手里」。本篇讲的是对端发现用的中介——**Port**，以及消息在对象上如何拆开存放。

读本篇前，建议已读完 `18` §1（动机与对 MSQ 的改造约束）。收发状态机见 `20`；队列算法细节见 `18` §2 起。

### 1.1 对端发现和消息投递拆开

按 `18` 的思路，临界区变成「单执行流 server + 无锁把请求递过去」之后，第一个工程问题就是：**我要找的那个 server 线程在哪？**

若有多个同类线程都能处理（例如两张网卡接口各挂一个发送线程），发给谁都可以，但总得有一个**可命名的会合点**，而不是调用方手里直接握着对方 `Thread_Base*`。

借鉴 endpoint（端点：可被寻址的通信端）一类设计，把「交给另一个线程」拆成两步：

1. **先会合**——在某个名字上，找到（或等到）对端线程；
2. **再投递**——把载荷交到对端线程的 `recv_msg_queue`。

core 对应两层：

1. **Port 会合层** — Port 的 `thread_queue` 上挂的是「谁在等」：`Ipc_Request_t` 指向某个 `Thread_Base`，**不是**消息本身。发送方与接收方在这里配对。
2. **每线程消息层** — 每个线程有自己的 `send_msg_queue` / `recv_msg_queue`（都是 MSQ）；配对成功后由 `ipc_transfer_message` 投递（为何还要拆成 `Msg_Data_t` + `Message_t`，见 §1.3 与 `18` §1.5）。

示意（会合层是**单状态**队列：同一时刻要么全是发送等待者，要么全是接收等待者，要么空）：

```text
  ┌─ Port 会合层 ──────────────────┐
  │  port.thread_queue（单状态 MSQ）│
  │  例：全是 SEND 侧 Ipc_Request   │
  │    Req → Req → Req              │
  │    tag = SEND                   │
  │  或全 RECV / 或仅 dummy(EMPTY)  │
  └────────────────────────────────┘
                 │ 配对成功后再投递
                 ▼
  ┌─ 线程上的消息队列 ─────────────┐
  │  send_msg_queue                 │
  │  recv_msg_queue                 │
  │  send_pending_msg 等            │
  └────────────────────────────────┘
```

因此**没有**「`send_msg(port, msg)` 一步完成」的接口：调用方先 `enqueue_msg_for_send` 把 `Message_t` 放进本线程 `send_msg_queue`，再对 port 做 `send_msg` 去配对。一次发送的时序见 `20` §1.1。

### 1.2 为什么会合层用「单状态」一条队列

直观做法是 Port 里各挂一条「发送等待」队列和一条「接收等待」队列。但「看对端有没有人，要么取出对方，要么把自己挂上」要跨两条队列，**很难做成无锁下的原子操作**。典型事故：发送方看见接收队列空，准备挂上自己；接收方同时看见发送队列空，也挂上自己——两边其实都有人，却双双阻塞。

改成**一条**队列更简单：队里要么全是发送等待者，要么全是接收等待者，要么为空。若来了「另一种」角色，必然能两两配对，最后剩下来的仍是同一种。这依赖 `18` 里「带状态」的入队 / 出队。

挂进这条队列的是 **`Ipc_Request_t`**，而不是 `Thread_Base`（TCB）本身。原因：MSQ 有「假出队」——逻辑上弹出的节点还可能留在队里当新的 **dummy**。若把 TCB 直接入队，假出队后再入队，容易让队头 dummy 的 `next` 指回自己，形成环。把「等待」单独做成 `Ipc_Request_t`，就可以避免把 TCB 卷进这个问题。

### 1.3 假出队如何迫使消息拆成 `Msg_Data_t` + `Message_t`

MSQ 假出队的细节见 `18` §1.4–1.5。要点是：逻辑上「弹出」的队列节点仍可能留在队上当 dummy，**不能**整段搬到对端 `recv_msg_queue`。所以拆成两个类型：

- **`Msg_Data_t`** — 拥有载荷指针、长度、`free_data` 和自己的 `refcount`；
- **`Message_t`** — 带 `ms_queue_node` 的队列节点，`data` 指向一份 `Msg_Data_t`。

`ipc_transfer_message` 时：给接收方 **new 一条 `Message_t`**，两边的 `Message_t.data` 指向同一份 `Msg_Data_t`（对其 `ref_get`）。字段见 §4.2；谁在什么时机跑 transfer，见 `20`。

---

## 2. 目标与边界

**提供：** Port 的创建 / 注册 / 按名查找 / 关闭；会合队列与消息对象（`Message_t` / `Msg_Data_t`）的分工；引用计数及与延迟回收（EBR，见 `17`）的衔接。

**不描述：** 收发状态机与系统异步投递（`20`）；MSQ 算法本身（`18`）；具体 RPC / 服务协议（上层）；权能式命名空间（可用钩子扩展，见 `22`）。

---

## 3. 分层与调用方（先看用法，细节在后文）

典型 **server**：

1. 先 `create_message_port`，再 `register_port`（别人才能按名找到）；
2. 循环 `recv_msg`：有对端则成功，否则阻塞；
3. `dequeue_recv_msg` 从 `recv_msg_queue` 取出 `Message_t`，用完后 `ref_put`。

典型 **client**：

1. `thread_lookup_port` 按名拿到 port（带引用）；
2. `enqueue_msg_for_send` 把 `Message_t` 放进本线程 `send_msg_queue`；
3. `send_msg`：有接收方则投递，否则阻塞；
4. 用完 port 后 `ref_put`。

关闭 port 时，仍堵在 `thread_queue` 上的等待者必须被叫醒，否则会永远阻塞。发送侧与接收侧**通知方式不同**（因为醒回来时看到的 API 成功形态不同）——完整因果与步骤见 §6.2；下面只记结论：

| 谁在等 | 关闭时怎么通知 | 阻塞 API 醒后大致结果 |
|--------|----------------|----------------------|
| 发送等待（`block_on_send`） | OR `THREAD_FLAG_IPC_PORT_CLOSED`，status 变成 ready | `send_msg` 丢掉未发出的孤儿消息，返回 `-E_REND_PORT_CLOSED` |
| 接收等待（`block_on_receive`） | 优先 `ipc_system_deliver_to` 投 `KMSG_OP_SYSTEM_PORT_CLOSED` 到其 `recv_msg_queue`；失败才 OR 同一 flag | 有 kmsg 则 `REND_SUCCESS` 再 dequeue 认 opcode；仅有 flag 则 `-E_REND_PORT_CLOSED` |

（kmsg 格式见 `21`；细节见 §6.2。）
---

## 4. 数据结构：对象分工与字段含义

定义以头文件为准（`port.h` / `message.h` / `ipc.h` / `thread.h`）。

阅读顺序建议：先看 §4.0 角色图，再按 Port / 消息 / 等待请求 / 名表 / 线程字段往下读。每个小节都是「先讲为什么有这个对象，再贴结构体与字段表」。公开 API 如何创建、查找、销毁它们，见 §7。

### 4.0 对象总览（先看角色）

§1.1 的两层，落到代码上就是下面几类对象：

```text
  Port_Table（global_port_table）
  │  name → Message_Port_t
  │  lookup
  ▼
  Message_Port_t
  │  thread_queue：Ipc_Request_t → Thread_Base
  │  配对成功
  ▼
  Thread A（sender）
  │  send_msg_queue / send_pending_msg：Message_t
  │  ipc_transfer_message（共享 Msg_Data_t）
  ▼
  Thread B（receiver）
  │  recv_msg_queue：新的 Message_t
  │  Message_t.data 两边指向同一份
  ▼
  Msg_Data_t（payload + refcount）
```

| 对象 | 一句话职责 |
|------|-----------|
| `Message_Port_t` | 命名会合点；`thread_queue` 上挂发送/接收等待者 |
| `Ipc_Request_t` | 挂进 `thread_queue` 的等待节点，指向某个 `Thread_Base` |
| `Message_t` | 挂进线程 `send_msg_queue` / `recv_msg_queue` 的 MSQ 节点，`data` 指向 `Msg_Data_t` |
| `Msg_Data_t` | 载荷本体（指针、长度、`free_data`、refcount） |
| `Port_Table` | 全局「名字到 port」索引（`name_index_t`） |
| 线程 IPC 字段 | `send_msg_queue` / `recv_msg_queue` / `send_pending_msg` / `port_ptr` / `port_cache` 等 |

**消息从不挂进 port 的 `thread_queue`**——那条队列只挂「等人配对」的请求；载荷只在线程侧队列（或 `send_pending_msg` 单槽）上流动。

---

### 4.1 `Message_Port_t`：为什么要有 Port

Port 回答 §1.1 的问题：没有它，发送方不知道该把消息交给哪个线程的 `recv_msg_queue`；有了它，双方在**同一个名字**上会合。

Port 上有两套容易混的「状态」，先分开：

1. **会合队列状态**（EMPTY / SEND / RECV）——记在队列尾指针的低几位标记里（带标记指针，见 `18`），表示「现在队里全是谁在等」。
2. **生命周期 `ops_life`**（ACTIVE / REGISTERED / CLOSING / CLOSED）——port 还能不能被查找、能不能进入收发临界区；和上面的 SEND/RECV **不是**同一套枚举。

相关常量：
```c
/* thread_queue 的 tagged 指针低位：当前队列里全是谁在等 */
#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0   /* 空（只有 dummy） */
#define IPC_PORT_STATE_SEND  1   /* 全是发送等待者 */
#define IPC_PORT_STATE_RECV  2   /* 全是接收等待者 */

/* ops_life：port 还能不能被 send/recv 进入 */
#define PORT_OPS_LIFE_CLOSED     0
#define PORT_OPS_LIFE_ACTIVE     1  /* 已 create，尚未 register */
#define PORT_OPS_LIFE_REGISTERED 2  /* 已在名表里，可 begin */
#define PORT_OPS_LIFE_CLOSING    3  /* 正在 unregister */
```

结构体（`port.h`）：

```c
typedef struct port_append_hooks {
        size_t append_info_len;      /* 紧跟结构体后的额外字节；0 = 无 */
        port_append_init_t init;     /* create 时可选初始化 append 区 */
        port_append_fini_t fini;     /* 真正释放前可选清理 */
        port_ops_allow_t ops_allow;  /* 可选准入：LOOKUP/SEND/RECV/REGISTER */
} port_append_hooks_t;

struct Msg_Port {
        ms_queue_t thread_queue;
        ref_count_t refcount;
        struct Port_Table *table;
        char name[PORT_NAME_LEN_MAX];
        u16 service_id;
        atomic64_t ops_life;
        atomic64_t ops_count;
        const struct port_append_hooks *append_hooks;
        char append_port_info[]; /* flexible array：长度 = append_info_len */
};
typedef struct Msg_Port Message_Port_t;
```

**每个字段为什么存在：**

| 字段 | 解决什么问题 |
|------|-------------|
| `thread_queue` | §1.2 的单状态会合队列。只挂等待请求，不挂消息。当前是 SEND 还是 RECV，见上面的队列状态标记（`ipc_get_queue_state`）。 |
| `refcount` | 名表和各次 lookup 都可能同时握着同一 port；最后一次 `ref_put` 才释放内存。 |
| `table` | 已注册时指向所属名表；未注册为 NULL。关闭 / 销毁时知道自己挂在哪。 |
| `name` | 全局发现用的字符串键（含结尾 `\0`，最长见 `PORT_NAME_LEN_MAX`）。查找与路由都以名为准。 |
| `service_id` | 从端口名算出的 16 位短整数（非 0）。**不用于找 port**。系统投递的消息头可带上它，方便接收方快速判断「是不是给我的」（见 `21`）。 |
| `ops_life` | 上面说的生命周期。未注册或正在关闭时，进入收发临界区的 `port_ops_begin` 必须失败。注意：还有另一套 `PORT_OPS_LOOKUP/SEND/…` 表示「做哪类操作时的准入检查」，不要和 life 混读（见 `22`）。 |
| `ops_count` | 当前有多少条路径已经 `begin` 尚未 `end`。关闭 port 时必须等它归零再清会合队列，否则会和临界区打架。它**不**禁止两个 send 同时进行。 |
| `append_hooks` | 可选：挂 `init`/`fini`/`ops_allow`，并声明 `append_info_len`。`NULL` = 无 append 区、准入一律放行。见 `22`。 |
| `append_port_info[]` | C flexible array：分配大小 = `sizeof(Msg_Port) + append_info_len`。core 不解释这段内容。 |

**`ops_life` 怎么走：**

```text
  create_message_port
        │
        ▼
     ACTIVE
        │  register_port
        ▼
     REGISTERED
        │  unregister 摘名
        ▼
     CLOSING
        │  等 ops_count == 0
        │  清 thread_queue
        ▼
     CLOSED
        │  表放下那一 ref
        ▼
     （refcount 归零后释放）
```

- `ACTIVE`：已创建，但还没进名表，别人按名找不到；也不能进入收发临界区。
- `REGISTERED`：已在名表里，可以查找和收发。
- `CLOSING`：已从名表摘掉，新的收发进不去；等正在进行的临界区结束后再清等待者。
- `CLOSED`：会合队列已清空；之后只靠引用计数把对象收回。

---

### 4.2 `Msg_Data_t` 与 `Message_t`：为什么拆成两个

动机已在 §1.3 讲过：假出队后不能把队列节点整段挪到对端 `recv_msg_queue`，只能 **new 一条 `Message_t`、共享同一份 `Msg_Data_t`**；载荷 buffer 上往往也不好再嵌一套 `ms_queue_node` 的 refcount。对应两个类型：

```c
struct MsgData {
        ref_count_t refcount;
        i64 msg_type;                    /* 调用方自定义类型标签 */
        u64 data_len;                    /* 载荷字节数 */
        void *data;                      /* 载荷指针 */
        error_t (*free_data)(ref_count_t *); /* 末次 put 时怎么释放 */
};
typedef struct MsgData Msg_Data_t;

struct Msg {
        ms_queue_node_t ms_queue_node;   /* 挂进 send/recv MSQ */
        Msg_Data_t *data;                /* 共享的 Msg_Data_t */
        void *receiver;                  /* 诊断/扩展用；正常路径可空 */
};
typedef struct Msg Message_t;
```

| 字段 | 解决什么问题 |
|------|-------------|
| `MsgData.refcount` | 多条 `Message_t`（sender 侧节点、receiver 侧新节点、`send_pending_msg`）可同时指向同一份 `Msg_Data_t`；谁最后 put 谁调 `free_data`。 |
| `msg_type` / `data_len` / `data` | 描述载荷本身。kmsg 会把 opcode 等放进 buffer，类型标签供上层区分。 |
| `free_data` | 不同载荷释放方式不同（默认 `free_msgdata_ref_default`：先 free buffer 再 free 结构）。 |
| `Msg.ms_queue_node` | 让这条 `Message_t` 能进 MSQ；节点自己的 refcount 管的是 **`Message_t` 生命周期**（经 EBR），不是载荷。 |
| `Msg.data` | 指向共享的 `Msg_Data_t`；`ipc_transfer_message` 给接收方 new `Message_t` 时对其 `ref_get`。 |
| `Msg.receiver` | 预留字段；现行普通收发路径不依赖它做正确性。 |

谁在什么时机跑 transfer、pending 兜底见 `20`；构造 / 释放见 §7.3。

---

### 4.3 `Ipc_Request_t`：为什么不把线程直接挂进 port

会合队列需要的是「某个线程在等」这个事实，不必把整颗线程控制块塞进 MSQ。原因见 §1.2（假出队与自环）。

```c
typedef struct {
        ms_queue_node_t ms_queue_node; /* 挂进 port->thread_queue */
        Thread_Base *thread;           /* 正在等的那条线程（持引用） */
        ms_queue_t *queue_ptr;         /* 可选：记下所属队列，便于诊断/回收 */
} Ipc_Request_t;
```

| 字段 | 解决什么问题 |
|------|-------------|
| `ms_queue_node` | `Ipc_Request_t` 自己充当 MSQ 节点；被假出队留下当 dummy 时，也动不到 `Thread_Base` 上的其它链表。 |
| `thread` | 配对成功后知道该给谁 transfer、该唤醒谁。创建时对线程 `ref_get`，request 退役时再 put，避免「队列上还有 request、线程已经拆掉」。 |
| `queue_ptr` | 辅助记录；正常配对不依赖它保证正确性。 |

配对成功后，request 从 `thread_queue` 离开并释放；线程继续活着，去 transfer 或被唤醒。

---

### 4.4 `Port_Table`：为什么要有全局名表

若没有名表，客户端只能直接握着 port 指针，跨模块、跨线程很难发现对端。`Port_Table` 就是 **「名字字符串到 port」** 的索引：开机 `global_port_init` 建一张全局表，server `register_port`，client 按名查找。

```c
struct Port_Table {
        name_index_t by_name; /* 通用「按名索引」原语，见 41 */
};

extern struct Port_Table *global_port_table;
```

表结构本身很薄：rows、哈希桶、可缓存的 `name_index_token_t`（row_index + row_gen）都在通用的 `name_index_t` 里（详见 `41`）。Port 挂上的回调（`get_name` / `hold` / `drop` / `on_register` / `on_unregister`）让 register 时自动把 `ops_life` 打成 `REGISTERED`、并让表多持一份 ref；unregister 则摘名并走 §6.1 的关闭顺序。

```text
  global_port_table
        │
        ▼
  Port_Table.by_name
        │
        ├─ "some_server"
        │       │
        │       ▼
        │   Message_Port_t
        │   (REGISTERED，表持 1 ref)
        │
        └─ "another_port"
                │
                ▼
            Message_Port_t

  表操作（竖着看状态变化）：
    register_port
        ACTIVE → REGISTERED（表 get 一 ref）
    unregister_port
        摘名 → CLOSING → 等 ops_count → 清队 → CLOSED → put
    port_table_lookup
        命中 → bump 调用方 ref → 可选 ops_allow(LOOKUP)
```

线程侧还有 `port_cache`（§4.5），减少反复查全局表；但**每次真正拿到可用指针时，仍要过 `ops_allow(LOOKUP)`**（见 `22`），cache 不能绕过准入。

---

### 4.5 线程侧 IPC 字段：`send_msg_queue` / `recv_msg_queue` 等

Port 只做会合（在 `thread_queue` 上配对）。消息真正进出，靠的是**当前线程**上的 MSQ 与相关字段（`THREAD_COMMON`，`thread.h`）：

```c
ms_queue_t recv_msg_queue;           /* transfer 进来的 Message_t */
ms_queue_t send_msg_queue;           /* enqueue_msg_for_send 放进去的 Message_t */
volatile Message_t *send_pending_msg;/* 单槽：ipc_system_* / transfer 中途 */
atomic64_t recv_pending_cnt;         /* 已到达、尚未 dequeue 的条数感 */
volatile void *port_ptr;             /* 当前阻塞在哪个 Message_Port_t；否则 NULL */
struct thread_port_cache port_cache; /* thread_lookup_port 的本地 cache（≤16） */
```

这两条 `*_msg_queue` **就是 MSQ**（和 port 的 `thread_queue` 同类原语），只是挂的节点类型不同：这里挂 `Message_t`，port 上挂 `Ipc_Request_t`。

| 字段 | 为什么要有 |
|------|-----------|
| `send_msg_queue` | 先 `enqueue_msg_for_send(msg)`，再 `send_msg(port)`。把「准备好一条待发 `Message_t`」和「在 port 上配对」分开；配对成功后 `ipc_transfer_message` 从这里（或 `send_pending_msg`）取走。 |
| `recv_msg_queue` | 配对成功后，接收方得到**新的** `Message_t` 挂在这里。`recv_msg` 返回成功后，调用方必须 `dequeue_recv_msg()` 取出所有权。 |
| `send_pending_msg` | 单槽。普通路径主要走 `send_msg_queue`；`ipc_system_*` 以及 transfer 中途「receiver 正在退出」等情形会把 `Message_t` 停在这里，避免弄丢，也避免和 `enqueue_msg_for_send` 混用。细节见 `20`。 |
| `recv_pending_cnt` | 有消息到达时增加，`dequeue_recv_msg` 成功时减一。关闭 port 清队时，用它判断 PORT_CLOSED kmsg 是否真的进了 `recv_msg_queue`——进了就不必再 OR `THREAD_FLAG_IPC_PORT_CLOSED`（§6.2）。 |
| `port_ptr` | 线程在 port 上 `block_on_send` / `block_on_receive` 时记下所堵的 port。清队时 CAS 清掉，避免醒过来还以为挂在已关的 port 上。 |
| `port_cache` | `thread_lookup_port(name)` 的本地加速：缓存 `(row_index, row_gen)` token。命中则 `resolve_token`，未命中再走全局 lookup。cache **不** pin port（不额外长期 `ref_get`）；unregister 后 gen 对不上即失效。最多 `THREAD_MAX_KNOWN_PORTS`（16），LRU 淘汰。 |

和 Port 字段的边界再强调一次：

```text
  port.thread_queue
      └── 只放 Ipc_Request（「谁在等」）

  thread.send_* / recv_*
      └── 只放 Message_t（「什么消息」）
```

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `port.h` / `port.c` | port 对象、表、清队、`global_port_init` |
| `message.h` / `message.c` | Msg_Data / Message、经 EBR 的释放 |
| `name_index.c` | 字符串索引原语（被 `Port_Table` 使用） |
| `ipc.h` / `ipc.c` | `Ipc_Request_t`、send/recv/transfer（行为细节见 `20`） |
| `thread.h` / `thread.c` | 线程侧收发箱字段、`thread_lookup_port` |

---

## 6. 流程

### 6.1 注册与关闭（动哪些字段）

创建，可选 `hooks.init` 写 `append_port_info`，再 `register_port`，使用一段时间后 `unregister_port`：

1. 表锁内按 `name` 从 `by_name` 摘掉，`ops_life` 从 REGISTERED 变为 CLOSING（此后 `port_ops_begin` 失败）
2. 解锁；自旋 `schedule` 直到 `ops_count==0`（没有半截 send/recv 临界区）
3. **`port_clean_thread_queue`**：掏空 `thread_queue`，唤醒等待者（§6.2）
4. `ops_life` 从 CLOSING 变为 CLOSED；`ref_put` 表持有的那一 ref（可能触发真正释放）

### 6.2 关闭 port 时的清队唤醒（`port_clean_thread_queue`）— **send / recv 通知方式不同**

循环 `msq_dequeue(thread_queue)`，按 waiter 当时 status 分流（非 `block_on_*` 的节点只释放 request）：

- **Send 等待者**：CAS 清该线程的 `port_ptr`，然后**恒** OR `THREAD_FLAG_IPC_PORT_CLOSED`，再把 `block_on_send` 变成 `ready`。不构造消息。
- **Recv 等待者**：CAS 清 `port_ptr`，用 port 的 `service_id` 造 `KMSG_OP_SYSTEM_PORT_CLOSED`，再 `ipc_system_deliver_to(thread, msg, false)`（**不过** `port_ops`），接着把 `block_on_receive` 变成 `ready`；若此时该线程 `recv_pending_cnt==0` 才 OR flag（投递成功则 pending≥1，**不**置 flag）。

#### 为何不对称（因果，不是审美）

send / recv 等待者醒来后所处的 API **形态不同**，所以通知通道只能不同：

| | Send 等待者 | Recv 等待者 |
|--|-------------|-------------|
| 醒在哪 | `send_msg` | `recv_msg` |
| 正常成功意味着 | 对手已 transfer；本侧 send 队列空了 | 本侧 recv 队列上有一条可 `dequeue` 的消息 |
| 关闭时该怎么说「完了」 | **errno**：丢弃 send 队列孤儿，返回 `-E_REND_PORT_CLOSED` | **优先仍是一条消息**：`KMSG_OP_SYSTEM_PORT_CLOSED`（与 TIMER_EXPIRE 一类，server 循环按 opcode 处理） |
| 为何不用对方路径的手段 | 往本线程 recv 队列投 kmsg 也没用——`send_msg` **从不** dequeue recv | 仅置 flag 则会永远得到 `-E_REND_PORT_CLOSED`、无消息可 dequeue；能投递时反而破坏「事件走消息通道」的习惯。flag 只做 **投递失败兜底**（OOM / `deliver_to` 失败时 `pending==0`），避免 SUCCESS 空队列阻塞不返回，也避免「已投上 kmsg 又 OR flag」把消息滞留在队列里却走了 errno |

**不是**「无锁下 flag 来不及置位所以要发消息」。若只求「别永远阻塞 / 返回已关闭」，recv 也可以像 send 一样 **先 OR flag 再 ready**（send 路径已经证明够用）。发 `PORT_CLOSED` kmsg 的意义是：让 `recv_msg` 走 **SUCCESS + dequeue**，把「port 关了」放入与其它系统事件同一条 opcode 通道；flag 是 deliver 失败时的备用 errno，不是并发补丁。

send / recv 等待者都变成 `ready`：unregister 必须在此清队（不能拖到 last-ref），否则 `ops` 门外的阻塞者会永远阻塞——历史问题见 Pattern Log「unregister must wake waiters」。

调用方约定（`ipc.c`）：`send_msg` 见到 flag 则 orphan drop 并返回 `-E_REND_PORT_CLOSED`；`recv_msg` 见到 flag 则同 errno，未见 flag 则 `REND_SUCCESS`（须 dequeue；关闭 port 的路径上常为 `KMSG_OP_SYSTEM_PORT_CLOSED`）。Doxygen：`port.h` `unregister_port`、`thread.h` `THREAD_FLAG_IPC_PORT_CLOSED`、`ipc.h` `recv_msg`；源码注释：`port.c` `port_clean_thread_queue`。

### 6.3 会合成功后的 transfer（摘要）

配对成功后，**没有在 port 上阻塞的那一侧**跑 `ipc_transfer_message`（推 / 拉平衡，见 `20`）：

1. 从 sender 的 `send_msg_queue` 或 `send_pending_msg` 取出一条 `Message_t`（及其 `Msg_Data_t`）。
2. 给 receiver **再分配一条** `Message_t`，`data` 指向同一 `Msg_Data_t`（引用 +1），挂进 `recv_msg_queue`，并增加 `recv_pending_cnt`。
3. 成功路径上可把对方从 `block_on_*` 置为 `ready`。

Port 的 `thread_queue` 在配对完成后不再持有这对请求。

### 6.4 按名查找（动名表与 `port_cache`）

`thread_lookup_port(name)` 大致：

1. 查本线程 `port_cache`：命中则拿缓存的（行下标，代际）去 `port_table_resolve_token`。
2. 未命中或 token 失效：`port_table_lookup_with_token` 走全局 `by_name`，成功则写入 `port_cache`。
3. 无论哪条路径，返回前调用方拿到的是 **已 bump ref** 的 `Message_Port_t*`；用完必须 `ref_put(..., free_message_port_ref)`。

---

## 7. 公开 API：如何操作这些结构

本篇 API 主要在 `port.h` / `message.h`；`send_msg` / `recv_msg` / `ipc_transfer_message` 的状态机见 `20`，这里只说明它们**碰到哪些字段**。说明以头文件 Doxygen 为准（已与 `.c` 核对）。

**本篇不涉及：** 收发状态机细节见 `20`；kmsg 载荷格式见 `21`；`ops_allow` 深挖见 `22`；MSQ/EBR 算法见 `18` / `17`；`name_index` 通用约定见 `41`。

### 7.1 典型编排（从空系统到收完一条消息）

| 场景 | 顺序 | 主要动到的结构 |
|------|------|----------------|
| 开机建表 | `global_port_init` | 分配 `global_port_table`，`by_name` 空索引 |
| Server 公布 port | 先 `create_message_port`，再 `register_port` | 新 `Message_Port_t`（从 ACTIVE 到 REGISTERED）；表多持 1 ref；`name` 可被查找 |
| Client 找到 port | `thread_lookup_port` | 可能写 `port_cache`；返回持 ref 的 port |
| 造并发送 | 依次 `create_message_data`、`create_message_with_msg`、`enqueue_msg_for_send`、`send_msg` | 新 `Msg_Data_t` + `Message_t`；进 `send_msg_queue`；port 上配对或挂 `Ipc_Request` |
| 接收并释放 | 先 `recv_msg`，再 `dequeue_recv_msg`，用完 `ref_put(msg)` | 从 `recv_msg_queue` 取出 `Message_t`；EBR 推迟回收节点；末次 put 释放 `Msg_Data_t` |
| 关闭 | `unregister_port`，各方再 `ref_put(port)` | 摘名、清 `thread_queue`、CLOSED；refs 归零后释放 port |

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
bool port_ops_begin(Message_Port_t *port, enum port_ops_type op_type);
void port_ops_end(Message_Port_t *port);
```

| 接口 | 对数据结构做了什么 |
|------|-------------------|
| `global_port_init` | 调用 `port_table_create()`，结果赋给 `global_port_table`。空表，**不**预注册任何名字。失败则 `-E_RENDEZVOS`（`cmain` 会 panic）。 |
| `create_message_port` | 分配 `Message_Port_t`（含可选 `append_port_info`）；填 `name`；算 `service_id`；`ops_life=ACTIVE`，`ops_count=0`；为 `thread_queue` 准备 MSQ dummy；`refcount=1`。**此时还不在名表里**。`hooks->init` 失败则整颗回滚。 |
| `register_port` | 持表锁：可选先过 `ops_allow(REGISTER)`，再把 port 按 `name` 插入 `by_name`，`ops_life` 从 ACTIVE 变为 REGISTERED，写上 `table` 指针，表对 port `ref_get` 一次。重名且不是同一对象则失败。 |
| `unregister_port` | 按名从 `by_name` 摘掉，进入 CLOSING，等到 `ops_count==0`，清 `thread_queue`（§6.2），进入 CLOSED，再表 `ref_put`。名字本就不在 / 已非 REGISTERED 也返回成功。 |
| `port_table_lookup*` | 在 `by_name` 里按名找到 port，**给调用方再 bump 一 ref**；再跑 `ops_allow(LOOKUP)`，拒绝则 put 掉并返回 NULL。带 token 的变体把（行下标，代际）写给线程缓存用。 |
| `port_table_resolve_token` | 用缓存 token + 名字在锁下验证代际；成功同样 bump ref + LOOKUP 门。 |
| `port_ops_begin` / `end` | begin：要求 `ops_life==REGISTERED` 且（可选）`ops_allow` 通过，然后 `ops_count++`。end：`ops_count--`。阻塞等待前必须先 end，否则 unregister 永远等不到清队。详见 `22`。 |
| `free_message_port_ref` | 引用归零后调用 `delete_message_port_structure`（先 `hooks->fini`，再释放内存）。调用方 lookup/create 拿到的每一 ref 都要配对 put。 |

典型客户端查找封装是 `thread_lookup_port`（实现见 `thread.c`，行为见 §6.4）。

### 7.3 消息：`Msg_Data_t` / `Message_t`（`message.h`）

```c
Msg_Data_t *create_message_data(i64 msg_type, u64 data_len, void **data_ptr,
                                error_t (*free_data)(ref_count_t *));
error_t free_msgdata_ref_default(ref_count_t *);
Message_t *create_message_with_msg(Msg_Data_t *msgdata);
Message_t *create_message_structure(void);
error_t fill_message_data(Message_t *msg, Msg_Data_t *msgdata);
error_t free_message_ref(ref_count_t *); /* 经 ebr_retire_ref */
```

| 接口 | 对数据结构做了什么 |
|------|-------------------|
| `create_message_data` | 分配 `Msg_Data_t`，**接管** `*data_ptr` 指向的 buffer（成功后把 `*data_ptr` 置 NULL，调用方不再 free 这块）。`refcount=1`。`data_len` 必须 >0 且指针有效。 |
| `free_msgdata_ref_default` | 默认 `free_data`：先释放 `data` buffer，再释放 `Msg_Data_t` 本身。 |
| `create_message_with_msg` | 分配一条 `Message_t`，`data` 指向传入的 `Msg_Data_t` 并对其 `ref_get`；`Message_t` 节点 ref=1。若调用方不再单独握着 `Msg_Data_t*`，通常再 put 一次多出来的那份。 |
| `create_message_structure` / `fill_message_data` | 先分配空的 `Message_t` 再挂上 `Msg_Data_t`（fill 会 bump）。适合「先占节点、后填载荷」的路径。 |
| `free_message_ref` | `Message_t` 引用归零时**不立刻**拆节点：交给 EBR 推迟（见 `17`/`18`）。真正回收时 put `Msg_Data_t`，再 free 这条 `Message_t`。 |

拆成 Data / Message 的原因见 §1.3 / §4.2。

### 7.4 与收发 API 的衔接（字段怎么被碰到）

下列接口正文在 `20`，这里只写「动哪块内存」，避免 §4 与行为篇脱节：

| 接口 | 主要写入 / 读出 |
|------|----------------|
| `enqueue_msg_for_send(msg)` | 当前线程 `send_msg_queue` 挂上 `Message_t`（队列持一 ref） |
| `send_msg` / `ipc_try_send_msg` | `port_ops_begin(SEND)`；在 `port->thread_queue` 上配对或挂 `Ipc_Request`；成功则 `ipc_transfer_message`；阻塞时写 `port_ptr`、status=`block_on_send` |
| `recv_msg` / `ipc_try_recv_msg` | `port_ops_begin(RECV)`；对称地配对或挂请求；成功后调用方 `dequeue_recv_msg` |
| `dequeue_recv_msg` | 从 `recv_msg_queue` 弹出一条，`recv_pending_cnt--`，所有权交给调用方 |
| `ipc_transfer_message` | 读 sender 的 send 队列或 `send_pending_msg`；给 receiver new `Message_t` 进 `recv_msg_queue` |
| `create_ipc_request` / `free_ipc_request` | 分配/释放挂在 `thread_queue` 上的等待节点，并持有/放下对 `thread` 的 ref |
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

- 2026-10-13：用语回调：去掉「数据袋 / 信封 / 收发件箱」等自造比喻，直接用 `Msg_Data_t` / `Message_t` / `send_msg_queue` 等；dummy / token / append 区保留英文惯用说法。
- 2026-10-13：§1–§3 润色；§3 关闭通知改为结论表，细节后置；§4 入口补阅读顺序与两套状态辨析。
- 2026-10-13：§1.1 / §4 对象总览 / `ops_life` / 名表等 ASCII 图改竖排，避免横版过宽。
- 2026-10-13：§4 重写为「对象分工 → 每个字段为何存在 → 结构体摘录」；去掉 FAM/FNV-1a 等未定义缩写与兼容层示例名；§4.5 逐字段说明线程收发箱；§7 改为「API 如何操作这些结构」，并加 §7.4 与收发 API 的字段衔接。
- 2026-10-13：§4 曾改回以头文件结构体摘录为主（后由同日上条重写覆盖）。
- 2026-10-12：§4.2 假出队因果只回指 `18` §1.4–1.5 / 本篇 §1.3（后由 2026-10-13 再补结构体正文）。
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
