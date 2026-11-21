# Port 与消息模型

v0.1 · 2026-10-02（2026-11-19：匿名 / 具名拆分定稿）

本篇覆盖两块：一是匿名会合（`port.h` / `port.c`、`message.h` / `message.c`），二是按名发现用的具名 port（`named_port.h` / `named_port.c`，含名表与 hooks）。收发状态机见 `20-阻塞与非阻塞收发.md`；准入细节见 `22-Port-hook与准入.md`；kmsg 见 `21-kmsg与TLV序列化.md`。

**建议先读** `18-支持IPC的无锁MSQ队列实现.md` **§1**（整个无锁 IPC 的动机 §1.1–1.2；驱动 MSQ 改成现状的约束在 §1.3–1.8），再读本篇。MSQ/EBR 算法见 `18-支持IPC的无锁MSQ队列实现.md` §2 起；`name_index` 通用约定见名称索引篇（`41-名称索引注册表.md`）。

---

## 1. 概述

`18-支持IPC的无锁MSQ队列实现.md` 给出了无锁队列原语；在此之上，还要回答「消息怎么找到对端、怎么交到对方手里」。本篇讲的是对端发现用的中介——**Port**，以及消息本身使用的数据结构如何组织。

读本篇前，建议已读完 `18-支持IPC的无锁MSQ队列实现.md` §1（动机与对 MSQ 的改造约束）。收发状态机见 `20-阻塞与非阻塞收发.md`；队列算法细节见 `18-支持IPC的无锁MSQ队列实现.md` §2 起。

### 1.1 对端发现和消息投递拆开

按 `18-支持IPC的无锁MSQ队列实现.md` 的思路，临界区变成「单执行流 server + 无锁把请求递过去」之后，第一个问题就是：**我要找的那个 server 线程在哪？**

若有多个同类线程都能处理（例如两张网卡接口各挂一个发送线程），发给谁都可以，但总得有一个数据结构来表示这种关系，而不是调用方手里直接握着对方 `Thread_Base*`线程控制块。

我们借鉴 endpoint（端点：可被寻址的通信端）一类设计，把「交给另一个线程」拆成两步：

1. **先会合**——在某个名字上，找到（或等到）对端线程；
2. **再投递**——把载荷交到对端线程的 `recv_msg_queue`等接收位置。

core 对应两层：

1. **Port 会合层** — Port 的 `thread_queue` 上挂的是「谁在等」：在队列里面排队的是 `Ipc_Request_t` ，它指向某个 `Thread_Base`，**而不是**消息本身在排队。发送方与接收方在这里进行配对。
2. **每线程消息层** — 每个线程有自己的 `send_msg_queue` / `recv_msg_queue`（都是 MSQ）作为发送方的队列和接收方的队列；在第一步配对成功后由 `ipc_transfer_message` 进行消息在两个队列之间进行投递（为何还要拆成 `Msg_Data_t` + `Message_t`，见 §1.3 与 `18-支持IPC的无锁MSQ队列实现.md` §1.5）。

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

因此**没有**「`send_msg(port, msg)` 一步完成」模式的接口：调用方需要先 `enqueue_msg_for_send` 把 `Message_t` 表示的消息放进本线程 `send_msg_queue`，再对 port 做 `send_msg` 去配对。一次发送的时序见 `20-阻塞与非阻塞收发.md` §1.1。接收也是如此，需要先 `recv_msg` 接收消息，接收到消息就表示，在它的 `recv_msg_queue` 里面存在一条消息（否则就是被阻塞住的），然后 `dequeue_recv_msg` 从接收队列中弹出一条消息。

### 1.2 为什么会合层用「单状态」一条队列

直观做法是 Port 里各挂一条「发送等待」队列和一条「接收等待」队列。但「看对端有没有人，要么取出对方，要么把自己挂上」要跨两条队列，**很难做成无锁下的原子操作**。典型事故：发送方看见接收队列空，准备挂上自己；接收方同时看见发送队列空，也挂上自己——两边其实都有人，却双双阻塞。

改成**一条**队列更简单：队里要么全是发送等待者，要么全是接收等待者，要么为空。若来了「另一种」角色，必然能两两配对，最后剩下来的仍是同一种。这依赖 `18-支持IPC的无锁MSQ队列实现.md` 里「带状态」的入队 / 出队。

挂进这条队列的是 **`Ipc_Request_t`**，而不是 `Thread_Base`（TCB）本身。原因：MSQ 有「假出队」——逻辑上弹出的节点还可能留在队里当新的 **dummy**。若把 TCB 直接入队，假出队后再入队，容易让队头 dummy 的 `next` 指回自己，形成环。把「等待」单独做成 `Ipc_Request_t`，就可以避免把 TCB 卷进这个问题。

但是这样需要额外注意一个问题，`Ipc_Request_t`需要一个指针指向对应的 `Thread_Base`（TCB），但是反过来TCB不应该有个指针指向这个request——否则就是循环指针，这不能如此做。然而不能如此做带来了另一个生命周期的问题：如果TCB被释放了，`Ipc_Request_t`仍然挂在Port队列里面，会有野指针的问题。为了解决这个问题，我们也对这个TCB使用了refcount。在其refcount没有归零之前，不释放TCB。

### 1.3 假出队如何迫使消息拆成 `Msg_Data_t` + `Message_t`

MSQ 假出队的细节见 `18-支持IPC的无锁MSQ队列实现.md` §1.4–1.5。要点是：逻辑上「弹出」的队列节点仍可能留在队上当 dummy，发送方不能直接把消息搬到接收方的 `recv_msg_queue`，同时我们需要减少复制数据带来的开销。所以拆成两个类型：

- **`Msg_Data_t`** — 拥有载荷指针、长度、`free_data` 和自己的 `refcount`，是真正的拥有消息的数据结构；
- **`Message_t`** — 带 `ms_queue_node` 的队列节点，只是在队列中排队的节点封装，`data` 字段指向一份 `Msg_Data_t`。

