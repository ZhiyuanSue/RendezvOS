# Port 与消息模型

v0.1 · 2026-10-02

本篇覆盖：`kernel/ipc/port.c`、`kernel/ipc/message.c`、`include/rendezvos/ipc/port.h`、`include/rendezvos/ipc/message.h`、`kernel/registry/name_index.c`（经 `Port_Table` 使用的部分）。

**建议先读** `18-支持IPC的无锁MSQ队列实现.md` **§1**（整个无锁 IPC 的动机 §1.1–1.2；驱动 MSQ 改成现状的约束在 §1.3–1.8），再读本篇。收发状态机与 push/pull 见 `20`；kmsg 见 `21`；钩子见 `22`；MSQ/EBR 算法见 `18` §2 起的章节；`name_index` 通用约定见 `10-基础设施/41-名称索引注册表.md`。

---

## 1. 概述

`18` 给出了无锁队列原语；在此之上，还要回答「消息怎么找到对端、怎么交到对方手里」。本篇讲的是对端发现用的中介——**Port**，以及消息本身使用的数据结构如何组织。

读本篇前，建议已读完 `18` §1（动机与对 MSQ 的改造约束）。收发状态机见 `20`；队列算法细节见 `18` §2 起。

### 1.1 对端发现和消息投递拆开

按 `18` 的思路，临界区变成「单执行流 server + 无锁把请求递过去」之后，第一个问题就是：**我要找的那个 server 线程在哪？**

若有多个同类线程都能处理（例如两张网卡接口各挂一个发送线程），发给谁都可以，但总得有一个数据结构来表示这种关系，而不是调用方手里直接握着对方 `Thread_Base*`线程控制块。

我们借鉴 endpoint（端点：可被寻址的通信端）一类设计，把「交给另一个线程」拆成两步：

1. **先会合**——在某个名字上，找到（或等到）对端线程；
2. **再投递**——把载荷交到对端线程的 `recv_msg_queue`等接收位置。

core 对应两层：

1. **Port 会合层** — Port 的 `thread_queue` 上挂的是「谁在等」：在队列里面排队的是 `Ipc_Request_t` ，它指向某个 `Thread_Base`，**而不是**消息本身在排队。发送方与接收方在这里进行配对。
2. **每线程消息层** — 每个线程有自己的 `send_msg_queue` / `recv_msg_queue`（都是 MSQ）作为发送方的队列和接收方的队列；在第一步配对成功后由 `ipc_transfer_message` 进行消息在两个队列之间进行投递（为何还要拆成 `Msg_Data_t` + `Message_t`，见 §1.3 与 `18` §1.5）。

示意（会合层是**单状态**队列：同一时刻要么全是发送等待者，要么全是接收等待者，要么空）：

```text
  ┌─ Port 会合层 ───────────────────┐
  │  port.thread_queue（单状态 MSQ） │
  │  例：全是 SEND 侧 Ipc_Request    │
  │    Req → Req → Req             │
  │    tag = SEND                  │
  │  或全 RECV / 或仅 dummy(EMPTY)  │
  └────────────────────────────────┘
                 │ 配对成功后再投递
                 ▼
  ┌─ 线程上的消息队列 ───────────────┐
  │  send_msg_queue                │
  │  recv_msg_queue                │
  │  send_pending_msg 等用于消息传递 │
  └────────────────────────────────┘
```

因此**没有**「`send_msg(port, msg)` 一步完成」模式的接口：调用方需要先 `enqueue_msg_for_send` 把 `Message_t` 表示的消息放进本线程 `send_msg_queue`，再对 port 做 `send_msg` 去配对。一次发送的时序见 `20` §1.1。接收也是如此，需要先 `recv_msg` 接收消息，接收到消息就表示，在他的`recv_msg_queue`里面存在一条消息（否则就是被阻塞住的），然后`dequeue_recv_msg`从接收队列中弹出一条消息。

### 1.2 为什么会合层用「单状态」一条队列

直观做法是 Port 里各挂一条「发送等待」队列和一条「接收等待」队列。但「看对端有没有人，要么取出对方，要么把自己挂上」要跨两条队列，**很难做成无锁下的原子操作**。典型事故：发送方看见接收队列空，准备挂上自己；接收方同时看见发送队列空，也挂上自己——两边其实都有人，却双双阻塞。