`ipc_transfer_message` 时：给接收方 **复制一条 `Message_t`**，两边的 `Message_t.data` 指向同一份 `Msg_Data_t`（对其 `ref_get`）。字段见 §4.2；谁在什么时机跑 transfer，见 `20-阻塞与非阻塞收发.md`。

---

## 2. 目标与边界

**提供：** 匿名 Port 的创建 / 关闭与会合；具名 Port 的创建 / 注册 / 按名查找 / 注销；消息对象（`Message_t` / `Msg_Data_t`）的分工；引用计数及与延迟回收（EBR，见 `17-EBR与线程资源回收.md`）的衔接。

**不描述：** 收发状态机与系统异步投递（`20-阻塞与非阻塞收发.md`）；MSQ 算法本身（`18-支持IPC的无锁MSQ队列实现.md`）；具体 RPC / 服务协议（上层）；`ops_allow` 细则（`22-Port-hook与准入.md`）。

---

## 3. 分层与调用方（先看用法，细节在后文）

读后面章节前，先分清两件互不替代的事：

1. **会合能不能用**——记在匿名 port 的 `ops_life` 上（OPEN / CLOSING / CLOSED）。`close_port` 管的是这个。
2. **别人能不能按名找到**——记在具名 port 是否挂进了 `Port_Table`（`table` 是否非空）。`register_named_port` / `unregister_named_port` 管的是这个。

未进名表也可以对已经握在手里的匿名 port 收发；进了名表也不会自动改 `ops_life`。

兼容层典型编排（**具名 server / 具名 client** 的逐步用法、注销时序、失败时该认哪些返回值）集中在 §6.1 / §6.2，此处不再复列步骤。

**纯匿名**（调用方直接传指针，例如 wait / sleep 一类）：先 `create_message_port(service_id, owner_list)`（`owner_list` 见 §4.1 / §6.4；无归属则 `NULL`），再用 `send_msg` / `recv_msg`，结束时 `close_port`，再放下 create（先 `list_del_init(owner_link)` 再 `ref_put(..., free_message_port_ref)`）。没有 hooks，也不进名表。

关闭 port 时，仍堵在 `thread_queue` 上的等待者必须被叫醒，否则会永远阻塞。发送侧与接收侧**通知方式不同**（因为醒回来时看到的 API 成功形态不同）。这里只记结论：

| 谁在等 | 关闭时怎么通知 | 阻塞 API 醒后大致结果 |
|--------|----------------|----------------------|
| 发送等待（`block_on_send`） | 把线程的 flags 置位 `THREAD_FLAG_IPC_PORT_CLOSED`，线程的 status 变成 ready | `send_msg` 丢掉未发出的孤儿消息，返回 `-E_REND_PORT_CLOSED` |
| 接收等待（`block_on_receive`） | 优先 `ipc_system_deliver_to` 投 `KMSG_OP_SYSTEM_PORT_CLOSED` 消息到其 `recv_msg_queue`；失败才置位 flag | 有 kmsg 则 `REND_SUCCESS`，再 dequeue 认 opcode，上层从opcode辨认出来，端口关闭的情况；仅有 flag 则 `-E_REND_PORT_CLOSED` |

---

## 4. 数据结构：对象分工与字段含义

定义以头文件为准（`port.h` / `message.h` / `ipc.h` / `thread.h`）。

阅读顺序建议：先看 §4.0 角色图，再按匿名 Port、具名 Port、消息、等待请求、名表、线程字段往下读。每个小节都是「先讲为什么有这个对象，再贴结构体与字段表」。公开 API 见 §7。

### 4.0 对象总览（先看角色）

§1.1 的两层，落到代码上就是下面几类对象。按名查找时，先经过名表找到具名 port；真正配对仍发生在它内部嵌着的那份匿名会合上：

```text
  Port_Table（global_port_table）
  │  按名字索引具名 port，别人才能发现
  ▼
  Named_Message_Port_t（具名 port）
  │  开头是 PORT_COMMON：嵌着一份与 Message_Port_t 同布局的匿名会合
  ▼
  Message_Port_t（匿名会合）
  │  thread_queue 上挂着 Ipc_Request_t，每个 request 指向一个 Thread_Base
  │  配对成功后取得对端 Thread_Base
  ▼
  Thread A（sender）
  │  send_msg_queue / send_pending_msg 里放 Message_t
  │  ipc_transfer_message
  ▼
  Thread B（receiver）
  │  recv_msg_queue：收到复制出来的新 Message_t
  │  两边的 Message_t.data 指向同一份 Msg_Data_t
  ▼
  Msg_Data_t（载荷 + refcount）
```

| 对象 | 一句话职责 |
|------|-----------|
| `Message_Port_t` | 匿名会合端点；`thread_queue` 上挂发送/接收等待者；本身没有名字 |
| `Named_Message_Port_t` | 具名 port：里面嵌着匿名会合，外面带 `name`、`table` 和可选 hooks；见 §4.1.1 / `22-Port-hook与准入.md` |
| `Ipc_Request_t` | 挂进 `thread_queue` 的等待节点，指向某个 `Thread_Base` |
| `Message_t` | 挂进线程 `send_msg_queue` / `recv_msg_queue` 的 MSQ 节点，`data` 指向 `Msg_Data_t` |
| `Msg_Data_t` | 载荷本体（指针、长度、`free_data`、refcount） |
| `Port_Table` | 全局具名 port 注册表（内部用 `name_index_t`） |
| 线程 IPC 字段 | `send_msg_queue` / `recv_msg_queue` / `send_pending_msg` / `port_ptr` / `port_cache` 等 |

---

### 4.1 `Message_Port_t`：匿名会合点

Port 回答了 §1.1 的问题：没有它，发送方不知道该把消息交给哪个线程的 `recv_msg_queue`；有了它，双方在**同一个 port 对象**上会合——要么手里直接握着匿名 port 指针，要么先按名查到具名 port，再在它嵌着的那份匿名会合上配对。

**匿名 port 是完整对象**：`message_port_init` / `create_message_port` 之后就可以 `send` / `recv`，**不**要求先注册。按名发现、hooks、名表属于具名 port（§4.1.1 / `22-Port-hook与准入.md`），不要塞进 `ops_life` 里混谈。

Port 上有两套容易混的「状态」，先分开：

1. **会合队列状态**（EMPTY / SEND / RECV）——记在队列尾指针的低几位 tag 里（tagged ptr 见 `18-支持IPC的无锁MSQ队列实现.md`），表示「现在队里全是谁在等」。
2. **生命周期 `ops_life`**（OPEN / CLOSING / CLOSED）——能不能进入收发临界区；**与是否在名表无关**。

相关常量（`port.h`）：
```c
/* thread_queue 的 tagged 指针低位：当前队列里全是谁在等 */
#define IPC_PORT_APPEND_BITS 2
#define IPC_PORT_STATE_EMPTY 0   /* 空（只有 dummy） */
#define IPC_PORT_STATE_SEND  1   /* 全是发送等待者 */
#define IPC_PORT_STATE_RECV  2   /* 全是接收等待者 */

/* ops_life：能不能 port_ops_begin */
#define PORT_OPS_LIFE_CLOSED  0
#define PORT_OPS_LIFE_OPEN    1  /* init/create 后；可 begin */
#define PORT_OPS_LIFE_CLOSING 2  /* close_port 进行中 */
```

结构体（匿名，`port.h`）：

```c
struct Msg_Port {
        ms_queue_t thread_queue;
        ms_queue_node_t msq_dummy; /* 初始 MSQ sentinel，随 port 体，不单独 m_free */
        ref_count_t refcount;
        u32 service_id;
        atomic64_t ops_life;
        atomic64_t ops_count;
        struct list_entry owner_link; /* 可选：挂到调用方提供的 owner 链表头 */
};
typedef struct Msg_Port Message_Port_t;
```

**每个字段为什么存在：**

| 字段 | 解决什么问题 |
|------|-------------|
| `thread_queue` | §1.2 的单状态会合队列。只挂等待请求，不挂消息。 |
| `msq_dummy` | `msq_init` 的初始 dummy；假出队后新 dummy 可能是堆上 `Ipc_Request` 节点。 |
| `refcount` | 谁还握着这个 port；堆上创建的对象，末次 `ref_put` 会走到 `free_message_port_ref`，再 `m_free`。 |
| `service_id` | 给 kmsg 的 `module` 一类标签用（例如 `PORT_CLOSED`）；与 `hdr.module` 同为 `u32`；**不是**注册表里的名字。 |
| `ops_life` | 只有 OPEN 才允许 `port_ops_begin`；关闭时先进入 CLOSING，再落到 CLOSED。 |
| `ops_count` | 已 begin 尚未 end 的人数；`close_port` 要等它归零，再清会合队列。 |
| `owner_link` | 可选归属链节点。`create_*(…, owner_list)` 在 `owner_list != NULL` 时 `list_add_tail` 到**任意**调用方提供的 `list_entry` 头上——**不必**是 `Thread_Base`。常见做法是挂到 `&thread->owned_anon_ports` / `owned_named_ports`，由线程退出 walker 两阶段回收；也可以挂到别的宿主链表，由该宿主按同样不变量回收。`close_port` **不**摘链。 |

API 要点：`message_port_init`（可嵌在别的结构里，所以会有这种只初始化不分配的接口，其中不挂链，只初始化链表节点）或 `create_*(…, owner_list)`（堆上分配并 init）之后，`ops_life` 就是 OPEN。`owner_list` 是链表头指针：`NULL` = 不挂链、无「随 owner 死亡自动收」义务；非 `NULL` 则挂上去。**不变量：还挂在某条 owner 链上 ⇔ create/ownership 那枚 ref 尚未放下。** `close_port`：OPEN→CLOSING，等 `ops_count` 归零、清完队列，再 CLOSED；**不**摘 `owner_link`，也**不会** `ref_put`。放下 create/ownership：`message_port_put_owner_ref` / `named_message_port_put_owner_ref`（先摘链再 put）；lookup / 表 pin 用普通 `ref_put`。嵌在别的结构里的 port 只 `message_port_init` + 日后 `close_port`，由外层宿主拆掉。

**`ops_life` 怎么走：**

```text
  message_port_init / create_message_port
        │
        ▼
      OPEN
        │  close_port
        ▼
     CLOSING
        │  等 ops_count == 0
        │  清 thread_queue
        ▼
     CLOSED
        │  调用方 ref_put（堆 create：可能释放对象）
        ▼
     （refcount 归零后释放）
```

- `OPEN`：可以收发；不要求已经挂进名表。
- `CLOSING`：新的 `port_ops_begin` 会失败；等正在进行的收发结束后，再清等待者。
- `CLOSED`：会合队列已经处理完；对象还在不在，交给 refcount（由调用方 `ref_put`）。

**为什么要有 `ops_life` / `ops_count`，而不是只靠引用计数？**

会合是无锁的：多方可以同时 `send_msg` / `recv_msg`，同时另一方又要 `close_port` 清 `thread_queue`。只靠「refcount 不为 0 就不释放内存」，挡不住「半截临界区」和「清队列」撞在一起。所以另外维护一套与 MSQ **分开** 的生命周期计数：

- 只有 `OPEN` 时 `port_ops_begin` 才放行；
- `close_port` 先把状态改成 CLOSING，等 `ops_count` 归零，再清队列，再改成 CLOSED；它自己不放下引用，由调用方随后 `ref_put`。

谁可以按名看见、谁可以经具名接口收发，由具名侧的 `ops_allow` / hooks 负责，**不会**走进匿名的 `port_ops_begin`；见 `22-Port-hook与准入.md`。

---

### 4.1.1 `Named_Message_Port_t`：具名 port（简述）