改成**一条**队列更简单：队里要么全是发送等待者，要么全是接收等待者，要么为空。若来了「另一种」角色，必然能两两配对，最后剩下来的仍是同一种。这依赖 `18` 里「带状态」的入队 / 出队。

挂进这条队列的是 **`Ipc_Request_t`**，而不是 `Thread_Base`（TCB）本身。原因：MSQ 有「假出队」——逻辑上弹出的节点还可能留在队里当新的 **dummy**。若把 TCB 直接入队，假出队后再入队，容易让队头 dummy 的 `next` 指回自己，形成环。把「等待」单独做成 `Ipc_Request_t`，就可以避免把 TCB 卷进这个问题。

但是这样需要额外注意一个问题，`Ipc_Request_t`需要一个指针指向对应的 `Thread_Base`（TCB），但是反过来TCB不应该有个指针指向这个request——否则就是循环指针，这不能如此做。然而不能如此做带来了另一个生命周期的问题：如果TCB被释放了，`Ipc_Request_t`仍然挂在Port队列里面，会有野指针的问题。为了解决这个问题，我们也对这个TCB使用了refcount。在其refcount没有归零之前，不释放TCB。

### 1.3 假出队如何迫使消息拆成 `Msg_Data_t` + `Message_t`

MSQ 假出队的细节见 `18` §1.4–1.5。要点是：逻辑上「弹出」的队列节点仍可能留在队上当 dummy，发送方不能直接把消息搬到接收方的 `recv_msg_queue`，同时我们需要减少复制数据带来的开销。所以拆成两个类型：

- **`Msg_Data_t`** — 拥有载荷指针、长度、`free_data` 和自己的 `refcount`，是真正的拥有消息的数据结构；
- **`Message_t`** — 带 `ms_queue_node` 的队列节点，只是在队列中排队的节点封装，`data` 字段指向一份 `Msg_Data_t`。

`ipc_transfer_message` 时：给接收方 **复制一条 `Message_t`**，两边的 `Message_t.data` 指向同一份 `Msg_Data_t`（对其 `ref_get`）。字段见 §4.2；谁在什么时机跑 transfer，见 `20`。

---

## 2. 目标与边界

**提供：** Port 的创建 / 注册 / 按名查找 / 关闭；会合队列与消息对象（`Message_t` / `Msg_Data_t`）的分工；引用计数及与延迟回收（EBR，见 `17`）的衔接。

**不描述：** 收发状态机与系统异步投递（`20`）；MSQ 算法本身（`18`）；具体 RPC / 服务协议（上层）；port 注册表空间与可用 hook 扩展（见 `22`）。

---

## 3. 分层与调用方（先看用法，细节在后文）

兼容层典型 **server**：

1. 先 `create_message_port`，再 `register_port`（别人才能按名找到）；
2. 循环 `recv_msg`：有人给他发消息则成功，否则阻塞；
3. `dequeue_recv_msg` 从 `recv_msg_queue` 取出 `Message_t`，用完后 `ref_put`。

兼容层典型 **client**：

1. `thread_lookup_port` 按名拿到 port（带引用计数）；
2. `enqueue_msg_for_send` 把 `Message_t` 放进本线程 `send_msg_queue`；
3. `send_msg`：有接收方则投递，否则阻塞；
4. 用完 port 后 `ref_put`释放引用计数。

关闭 port 时，仍堵在 `thread_queue` 上的等待者必须被叫醒，否则会永远阻塞。发送侧与接收侧**通知方式不同**（因为醒回来时看到的 API 成功形态不同）。兼容层该怎么编排、失败时该认哪些返回值，见 §6；这里只记结论：

| 谁在等 | 关闭时怎么通知 | 阻塞 API 醒后大致结果 |
|--------|----------------|----------------------|
| 发送等待（`block_on_send`） | 把线程的 flags 置位 `THREAD_FLAG_IPC_PORT_CLOSED`，线程的 status 变成 ready | `send_msg` 丢掉未发出的孤儿消息，返回 `-E_REND_PORT_CLOSED` |
| 接收等待（`block_on_receive`） | 优先 `ipc_system_deliver_to` 投 `KMSG_OP_SYSTEM_PORT_CLOSED` 消息到其 `recv_msg_queue`；失败才置位 flag | 有 kmsg 则 `REND_SUCCESS`，再 dequeue 认 opcode，上层从opcode辨认出来，端口关闭的情况；仅有 flag 则 `-E_REND_PORT_CLOSED` |