具名 port 里面嵌着一份匿名会合（结构体开头是 `PORT_COMMON`，布局与 `Message_Port_t` 相同），外面再带名字、名表指针，以及可选的 hooks 和尾部追加区。准入细节见 `22-Port-hook与准入.md`；本篇只记和匿名侧怎么分工：

| | 匿名 `Message_Port_t` | 具名 `Named_Message_Port_t` |
|--|----------------------|------------------------------|
| 创建 | `create_message_port(service_id, owner_list)` / `message_port_init` | `create_named_message_port(name, hooks, owner_list)`（`service_id` 由名字算出，不会是 0） |
| 收发 | `send_msg` / `recv_msg` 及其 try 变体 | 用 `named_port_send_msg` / `named_port_recv_msg` 等：先问 `ops_allow`，通过后再进内部那份匿名会合 |
| 关闭会合 | `close_port` | 通常调用 `unregister_named_port`：先关掉内部的匿名会合，再从名表摘掉；若漏了 unregister，末次 `free_named_message_port_ref` 也会尽量保证已经 CLOSED |
| 释放 create | `message_port_put_owner_ref` | `named_message_port_put_owner_ref`；**不要**对内部匿名部分调 `free_message_port_ref` |

业务代码也不要把具名 port 强转成 `Message_Port_t*` 再直接 `send_msg` / `recv_msg`，那样会跳过准入检查。

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

谁在什么时机跑 transfer、pending 指针见 `20-阻塞与非阻塞收发.md`；构造 / 释放见 §7.3。

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
| `thread` | 配对成功后知道该给谁 transfer、该唤醒谁。对 TCB 的引用配对见下；避免「队列上还有 request、线程已经拆掉」。 |
| `queue_ptr` | 辅助记录；正常配对不依赖它保证正确性。 |

两条引用计数不要混（收发路径上的逐步 get/put 见 `20-阻塞与非阻塞收发.md` §4.1）：

- **`thread->refcount`**：仅在 `create_ipc_request` **+1**，在 `delete_ipc_request`（EBR 真正回收 request 之后）**-1**。单向：request → thread，TCB 不回指 request。
- **`ms_queue_node.refcount`**：管 request 节点本身；入队后由队列持有，配对方 dequeue 后再握一份，用完 `ref_put(..., free_ipc_request)`；假出队时节点常仍当 dummy，不是配对当下就拆掉。

配对成功后，调用方拿到这份 `Ipc_Request_t` 去 transfer 并唤醒线程，再 put 节点引用。

---

### 4.4 `Port_Table`：为什么要有全局 port 注册表

若没有 port 注册表，客户端只能直接握着指针，跨模块很难发现对端。`Port_Table`（通常就是 `global_port_table`）专门给**具名** port 用：开机时 `global_port_init` 建一张空表，server 调用 `register_named_port`，client 再按名查找。纯匿名 port **不会**进这张表。

```c
struct Port_Table {
        name_index_t by_name; /* 通用「按名索引」原语，见 41 */
};

extern struct Port_Table *global_port_table;
```

表结构本身很薄：行、哈希桶、可缓存的 `name_index_token_t`（行下标加代际）都在 `name_index_t` 里（见 `41-名称索引注册表.md`）。名表只回答「能不能按这个名字找到」；内部那份匿名会合的 `ops_life`（OPEN / CLOSING / CLOSED）仍由 `close_port` 管。**注册进表不会**改写 `ops_life`。

```text
  具名侧常见步骤（和 ops_life 分开看）：
    create_named_message_port
        堆上对象；内部会合已是 OPEN；table 仍为空，别人按名找不到
    register_named_port
        挂进名表；表多持一份引用；写上 port->table
    unregister_named_port
        先关掉内部的匿名会合（清会合队列），再从名表摘掉，最后放下表持有的那份引用
    port_table_lookup*
        命中后给调用方一份引用，再跑可选的 ops_allow(LOOKUP)；
        若拒绝，则放下这份引用并返回 NULL
```

线程侧还有 `port_cache`（§4.5），用来少查几次全局表；但**每次真正把可用指针交到调用方手里时，仍要过一遍 `ops_allow(LOOKUP)`**（见 `22-Port-hook与准入.md`），缓存不能绕过准入。

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
| `send_pending_msg` | 单槽。普通路径主要走 `send_msg_queue`；`ipc_system_*` 以及 transfer 中途「receiver 正在退出」等情形会把 `Message_t` 临时存放在这里，避免弄丢，细节见 `20-阻塞与非阻塞收发.md`。 |
| `recv_pending_cnt` | 有消息到达时增加，`dequeue_recv_msg` 成功时减一。 |
| `port_ptr` | 线程在 port 上阻塞时记下它阻塞在的 port。清理时 CAS 清掉，避免醒过来还以为挂在一个已经关掉的 port 上。 |
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
| `port.h` / `port.c` | 匿名会合：创建、关闭、`port_ops_begin` |
| `named_port.h` / `named_port.c` | 具名 port：名表、hooks、按名收发封装、注册与注销 |
| `message.h` / `message.c` | `Msg_Data_t` / `Message_t` 及生命周期 |
| `name_index.c` | 通用名称索引（`Port_Table.by_name` 用它实现） |
| `ipc.h` / `ipc.c` | 匿名侧的 send / recv / transfer（行为细节见 `20-阻塞与非阻塞收发.md`） |
| `thread.h` / `thread.c` | 线程侧收发字段，以及 `thread_lookup_port`、`port_cache` |

---

## 6. 流程：兼容层怎么把 Port / 消息用起来

§4 解释对象为什么长这样，§7 是 API 与字段对照。本节只写**兼容层（或同风格上层）落地时的编排与约定**，不重讲字段表，也不代替 `20-阻塞与非阻塞收发.md` 的收发状态机。

| 小节 | 讲什么 | 明确不讲 |
|------|--------|----------|
| §6.1 | Server：注册 Port、收消息循环、注销时要注意什么 | `ops_life` / `ops_count` 逐步状态（§4 / §7）；hook 与准入见 `22-Port-hook与准入.md` |
| §6.2 | Client：查找 Port、发一条、放下引用；失败时怎么认 | `port_cache` / token 实现细节；推拉谁跑 transfer 见 `20-阻塞与非阻塞收发.md` |
| §6.3 | 消息对象在路径上的所有权（谁 create / dequeue / put） | 假出队为何拆类型见 §1.3；`ipc_transfer_message` 内部步骤见 `20-阻塞与非阻塞收发.md` |
| §6.4 | 引用与关闭的短清单（易踩坑） | `port_clean_thread_queue` 算法（`port.c`）；关闭通知不对称的长因果（结论见 §3） |

### 6.1 Server：注册 Port、收消息循环、注销

兼容层里一个「对外服务」的线程，典型长这样：

1. `create_named_message_port(name, hooks, owner_list)` —— 得到具名 port；内部会合已经是 OPEN，但还没进名表，**别人按名找不到**。`owner_list` 可为 `&thread->owned_named_ports`、别的宿主链表头，或 `NULL`（见 §6.4-7）。
2. `register_named_port(global_port_table, port)` —— 挂进名表；若同名已被别的对象占用会失败。成功后别人才能按名找。
3. 循环调用 `named_port_recv_msg(port)`；成功则 `dequeue_recv_msg()`，按 opcode / 载荷办事，再用 `ref_put(msg)`。
4. 不再服务：先 `unregister_named_port(global_port_table, name)`，再放下 create（`named_message_port_put_owner_ref(port)`，或等价的先摘链再 put）。

接口使用者还要记住：

- **注销必须主动做，而且要在还想叫醒等待者的时候做。** 不能只 `ref_put`、指望结构释放时自然清理队列——叫醒等待者发生在 `unregister_named_port` 里调用的 `close_port`，而不是「只从表里摘掉名字」。末次 `free_named_message_port_ref` 也会尽量把内部会合关到 CLOSED，但那只是兜底；业务上仍应先 unregister。
- 关 port 之后，若本线程还卡在接收上，醒来时可能是：dequeue 到 `KMSG_OP_SYSTEM_PORT_CLOSED`（接收接口返回成功），或直接得到 `-E_REND_PORT_CLOSED`（只有 flag、没有消息）。server 循环对**两种收尾形态**都要能处理。对照表见 §3；kmsg 字节布局见 `21-kmsg与TLV序列化.md`。
- `hooks` / `ops_allow` 不是把基本路径跑通的前提；需要准入策略时再读 `22-Port-hook与准入.md`。

### 6.2 Client：查找 Port、发一条、放下引用

server 已经 `register_named_port` 之后，客户端：

1. `thread_lookup_port(name)` —— 返回的 `Named_Message_Port_t*` **自带一份引用**（内部可能命中本线程的 `port_cache`）。
2. 准备待发消息（顺序见 §6.3）：依次 `create_message_data`、`create_message_with_msg`、`enqueue_msg_for_send`。
3. `named_port_send_msg(port)` —— 有接收方则完成投递；否则在 port 上阻塞等待。
4. 用完：`ref_put(port, free_named_message_port_ref)`。

调用侧常见坑：

- 查找得到 `NULL`：名字从未注册、已被注销，或 LOOKUP 的 hook 拒绝了这次查找（拒绝时具体错误码可能被丢掉，见 `22-Port-hook与准入.md`）。此时不要解引用。
- 发送过程中 port 被关掉：`named_port_send_msg` 最终会走到匿名侧的 `send_msg`，返回 `-E_REND_PORT_CLOSED`，并且会 **丢掉未发出的孤儿消息**。不要假设「失败了消息还躺在 `send_msg_queue` 里等重试」。
- 发送或接收被 `ops_allow` 拒绝：通常返回 `-E_REND_ACCESS`（或 hook 给出的其它错误码），**不会**进入会合；待发消息仍留在本线程的发送队列里。这和「port 已关闭」不是一回事（见 `22-Port-hook与准入.md` §6）。
- 在仍可能阻塞的发送返回之前，不要把 lookup / create 拿到的那份 port 引用 put 光。

### 6.3 消息：谁创建、谁取出、谁释放

假出队迫使「载荷」和「队列节点」拆开（§1.3）。兼容层路径上只要守住所有权：

| 步骤 | 调用方该做什么 |
|------|----------------|
| `create_message_data` | 得到 `Msg_Data_t`（载荷 + `free_data` 析构函数），自带引用。 |
| `create_message_with_msg` | 得到 `Message_t` 节点，`data` 字段指向上述载荷。 |
| `enqueue_msg_for_send` | `Message_t` 节点进入**本线程** `send_msg_queue`；此后按 API 约定处理 create 侧多余引用，不要再当「独占节点」乱 put。 |
| 发送成功（具名走 `named_port_send_msg`，匿名走 `send_msg`） | 给对端 `recv_msg_queue` 放上**新的**一条 `Message_t`，`data` 指向同一份 `Msg_Data_t`（并用 `ref_get` 增加这份 `Msg_Data_t` 的引用计数）。发送侧那条 `Message_t` 在 transfer 成功后被 `ref_put`，不再和接收侧长期各握一条指向同一个 `Msg_Data_t` 的不同 `Message_t`。 |
| 接收侧 | 接收成功后**必须** `dequeue_recv_msg()` 取得所有权，用完再 `ref_put` 这条 `Message_t`。 |
| 关闭的时候存在的未发出消息 | 走 send 的 orphan drop；当作发送失败，不要二次释放同一节点。 |

谁在配对成功后真正跑 `ipc_transfer_message`（推还是拉）、try 路径与 IRQ 投递，一律见 `20-阻塞与非阻塞收发.md`。本篇只保证：兼容层按上表 create / enqueue / dequeue / put，就不会把 `Msg_Data_t` 和 `Message_t` 的寿命弄反。

### 6.4 引用与关闭：短清单