---

## 4. 数据结构：对象分工与字段含义

定义以头文件为准（`port.h` / `message.h` / `ipc.h` / `thread.h`）。

阅读顺序建议：先看 §4.0 角色图，再按 Port / 消息 / 等待请求 / port 注册表 / 线程字段往下读。每个小节都是「先讲为什么有这个对象，再贴结构体与字段表」。公开 API 如何创建、查找、销毁它们，见 §7。

### 4.0 对象总览（先看角色）

§1.1 的两层，落到代码上就是下面几类对象：

```text
  Port_Table（global_port_table）
  │  组织起来所有的Port
  ▼
  Message_Port_t
  │  包含了thread_queue，其中排队着Ipc_Request_t，指向Thread_Base
  │  如果配对成功，获取到对端的Thread_Base
  ▼
  Thread A（sender）
  │  send_msg_queue / send_pending_msg，塞入Message_t
  │  ipc_transfer_message
  ▼
  Thread B（receiver）
  │  recv_msg_queue：收到复制后的新的 Message_t
  │  其中Message_t.data和sender指向同一份Msg_Data_t
  ▼
  Msg_Data_t（包括了payload + refcount）
```

| 对象 | 一句话职责 |
|------|-----------|
| `Message_Port_t` | 带名称的发送接收方会合端点；其中`thread_queue` 上挂发送/接收等待者 |
| `Ipc_Request_t` | 挂进 `thread_queue` 的等待节点，指向某个 `Thread_Base` |
| `Message_t` | 挂进线程 `send_msg_queue` / `recv_msg_queue` 的 MSQ 节点，`data` 指向具体的消息载体 `Msg_Data_t` |
| `Msg_Data_t` | 载荷本体（指向具体数据块的指针、长度、`free_data`析构函数指针、refcount） |
| `Port_Table` | 全局 port 注册表（内部用 `name_index_t` 做按名索引） |
| 线程 IPC 字段 | `send_msg_queue` / `recv_msg_queue` / `send_pending_msg` / `port_ptr` / `port_cache` 等 |

---

### 4.1 `Message_Port_t`：为什么要有 Port

Port 回答了 §1.1 的问题：没有它，发送方不知道该把消息交给哪个线程的 `recv_msg_queue`；有了它，双方在**同一个名字**上会合。

Port 上有两套容易混的「状态」，先分开：