写调用方代码时可用下面几条自检：

1. **每一份 create / lookup 拿到的具名 port，都要配对放下。** lookup / 调用方 pin：`ref_put(..., free_named_message_port_ref)`。create/ownership：先摘 `owner_link` 再 put（`named_message_port_put_owner_ref`，或手写同等两步）。挂在名表期间，表另持一份，由 `unregister_named_port` 放下。
2. **先 `unregister_named_port`，再 put 自己的引用。** 叫醒等待者发生在 unregister 内部的 `close_port`（见 §6.1）。
3. **关 port 后的两种收尾形态都要处理**（§3 表；server 见 §6.1，client 见 §6.2）。
4. **`port_cache` 不延长 port 寿命。** token 失效会回查名表；逻辑上仍以「手里握着的引用」为准。
5. **线程退出与 port 上残留的 `Ipc_Request`：** request 握着 thread 的引用，避免野指针。会合时若线程已不是对应的 `block_on_*`，或 `port_ptr` 对不上，这条 request 会被丢掉（见 `20-阻塞与非阻塞收发.md`）。不要假设「线程一 exit，队上 request 立刻消失」，也不要为此再发明通用 `cancel_ipc`。
6. **具名对象不要对内部那份匿名会合调用 `free_message_port_ref`。** 释放只能走 `free_named_message_port_ref`。
7. **Owner 链表与 create 寿命。** `create_*(…, owner_list)` 的 `owner_list` 是**任意** `struct list_entry *` 链表头，**不**要求一定是 `Thread_Base` 上的字段；`NULL` = 不挂链。常见是 `&thread->owned_anon_ports` / `owned_named_ports`——线程退出时 `thread_close_owned_ports` 两阶段回收：phase1 对链上全部 close（具名用 `np->table` 做 `unregister_named_port`，不是写死 `global_port_table`），**保留** create ref；phase2 对仍在链上的节点 `*_put_owner_ref`。也可挂到别的宿主链表，由该宿主按同一不变量回收。**不变量：还挂在链上 ⇔ create 尚未放过**（自愿放下 create 必须先摘链；末次 `free_*_ref` 也会 `list_del_init` 兜底）。`close_port` **不**摘链。compat 侧 `get_or_create` 在 create+register 成功后另 `ref_get` 给调用方，避免把 create 当 lookup pin 放掉。IRQ / 尚无明确宿主时传 `NULL`。

---

## 7. 公开 API：如何操作这些结构

本篇 API 分两块：匿名会合在 `port.h` / `message.h`；具名名表与 `named_port_send_msg` 一类封装在 `named_port.h`。`send_msg` / `recv_msg` / `ipc_transfer_message` 的状态机见 `20-阻塞与非阻塞收发.md`，这里只说明它们**碰到哪些字段**。说明以头文件 Doxygen 为准。

**本篇不涉及：** 收发状态机细节见 `20-阻塞与非阻塞收发.md`；kmsg 载荷格式见 `21-kmsg与TLV序列化.md`；`ops_allow` 细则见 `22-Port-hook与准入.md`；MSQ/EBR 见 `18-支持IPC的无锁MSQ队列实现.md` / `17-EBR与线程资源回收.md`；`name_index` 见 `41-名称索引注册表.md`。

### 7.1 典型编排（从空系统到收完一条消息）

| 场景 | 顺序 | 主要动到的结构 |
|------|------|----------------|
| 初始化建表 | `global_port_init` | 分配 `global_port_table`，`by_name` 为空索引 |
| Server 公布具名 port | 先 `create_named_message_port(name, hooks, owner_list)`，再 `register_named_port` | 新的 `Named_Message_Port_t`（内部会合已是 OPEN）；表多持 1 份引用；名字可被查找；可选挂入 `owner_list` |
| Client 找到 port | `thread_lookup_port` | 可能写入 `port_cache`；返回带引用的 `Named_Message_Port_t*` |
| 创建消息并发送 | 依次 `create_message_data`、`create_message_with_msg`、`enqueue_msg_for_send`、`named_port_send_msg` | 新的 `Msg_Data_t` 与 `Message_t`；进入 `send_msg_queue`；在内部匿名会合上配对，或挂上 `Ipc_Request` |
| 接收消息并释放 | 先 `named_port_recv_msg`，再 `dequeue_recv_msg`，用完 `ref_put(msg)` | 从 `recv_msg_queue` 取出 `Message_t`；节点回收经 EBR 推迟 |
| 关闭 | `unregister_named_port`，各方再放下自己的 pin（create 须先摘链） | 先关掉内部匿名会合并清 `thread_queue`，再从名表摘掉；引用归零后释放具名对象 |
| 纯匿名（无名字） | `create_message_port(service_id, owner_list)` → `send_msg` / `recv_msg` → `close_port` → 摘链 + `ref_put(..., free_message_port_ref)` | 不经名表，也没有 hooks |

### 7.2 匿名 Port（`port.h`）与具名 Port（`named_port.h`）

**匿名：**

```c
error_t message_port_init(Message_Port_t *port, u32 service_id);
Message_Port_t *create_message_port(u32 service_id,
                                    struct list_entry *owner_list);
error_t close_port(Message_Port_t *port);
error_t free_message_port_ref(ref_count_t *);
bool port_ops_begin(Message_Port_t *port); /* ipc 内部：只看是否 OPEN */
void port_ops_end(Message_Port_t *port);
```

**具名：**

```c
error_t global_port_init(void);
Named_Message_Port_t *create_named_message_port(const char *name,
                                                const port_append_hooks_t *hooks,
                                                struct list_entry *owner_list);
void message_port_put_owner_ref(Message_Port_t *port);
void named_message_port_put_owner_ref(Named_Message_Port_t *port);
error_t register_named_port(struct Port_Table *table, Named_Message_Port_t *port);
error_t unregister_named_port(struct Port_Table *table, const char *name);
Named_Message_Port_t *port_table_lookup(struct Port_Table *, const char *name);
Named_Message_Port_t *port_table_lookup_with_token(...);
Named_Message_Port_t *port_table_resolve_token(...);
bool port_table_port_is_live(...);
error_t free_named_message_port_ref(ref_count_t *);
error_t named_port_send_msg(Named_Message_Port_t *port);
error_t named_port_recv_msg(Named_Message_Port_t *port);
error_t named_port_try_send_msg(Named_Message_Port_t *port);
error_t named_port_try_recv_msg(Named_Message_Port_t *port);
```

| 接口 | 对数据结构做了什么 |
|------|-------------------|
| `global_port_init` | 调用 `port_table_create()`，结果写入 `global_port_table`。得到一张空表，不预注册任何 port。失败则返回 `-E_RENDEZVOS`。 |
| `create_message_port` | 堆上分配匿名 `Message_Port_t` 并 init，`ops_life` 为 OPEN；`owner_list != NULL` 时挂入该链表头。没有名字，也没有 hooks。 |
| `create_named_message_port` | 堆上分配 `Named_Message_Port_t`（可带尾部 `append_info`）；内部匿名会合为 OPEN；`service_id` 由名字算出（不会是 0）；**此时还不在名表里**；可选挂 `owner_list`。若 `hooks->init` 失败则回滚。 |
| `message_port_put_owner_ref` / `named_message_port_put_owner_ref` | 先摘 `owner_link`，再 put create/ownership。只用于放下归属引用，不要用于 lookup / 表 pin。 |
| `register_named_port` | 持表锁：可选地跑 `ops_allow(REGISTER)`，再按 `name` 插入，写上 `table`，表对 port `ref_get` 一次。内部会合必须仍是 OPEN。重名且不是同一对象则失败。 |
| `unregister_named_port` | 先对内部匿名会合调用 `close_port`（OPEN 经 CLOSING 清队列后到 CLOSED），再从名表摘掉，放下表持有的那份引用。名字本来就不在表中时也返回成功。 |
| `port_table_lookup*` | 在名表里按名找到 port，给调用方一份引用，再跑 `ops_allow(LOOKUP)`；若拒绝则放下引用并返回 `NULL`。带 token 的变体把（行下标，代际）交给线程缓存用。 |
| `port_table_resolve_token` | 在锁下用缓存 token 加名字校验；成功同样取得引用；**每次**仍要过 LOOKUP。 |
| `named_port_send_msg` 等 | 先跑 `ops_allow`（发送或接收），通过后再进入内部匿名会合的 `send_msg` / `recv_msg` 或其 try 变体。拒绝则不进入会合。 |
| `port_ops_begin` / `end` | **匿名会合内部用**：要求当前为 OPEN，然后增加 `ops_count`；**不会**调用 `ops_allow`。阻塞等待前必须先 end。见 `20-阻塞与非阻塞收发.md` / `22-Port-hook与准入.md`。 |
| `free_message_port_ref` | 只用于匿名堆对象；引用归零后调用 `delete_message_port_structure`。 |
| `free_named_message_port_ref` | 具名对象的析构：先尽量保证内部会合已 CLOSED，再跑 `hooks->fini`，最后 `m_free`。不要对内部匿名部分调用 `free_message_port_ref`。 |

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
| `free_message_ref` | `Message_t` 引用归零时**不立刻**拆节点：交给 EBR 推迟（见 `17-EBR与线程资源回收.md`/`18-支持IPC的无锁MSQ队列实现.md`）。真正回收时 put `Msg_Data_t`，再 free 这条 `Message_t`。 |

拆成 Data / Message 的原因见 §1.3 / §4.2。

---

## 8. 多架构

Port 的对象模型与 ISA 无关；原子操作相关实现随 arch，语义相同。

---

## 9. 测试

`modules/test/` 下 port/ipc 相关；`make ARCH=x86_64 config && make run`。其中有 port 表查找，以及按 port 数据结构两收两发等测试用例。

---

## 10. 限制与后续

- 具名侧可以用 `port_append_hooks_t` / `ops_allow` 做准入（见 `22-Port-hook与准入.md`）；生产环境也可以一个 hook 都不挂。
- 按名对外服务的业务，收发请走 `named_port_send_msg` / `named_port_recv_msg` 一类封装，不要把具名 port 强转成匿名指针再直接调用 `send_msg` / `recv_msg`。
- **Owner 链表与 port 寿命（已接）：** `create_*` 显式 `owner_list`（任意链表头，常见为线程 `owned_*`，**不**绑定必须是 `Thread_Base`；`NULL`=不挂链）。`close_port` 不摘链。挂链⇔欠 create；放下 create 须先摘链再 put。线程若使用自带 `owned_*`，退出两阶段 close/unregister → unlink+put。IRQ / 无明确宿主时传 `NULL`。

---

## 11. 变更记录