1. **会合队列状态**（EMPTY / SEND / RECV）——记在队列尾指针的低几位tag里（tagged prt 见 `18`），表示「现在队里全是谁在等」。
2. **生命周期 `ops_life`**（ACTIVE / REGISTERED / CLOSING / CLOSED）——port 还能不能被查找、能不能进入收发临界区；和上面的 SEND/RECV不是同一套。

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
#define PORT_OPS_LIFE_REGISTERED 2  /* 已在 port 注册表里，可 begin */
#define PORT_OPS_LIFE_CLOSING    3  /* 正在 unregister */
```

结构体（`port.h`）：

```c
typedef struct port_append_hooks {
        size_t append_info_len;      /* 紧跟结构体后的额外字节；0 = 无 */
        port_append_init_t init;     /* create 时对append info的可选初始化 append 区 */
        port_append_fini_t fini;     /* 真正释放前对append info的可选清理 */
        port_ops_allow_t ops_allow;  /* 可选准入：LOOKUP/SEND/RECV/REGISTER，如果存在，还需要进行ops_allow的判定 */
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
| `thread_queue` | §1.2 的单状态会合队列。只挂等待请求，不挂消息。当前是 SEND 还是 RECV，见上面的队列状态标记。 |
| `refcount` | port 注册表和各次 lookup 都可能同时握着 port 的引用计数；最后一次 `ref_put` 才释放Port的内存。 |
| `table` | 已注册时指向所属 port 注册表；未注册为 NULL。作用是关闭 / 销毁时知道自己挂在哪。 |
| `name` | 全局发现用的字符串键（含结尾 `\0`，最长见 `PORT_NAME_LEN_MAX`）。查找与路由都以名为准。 |
| `service_id` | 从端口名算出的 16 位短整数（非 0）。**不用于找 port**。系统投递的消息头可带上它，方便接收方快速判断「是不是给我的」（见 `21`）。 |
| `ops_life` | 上面说的生命周期。如果未注册或正在关闭时（即不在可允许的Port生命周期时），进入收发临界区的 `port_ops_begin` 必须失败。 |
| `ops_count` | 当前有多少调用方已经 `begin` 尚未 `end`。关闭 port 时必须等它归零再清会合队列，否则会出bug。它不禁止两个 send 同时进行（send_msg本身也是无锁的，所以允许多个线程正在操作）。 |
| `append_hooks` | 暴露给兼容层的可选hook：挂 `init`/`fini`/`ops_allow`，并声明 `append_info_len`。`NULL` = 无 append 区、准入一律放行。见 `22`。 |
| `append_port_info[]` | C flexible array：分配大小 = `sizeof(Msg_Port) + append_info_len`。core 不解释这段内容，兼容层自行管理。 |

**`ops_life` 怎么走：**

```text
  create_message_port
        │
        ▼
     ACTIVE
        │  register_port
        ▼
     REGISTERED
        │  unregister
        ▼
     CLOSING
        │  等 ops_count == 0
        │  清 thread_queue
        ▼
     CLOSED
        │  放下 ref
        ▼
     （refcount 归零后释放）
```

- `ACTIVE`：已创建，但还没进 port 注册表，别人按名查找是找不到的；更不能收发。
- `REGISTERED`：已在 port 注册表里，可以查找和收发。
- `CLOSING`：已从 port 注册表摘掉，新的收发进不去；等正在进行的临界区结束后再清等待者。
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
| `Msg.ms_queue_node` | 让这条 `Message_t` 能进 MSQ；`Msg.ms_queue_node` 里面的 refcount 管的是 `Message_t` 生命周期，不是载荷。 |
| `Msg.data` | 指向共享的 `Msg_Data_t`；`ipc_transfer_message` 给接收方 new `Message_t` 时对其 `ref_get`。 |
| `Msg.receiver` | 预留字段；现行普通收发路径不依赖它做正确性。 |

谁在什么时机跑 transfer、pending 指针见 `20`；构造 / 释放见 §7.3。

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

配对成功后，调用方拿到这份 `Ipc_Request_t` 去 transfer 之后唤醒线程。

按 MSQ 假出队的实现，这个 `Ipc_Request_t` 节点往往还留在队头当 dummy，要等下一次出队才放下队列那份引用，不是配对当下就从队列上消失。

---

### 4.4 `Port_Table`：为什么要有全局 port 注册表

若没有 port 注册表，客户端只能直接握着 port 指针，跨模块、跨线程很难发现对端。`Port_Table`（通常即 `global_port_table`）就是全局 port 注册表：开机 `global_port_init` 建好空表，server `register_port`，client 按名查找。

```c
struct Port_Table {
        name_index_t by_name; /* 通用「按名索引」原语，见 41 */
};

extern struct Port_Table *global_port_table;
```

表结构本身很薄：rows、哈希桶、可缓存的 `name_index_token_t`（row_index + row_gen）都在通用的 `name_index_t` 里（详见 `41`）。Port 挂上的回调（`get_name` / `hold` / `drop` / `on_register` / `on_unregister`）让 register 时自动把 `ops_life` 打成 `REGISTERED`、并让表多持一份 ref；unregister 则取消掉端口的注册并关闭（compat 约定见 §6.1、§6.4；API 对照见 §7.2）。

```text
  port 注册表操作：
    register_port
        ACTIVE → REGISTERED（注册表多持一份 ref）
    unregister_port
        从 port 注册表摘下 → life 改为 CLOSING → 等 ops_count 清零 → 清理队列 → life 改为 CLOSED → 放下注册表持有的那份 ref
    port_table_lookup
        命中 → 获取调用方 ref → 可选 ops_allow(LOOKUP)
```

线程侧还有 `port_cache`（§4.5），减少反复查全局 port 注册表；但**每次真正拿到可用指针时，仍要过 `ops_allow(LOOKUP)`**（见 `22`），cache 不能绕过准入。

---

### 4.5 线程侧 IPC 字段：`send_msg_queue` / `recv_msg_queue` 等

Port 只做会合（在 `thread_queue` 上配对）。消息真正进出，靠的是**当前线程**上的 MSQ 与相关字段（`THREAD_COMMON`宏，见`thread.h`）：

```c
ms_queue_t recv_msg_queue;           /* 消息接收队列 */
ms_queue_t send_msg_queue;           /* 消息发送队列 */
volatile Message_t *send_pending_msg;/* 单槽：为了防止消息发送失败的丢失情况，记录下正要发送的消息 */
atomic64_t recv_pending_cnt;         /* 已到达、尚未 dequeue 的消息条数 */
volatile void *port_ptr;             /* 当前阻塞在哪个 Message_Port_t；如果没有为 NULL */
struct thread_port_cache port_cache; /* thread_lookup_port 的本地 cache（条数≤16） */
```

这两条 `*_msg_queue` 队列是不带状态的 MSQ，挂的节点类型为 `Message_t`，但 port 的 `thread_queue` 上挂 `Ipc_Request_t`。

| 字段 | 为什么要有 |
|------|-----------|
| `send_msg_queue` | 存放待发送的消息队列；配对成功后 `ipc_transfer_message` 从这里（或 `send_pending_msg`）取走。 |
| `recv_msg_queue` | 存放 `recv_msg` 接收成功的消息队列，调用方必须 `dequeue_recv_msg()` 取出所有权。 |
| `send_pending_msg` | 单槽。普通路径主要走 `send_msg_queue`；`ipc_system_*` 以及 transfer 中途「receiver 正在退出」等情形会把 `Message_t` 临时存放在这里，避免弄丢，细节见 `20`。 |
| `recv_pending_cnt` | 有消息到达时增加，`dequeue_recv_msg` 成功时减一。 |
| `port_ptr` | 线程在 port 上阻塞时，时记下它阻塞在的 port。清理时 CAS 清掉，避免醒过来还以为挂在一个已经关掉的 port 上。 |
| `port_cache` | `thread_lookup_port(name)` 的本地加速：缓存 `(row_index, row_gen)` token。命中则 `resolve_token`，未命中再走全局 lookup。cache **不** pin port（不额外长期 `ref_get`）；unregister 后 gen 对不上即失效。最多 `THREAD_MAX_KNOWN_PORTS`（16）个，采用 LRU 淘汰。 |

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
| `port.h` / `port.c` | Port 注册表和 Port 生命周期管理 |
| `message.h` / `message.c` | Msg_Data_t / Message_t 及基本消息管理和生命周期 |
| `name_index.c` | 通用名称索引原语（port 注册表的 `by_name` 用它实现） |
| `ipc.h` / `ipc.c` | send/recv/transfer等原语（行为细节见 `20`） |
| `thread.h` / `thread.c` | 线程侧收发相关数据字段及`thread_lookup_port`等缓存操作。 |

---

## 6. 流程：兼容层怎么把 Port / 消息用起来

§4 解释对象为什么长这样，§7 是 API 与字段对照。本节只写**兼容层（或同风格上层）落地时的编排与约定**，不重讲字段表，也不代替 `20` 的收发状态机。

| 小节 | 讲什么 | 明确不讲 |
|------|--------|----------|
| §6.1 | Server：注册 Port、收消息循环、注销时要注意什么 | `ops_life` / `ops_count` 逐步状态（§4 / §7）；hook 与准入见 `22` |
| §6.2 | Client：查找 Port、发一条、放下引用；失败时怎么认 | `port_cache` / token 实现细节；推拉谁跑 transfer 见 `20` |
| §6.3 | 消息对象在路径上的所有权（谁 create / dequeue / put） | 假出队为何拆类型见 §1.3；`ipc_transfer_message` 内部步骤见 `20` |
| §6.4 | 引用与关闭的短清单（易踩坑） | `port_clean_thread_queue` 算法（`port.c`）；关闭通知不对称的长因果（结论见 §3） |

### 6.1 Server：注册 Port、收消息循环、注销

兼容层里一个「对外服务」的线程对 Port 的使用，典型长这样：

1. `create_message_port(name, hooks)` —— 得到 port，`ops_life` 还是 ACTIVE，**别人按名找不到**。
2. `register_port(global_port_table, port)` —— 进入 port 注册表；重名且不是同一对象会失败，要处理错误。成功后别人才能按名找得到。
3. 循环：`recv_msg(port)`；成功则 `dequeue_recv_msg()`，按 opcode / 载荷办事，再用 `ref_put(msg)`。
4. 不再服务：先 `unregister_port(global_port_table, name)`，再对 create 拿到的那份引用 `ref_put(port)`。

接口使用者还要记住：

- **注销必须主动做，而且要在还想叫醒等待者的时候做。** 不能只 `ref_put`、指望结构释放时自然清队——唤醒等在这发生在 `unregister_port`，不发生在 last-ref 析构。拖到结构没了再清，还堵在 `thread_queue` 上的人会永远阻塞。
- 关 port 之后，若本线程还卡在 `recv_msg` 上，醒来时可能是：dequeue 到 `KMSG_OP_SYSTEM_PORT_CLOSED`（`recv_msg` 返回成功），或直接 `-E_REND_PORT_CLOSED`（只有 flag、没有消息）。server 循环对**两种收尾形态**都要能处理。对照表见 §3；kmsg 字节布局见 `21`。
- `hooks` / `ops_allow` 不是把基本路径跑通的前提；需要准入策略时再读 `22`。

### 6.2 Client：查找 Port、发一条、放下引用

server 端已经 `register_port` 之后，客户端：

1. `thread_lookup_port(name)` —— 返回的 `Message_Port_t*` **自带一份引用**（内部可能命中本线程 `port_cache`，调用方当普通查找用即可）。
2. 准备待发消息（顺序见 §6.3）：依次 `create_message_data`、`create_message_with_msg`、`enqueue_msg_for_send`。
3. `send_msg(port)` —— 有接收方则完成投递；否则在 port 上阻塞等待。
4. 用完 port：`ref_put(port, free_message_port_ref)`。

调用侧常见坑：

- lookup 得到 NULL意味着名字从未注册、已被 unregister，或 LOOKUP 钩子拒绝（拒绝时 errno 可能被丢掉，见 `22`）。此时不要解引用。
- 发送过程中 port 被关掉：`send_msg` 返回 `-E_REND_PORT_CLOSED`，并且会 **丢掉未发出的孤儿消息**。不要假设「失败了消息还躺在 `send_msg_queue` 里等重试」。
- 在仍可能阻塞的 `send_msg` 返回之前，不要把 lookup/create 的那份 port 引用 put 光；用指针就要配着持有引用。

### 6.3 消息：谁创建、谁取出、谁释放

假出队迫使「载荷」和「队列节点」拆开（§1.3）。兼容层路径上只要守住所有权：

| 步骤 | 调用方该做什么 |
|------|----------------|
| `create_message_data` | 得到 `Msg_Data_t`（载荷 + `free_data` 析构函数），自带引用。 |
| `create_message_with_msg` | 得到 `Message_t` 节点，`data` 字段指向上述载荷。 |
| `enqueue_msg_for_send` | `Message_t` 节点进入**本线程** `send_msg_queue`；此后按 API 约定处理 create 侧多余引用，不要再当「独占节点」乱 put。 |
| `send_msg` 成功 | 给对端 `recv_msg_queue` 放上**新的**一条 `Message_t`，`data` 指向同一份 `Msg_Data_t`（并用 `ref_get` 增加这份 `Msg_Data_t` 的引用计数）。发送侧那条 `Message_t` 在 transfer 成功后被 `ref_put`，不再和接收侧长期各握一条指向同一个 `Msg_Data_t` 的不同 `Message_t` 。 |
| 接收侧 | `recv_msg` 成功后**必须** `dequeue_recv_msg()` 取得所有权，用完再 `ref_put` 这条 `Message_t`。 |
| 关闭的时候存在的未发出消息 | 走 send 的 orphan drop；当作发送失败，不要二次释放同一节点。 |

谁在配对成功后真正跑 `ipc_transfer_message`（推还是拉）、try 路径与 IRQ 投递，一律见 `20`。本篇只保证：兼容层按上表 create / enqueue / dequeue / put，就不会把 `Msg_Data_t` 和 `Message_t` 的寿命弄反。

### 6.4 引用与关闭：短清单

写调用方代码时可用下面几条自检：

1. **每一份 create / lookup 拿到的 port，都要配对 `ref_put`。** REGISTERED 期间注册表另持一份；那份由 `unregister_port` 放下，不是客户端多 put 一次能代替的。
2. **先 `unregister_port`，再 put 自己的 port 引用。** 唤醒等待者发生在 unregister（见 §6.1）。
3. **关 port 后的两种收尾形态都要处理**（§3 表；server 见 §6.1，client 见 §6.2）。
4. **`port_cache` 不延长 port 寿命。** token 失效会回查 port 注册表；逻辑上仍以「持有的 ref」为准。
5. **线程退出与 port 上残留的 `Ipc_Request`：** request 握着 thread 的引用，避免野指针。会合时若线程已不是对应的 `block_on_*`，或 `port_ptr` 对不上，这条 request 会被丢掉（见 `20`）。不要假设「线程一 exit，队上 request 立刻消失」，也不要为此再发明通用 `cancel_ipc`。

---

## 7. 公开 API：如何操作这些结构

本篇 API 主要在 `port.h` / `message.h`；`send_msg` / `recv_msg` / `ipc_transfer_message` 的状态机见 `20`，这里只说明它们**碰到哪些字段**。说明以头文件 Doxygen 为准。

**本篇不涉及：** 收发状态机细节见 `20`；kmsg 载荷格式见 `21`；`ops_allow` 准入见 `22`；MSQ/EBR 算法见 `18` / `17`；`name_index` 通用约定见 `41`。

### 7.1 典型编排（从空系统到收完一条消息）

| 场景 | 顺序 | 主要动到的结构 |
|------|------|----------------|
| 初始化建表 | `global_port_init` | 分配 `global_port_table`，`by_name` 空索引 |
| Server 公布 port | 先 `create_message_port`，再 `register_port` | 新 `Message_Port_t`（从 ACTIVE 到 REGISTERED）；表多持 1 份 ref；此时 `name` 可被查找 |
| Client 找到 port | `thread_lookup_port` | 可能写 `port_cache`；返回持 ref 的 port |
| 创建消息并发送 | 依次 `create_message_data`、`create_message_with_msg`、`enqueue_msg_for_send`、`send_msg` | 新 `Msg_Data_t` + `Message_t`；进 `send_msg_queue`；port 上配对或挂 `Ipc_Request` |
| 接收消息并释放 | 先 `recv_msg`，再 `dequeue_recv_msg`，用完 `ref_put(msg)` | 从 `recv_msg_queue` 取出 `Message_t`；EBR 推迟回收节点；末次 put 释放 `Msg_Data_t` |
| 关闭 | `unregister_port`，各方再 `ref_put(port)` | 从注册表中摘下、清 `thread_queue`、port 转为 CLOSED；refs 归零后释放 port |

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
| `global_port_init` | 调用 `port_table_create()`，并写入 `global_port_table`。生成一个空表，不预注册任何port。失败则返回 `-E_RENDEZVOS`。 |
| `create_message_port` | 分配一个 `Message_Port_t`数据结构（含可选的兼容层添加的 `append_port_info`相关字段）；初始化相关字段。此时还不在 port 注册表里。如果兼容层注入的初始化 `hooks->init` 失败则回滚。 |
| `register_port` | 在持有表的锁的情况下执行：这里提供了一个可选的 `ops_allow(REGISTER)` 来让兼容层注入相关操作决定是否可以允许注入的hook，再把 port 按 `name` 插入，修改 `ops_life` 从 ACTIVE 变为 REGISTERED，写上 `table` 指针，表对 port `ref_get` 一次以获取引用计数。重名且不是同一对象则注册失败。 |
| `unregister_port` | 按名从 `by_name` 摘掉， `ops_life`  进入 CLOSING 状态，等到 `ops_count==0`之后，清理 `thread_queue`（实现见 `port_clean_thread_queue`），进入 CLOSED 状态，再对表进行 `ref_put` 释放引用。名字不在表中 / 已非 REGISTERED 也返回成功。兼容层需要主动在解除注册时调用这个函数。 |
| `port_table_lookup*` | 在 `by_name` 里按名找到 port，获取一个引用计数；再跑 `ops_allow(LOOKUP)` 执行兼容层是否允许查找到这个port的准入测试，如果拒绝则 put 掉并返回 NULL。带 token 的变体把（行下标，代际）写给线程缓存用。 |
| `port_table_resolve_token` | 用缓存 token + 名字在锁下验证是否可以正常用cache；如果成功同样增加ref得到这个port。 |
| `port_ops_begin` / `end` | begin：要求 `ops_life==REGISTERED` 且（可选）`ops_allow` 通过，然后 `ops_count++`。end：`ops_count--`。阻塞等待前必须先 end，否则 unregister 永远等不到清队。详见 `22`，主要目的就是port准入，以及兼容层注入port准入标准。 |
| `free_message_port_ref` | 引用归零后调用 `delete_message_port_structure`（先 `hooks->fini`，再释放内存）。调用方 lookup/create 拿到的每一 ref 都要配对 put。 |

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
| `create_message_data` | 分配 `Msg_Data_t`，将 `*data_ptr` 指向的裸的内存 buffer 进行封装（成功后把 `*data_ptr` 置 NULL，调用方不再 free 这块）。设置`refcount=1`。要求 `data_len` 必须 >0 且指针有效。 |
| `free_msgdata_ref_default` | 默认的 `free_data` 函数指针，也是`Msg_Data_t`的默认释放函数：先释放 `data` buffer，再释放 `Msg_Data_t` 本身。 |
| `create_message_with_msg` | 分配一条 `Message_t`，`data` 指向传入的 `Msg_Data_t` 并对其 `ref_get`；`Message_t` 节点 ref=1。若调用方不再单独握着 `Msg_Data_t*`，通常再 put 一次多出来的那份。 |
| `create_message_structure` / `fill_message_data` | 先分配空的 `Message_t` 再挂上 `Msg_Data_t`。适合「先占节点、后填载荷」的路径。 |
| `free_message_ref` | `Message_t` 引用归零时**不立刻**拆节点：交给 EBR 推迟（见 `17`/`18`）。真正回收时 put `Msg_Data_t`，再 free 这条 `Message_t`。 |

拆成 Data / Message 的原因见 §1.3 / §4.2。

---

## 8. 多架构

Port 的对象模型与 ISA 无关；原子操作相关实现随 arch，语义相同。

---

## 9. 测试

`modules/test/` 下 port/ipc 相关；`make ARCH=x86_64 config && make run`。其中有port表的查找，以及根据port数据结构进行通信的两收两发等测例。

---

## 10. 限制与后续

- 我们预留了Hook来做基于Port的准入。

---

## 11. 变更记录

- 2026-10-16：对照源码修正三处：配对后的 `Ipc_Request` 仍可能当 dummy；`unregister_port` 放下的是注册表那份 ref；transfer 成功后发送侧 `Message_t` 被 put。§6.4 陈旧 request 改为按 status / `port_ptr` 丢掉。
- 2026-10-15：§6 按审阅意见补强：恢复「先 unregister」、client 失败约定、§6.4 短清单；修正 6.3 transfer 后 Message_t 表述；清掉 §6.4 死链；「两种错误」改为「两种收尾形态」。
- 2026-10-15：§6 重写为「兼容层怎么用」：6.1 server / 6.2 client / 6.3 消息所有权 / 6.4 引用与关闭清单；去掉与 §4/§7/`20` 重复的清队算法、transfer 摘要、lookup 实现逐步说明；§3 关闭表改为指向 §6 的用法约定。
- 2026-10-13：用语回调：去掉「数据袋 / 信封 / 收发件箱」等自造比喻，直接用 `Msg_Data_t` / `Message_t` / `send_msg_queue` 等；dummy / token / append 区保留英文惯用说法。
- 2026-10-13：§1–§3 润色；§3 关闭通知改为结论表，细节后置；§4 入口补阅读顺序与两套状态辨析。
- 2026-10-13：§1.1 / §4 对象总览 / `ops_life` / port 注册表等 ASCII 图改竖排，避免横版过宽。
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