- 2026-11-20：文档澄清——`owner_list` 是任意 `list_entry` 头，不必是 `Thread_Base`；线程 `owned_*` 只是常见宿主。对齐 create 签名、摘链再 put create、两阶段 exit、`named_message_port_put_owner_ref`。
- 2026-11-20：create_* 改为显式 owner（先 `owner_thread` 再改为 `owner_list`）；A3 简化——双链表、无锁、`close_port` 不摘链；退出两阶段；挂链⇔欠 create；get_or_create 成功后额外 `ref_get`。同日结构债清理——§3 具名步骤与 §6.1 / §6.2 近重复，§3 收为交叉引用；§3 关闭通知结论表保留原位。
- 2026-11-19：底子重评批次——篇首缩号；「在他的」→「它的」；`port_ptr` 衍字；§9 测例口径。
- 2026-11-19：匿名 / 具名定稿对齐——§3 / §4.0 / §4.1.1 / §4.4 / §5 / §6 / §7 改用具名创建、注册与 `named_port_*_msg`；`service_id` 为 `u32`；去掉 ACTIVE / REGISTERED 与旧 `register_port` 叙事；释放路径分清匿名与具名。同日按审阅意见润色：少用箭头串写步骤，少夹半截英文词。
- 2026-10-17：§1.2 / §4.3 写清 `Ipc_Request` 对 TCB 的 get/put 与节点自身 refcount；逐步时序指 `20-阻塞与非阻塞收发.md` §4.1。
- 2026-10-17：§4.1 补「为何需要 ops_life/ops_count」（Maintainer 修订稿收录）：与 port_cache、unregister 清理队列交叉，与会合无锁正交；并点出 `ops_allow` / capability·namespace 动机，细节仍指 `22-Port-hook与准入.md`。
- 2026-10-16：对照源码修正三处：配对后的 `Ipc_Request` 仍可能当 dummy；`unregister_port` 放下的是注册表那份 ref；transfer 成功后发送侧 `Message_t` 被 put。§6.4 陈旧 request 改为按 status / `port_ptr` 丢掉。
- 2026-10-15：§6 按审阅意见补强：恢复「先 unregister」、client 失败约定、§6.4 短清单；修正 6.3 transfer 后 Message_t 表述；清掉 §6.4 死链；「两种错误」改为「两种收尾形态」。
- 2026-10-15：§6 重写为「兼容层怎么用」：6.1 server / 6.2 client / 6.3 消息所有权 / 6.4 引用与关闭清单；去掉与 §4/§7/`20-阻塞与非阻塞收发.md` 重复的清理队列算法、transfer 摘要、lookup 实现逐步说明；§3 关闭表改为指向 §6 的用法约定。
- 2026-10-13：用语回调：去掉「数据袋 / 信封 / 收发件箱」等自造比喻，直接用 `Msg_Data_t` / `Message_t` / `send_msg_queue` 等；dummy / token / append 区保留英文惯用说法。
- 2026-10-13：§1–§3 润色；§3 关闭通知改为结论表，细节后置；§4 入口补阅读顺序与两套状态辨析。
- 2026-10-13：§1.1 / §4 对象总览 / `ops_life` / port 注册表等 ASCII 图改竖排，避免横版过宽。
- 2026-10-13：§4 重写为「对象分工 → 每个字段为何存在 → 结构体摘录」；去掉 FAM/FNV-1a 等未定义缩写与兼容层示例名；§4.5 逐字段说明线程收发箱；§7 改为「API 如何操作这些结构」，并加 §7.4 与收发 API 的字段衔接。
- 2026-10-13：§4 曾改回以头文件结构体摘录为主（后由同日上条重写覆盖）。
- 2026-10-12：§4.2 假出队因果只回指 `18-支持IPC的无锁MSQ队列实现.md` §1.4–1.5 / 本篇 §1.3（后由 2026-10-13 再补结构体正文）。
- 2026-10-10：文件名随 `18-支持IPC的无锁MSQ队列实现.md` 更名为 `18-支持IPC的无锁MSQ队列实现.md`。
- 2026-10-09：开篇与单状态交叉引用对齐 `18-支持IPC的无锁MSQ队列实现.md` §1 新结构（单状态见 §1.7）。
- 2026-10-05：§1.1 只留两层对象 ASCII（单状态 waiter）；发送时序图挪到 `20-阻塞与非阻塞收发.md`（本章不是收发状态机）。§10 演进转到 `20-阻塞与非阻塞收发.md` §10.1。
- 2026-10-04：补两层模型 ASCII 图（§1.1）与 `global_port_table` 结构示意图（§4.3）；为「假出队」首现处加名词解释；口语动词「塞/丢掉」改为「投/丢弃」；与 `22-Port-hook与准入.md` 的 token→LOOKUP 门关系补一句交叉引用。
- 2026-10-05：任务 1/3/5 精读——口语动词与翻译腔清理（睡死→永远阻塞、挂上→加入队列/投递/挂载、挪/搬走→迁移、拿到→取得、放掉→释放、晾在→滞留、挂死→阻塞不返回、摘表→从表中摘除、门闩→门禁、加门→加门禁、踩过坑→调试中遇到问题、勿想当然→不要想当然）；为首现缩写补全称（CAS、FNV-1a、LRU、endpoint）；为「自环」补简短定义；§6.3 标题与内容一致，无重复调整。
- 2026-10-05：最终词句顺畅。
- 2026-10-02：§1 按「先读 18§1 五图 → 本篇对象」重写：两层模型 / 单队列 / Msg 拆分都写成从设计框架往下推；§4.2 因果归无锁篇、本篇只写字段。
- 2026-10-02：§4.2 链无锁篇 §1.5 图解（`ipc_transfer_message.png`）。
- 2026-10-01：§6.2 补「为何不对称」因果表；明确 **不是** 无锁 flag 竞态补丁（kmsg = recv 成功路径上的可 dequeue 事件；flag = deliver 失败兜底）；与 `port.c` / `port.h` 注释对齐。
- 2026-09-27：中文表述润色（母语习惯）；「关港」改为「关闭 port」；「真源 / 契约」改为「以…为准 / 约定」；「不对称」改为更顺口的「通知方式不同」。
- 2026-09-26：对照 `port_clean_thread_queue` 纠正关闭 port 时唤醒两侧不同（send=flag；recv=优先 PORT_CLOSED kmsg，flag 仅 pending==0）；修正 unregister 序（先 CLOSING 再等 count）；§3/§6.2/§7.2 与 `port.h`/`thread.h`/`ipc.h` Doxygen 对齐。
- 2026-09-26：§7 全文审阅——按 `port.h`/`message.h` Doxygen 补接口说明；写清 create→register→unregister 与 `global_port_init` 编排；纠正旧稿签名省略。
- 2026-09-25：语言整理；标明无硬件寄存器依赖，与任务 / 无锁篇分工不变。
- 2026-08-29：整篇按源码+设计笔记重做——两步模型、单队列 vs 双队列事故、Msg_Data 拆分与 MSQ dummy 真相；动机与无锁篇分工。
- 2026-08-26：v0.1 初稿。
