# Port hook 与准入

v0.1 · 2026-09-27

本篇覆盖：上层如何经 **`port_append_hooks_t`** 注入 `init` / `fini` / `ops_allow`，以及这些回调挂在 `Msg_Port` 的哪两个字段上、在哪些路径被 core 调用。测试用例：`modules/test/single_port_test.c`。

**本篇不重复：** Port 对象与 `ops_life`/`ops_count` 动机与状态机见 `19` §4.1；send/recv 阻塞时序、醒后 flag、orphan drop 见 `20`；无锁 IPC / MSQ 见 `18`；`KMSG_OP_SYSTEM_PORT_CLOSED` 载荷见 `21`；名称索引见 `41`。清理队列两侧通知权威表见 `19` §3 / §6.2。

---

## 1. 概述

Port 热路径上有两道互不相干的检查：

1. **生命周期**（`ops_life` + `ops_count`）— unregister 与正在进行的 send/recv 互斥。动机与状态机相关详述在 `19` §4.1；本篇只写它怎样挂上 `port_ops_begin`，以及与第 2 条怎样交汇。
2. **准入策略**（可选 `ops_allow`）— 谁可以 register / 看见名字 / send / recv。

但是实际上，他们都可以表达为一件事情，是否允许使用这个port，是否允许这个消息的接收和发送，就是一个准入，所以我们将这两者糅合在一起。

在数据结构上，若试图把 Linux namespace（命名空间）等内容嵌入 `send_msg`，core 就会绑定一种安全模型。线程侧已有 `thread_append_hooks`（静态 hook 表指针 + 线程对象尾部追加区），port 侧做类似设计：**`port_append_hooks_t`**（通常一份静态表，经指针挂到 port）+ **`Message_Port_t.append_port_info[]`**（按 `append_info_len` 与 port 同块分配的尾部追加区）+ `init` / `fini` + **`ops_allow`**等相关操作。

在策略与机制划分上，core 保持 capability-neutral（权能中立，不内置 capability 系统）；策略外置，默认全放行。`hooks=NULL` / `ops_allow=NULL` = 与「没有 hook」时一样全放行。

---

## 2. 目标与边界

**提供：** 经 `port_append_hooks_t` 注入的准入 / 追加区生命周期回调；`Msg_Port` 上 `append_hooks` + `append_port_info[]` 的挂法。

**不做：** 把 `port_ops_begin` / `end` 当成上层 API（那是 core 内部步骤）；默认 deny；在 hook 里阻塞 / `schedule`。

---

## 3. `ops_allow` 何时被调用

本节只谈准入回调 **`ops_allow`**（`NULL` = 全放行）。`init` / `fini` 只管尾部追加区的创建与销毁，见 §4.2，不参与下面的判定。

从「port 可用」往下看，只有三处会问 `ops_allow`：

| 何时 | 参数 | 拒绝时 |
|------|------|--------|
| `register_port`（持注册表 MCS 锁，入表前） | `(REGISTER, NULL)` | 返回 hook 的 `error_t`，port 不入表。hook 内禁止再同表 lookup/register（死锁）。 |
| 按名 / token 查到 port 之后（`port_lookup_finish`） | `(LOOKUP, name)` | put 掉引用，返回 NULL；errno **丢弃**。 |
| `send_msg` / `recv_msg` / `ipc_try_*` 进会合前（在 `port_ops_begin` 里） | `(SEND)` 或 `(RECV)`，`lookup_name=NULL` | begin 失败；ipc 一律折成 `-E_REND_PORT_CLOSED`。 |

LOOKUP 与 SEND 可分别拒绝（测试用例拆开测）。token 缓存每次 resolve 仍过 LOOKUP，不能绕过。`port_ops_begin` 里在 `ops_allow` 前后还有 life / `ops_count` 步骤，见 §4.3；unregister 与会合见 `19` / `20`。

---

## 4. 数据结构（本篇焦点：hooks）

Port 整体与 `ops_life` / `ops_count` 等字段见 `19` §4.1。本篇只把 **与 hook 相关的两个成员** 写清楚，并给出上层注入用的表类型。

### 4.1 `Msg_Port` 上挂 hook 的两个字段

```c
struct Msg_Port {
        /* … thread_queue / refcount / table / name / service_id … 见 19 */
        atomic64_t ops_life;
        atomic64_t ops_count;
        const struct port_append_hooks *append_hooks; /* 指向 hook 表；可为 NULL */
        char append_port_info[]; /* 尾部追加区；长度 = append_hooks->append_info_len */
};
typedef struct Msg_Port Message_Port_t;
```

| 字段 | 含义 |
|------|------|
| `append_hooks` | 指向一份 **`port_append_hooks_t`**（上层通常给静态 const 表）。`NULL` = 无追加区、准入一律放行。create 时写入，之后不换表。 |
| `append_port_info[]` | 紧跟在 `Msg_Port` 定长部分之后的字节；**与 port 同块分配**。长度由 `append_hooks->append_info_len` 决定（hooks 为 NULL 或长度为 0 则无这段）。core **不解释**内容；`init` / `fini` / `ops_allow` 经 `port->append_port_info` 自行使用。 |

相应的分配大小为 `message_port_total_size(hooks)` = `sizeof(Message_Port_t) + (hooks ? hooks->append_info_len : 0)`。

### 4.2 上层注入表：`port_append_hooks_t`

```c
enum port_ops_type {
        PORT_OPS_LOOKUP, PORT_OPS_SEND, PORT_OPS_RECV, PORT_OPS_REGISTER
};

typedef error_t (*port_ops_allow_t)(Message_Port_t *port,
                                    enum port_ops_type op_type,
                                    const char *lookup_name);

typedef struct port_append_hooks {
        size_t append_info_len;   /* 告诉 create：port 尾部要多分配多少字节 */
        port_append_init_t init;  /* 可选；create 末调用 */
        port_append_fini_t fini;  /* 可选；真正 free 前调用 */
        port_ops_allow_t ops_allow; /* 可选；准入，见 §3 */
} port_append_hooks_t;
```

两层不要混：

| 对象 | 谁分配 | 角色 |
|------|--------|------|
| `port_append_hooks_t` | 上层（多为静态） | **函数指针表 + 追加区长度**；定长，**本身不是**尾部追加区 |
| `append_port_info[]` | `create_message_port` 与 port 同块分配 | **每个 port 私有**的字节；生命周期跟 port |

**为什么这样拆：** 同一份 hook 表可以挂到多个 port（从而让多个port具有相同的准入管理机制）；私有数据则与单个 port 绑定。回调一律传入 `Message_Port_t*`，用 `port->append_port_info` 取私有区，不必对 hooks 做 `container_of`（因为可能一个hook对应多个port），也没有 hooks 相应的生命周期问题。

```text
   port_append_hooks_t（通常静态）
   ┌──────────────┬───────┬───────┬────────────┐
   │append_info_  │ init  │ fini  │ ops_allow  │
   │len           │       │       │            │
   └──────────────┴───────┴───────┴────────────┘
                         │
                         │  create 时写入 mp->append_hooks
                         ▼
   Message_Port_t（一次性分配 message_port_total_size）
   ┌────────────────────────────────────────┬──────────────────────────┐
   │  定长部分（含 ops_life / ops_count）     │  append_info_len 字节     │
   │  append_hooks ──────────►（上表）       │  append_port_info[]       │
   └────────────────────────────────────────┴──────────────────────────┘
```

`ops_allow` 返回 `REND_SUCCESS` 允许准入；其它 `error_t` 拒绝。调用时机见 §3。

`ops_allow` 参数中的 `port_ops_type`的几种使用情况见 §3。

`ops_allow` 参数中的 `lookup_name`的区别如下。

| `lookup_name` | 何时 |
|---------------|------|
| 非 NULL | **仅** `PORT_OPS_LOOKUP`（索引键） |
| NULL | SEND / RECV / **REGISTER** |

### 4.3 与 `port_ops_begin` / `end` 的交汇（core 内部）

`port_ops_begin` / `port_ops_end` **不是**给上层注入用的接口；它们是 ipc 收发路径里包住 life/`ops_count` 的内部步骤，SEND/RECV 的 `ops_allow` 挂在 begin 里（见 §3）。上层只需填好 `port_append_hooks_t` 并交给 `create_message_port`。

begin 内部顺序（life 全貌见 `19` §4.1）：须已 REGISTERED → `ops_allow(SEND|RECV)` → `ops_count++` → 再确认仍 REGISTERED；失败路径不得留下 count。阻塞路径须先 `port_ops_end` 再 `schedule`（见 `20`）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `port.h` | `port_append_hooks_t`、`Msg_Port` 两字段、`create_message_port` |
| `port.c` | create 绑 hooks / 调 `init`；register·lookup·begin 里调 `ops_allow`；destroy 调 `fini` |
| `ipc.c` | send/recv/try 进会合前走 `port_ops_begin`（内部会调 SEND/RECV 的 `ops_allow`） |
| `thread.c` `thread_lookup_port` | token resolve 仍过 LOOKUP 的 `ops_allow` |
| `single_port_test.c` | 唯一非 NULL hooks 用法 |

---

## 6. `ops_allow` deny 与真关闭不是一回事

§3 已写 REGISTER / LOOKUP / SEND·RECV **何时**调 `ops_allow`。这里只补两点：哪些 ipc 路径会碰到 SEND/RECV 准入，以及 **deny 不等于 unregister**。

| ipc 路径 | 会不会跑 SEND/RECV 的 `ops_allow` |
|----------|-----------------------------------|
| `send_msg` / `recv_msg` / `ipc_try_*` | 会 |
| `ipc_system_try_deliver` | 会（内部走 try_send，过 SEND） |
| `ipc_system_deliver_to` | **不会**（清理队列时给阻塞 recv 注入 PORT_CLOSED 走这条，刻意旁路） |

| | `ops_allow` 拒绝 | `unregister` 真关闭 |
|--|------------------|---------------------|
| port 还在表里、仍是 REGISTERED？ | **是** | 否（见 `19`） |
| 给等待者打 PORT_CLOSED kmsg / flag？ | **否** | 是（怎么通知见 `19` §3；醒后怎么返回见 `20`） |
| SEND/RECV 调用方看到的错误 | 也是 `-E_REND_PORT_CLOSED`（与真关闭**不可区分**） | 见 `20` §6.2 |

因此：LOOKUP 拒绝只是「这次拿不到指针」；SEND/RECV 拒绝只是「这次进不了会合」，port **并没有**被关掉。已经睡在 port 上的等待者醒后**不会**再跑一遍 `ops_allow`——要把他们叫醒并收尾，只能 `unregister`（清理队列细节见 `19`）。LOOKUP 与 SEND 可否分别拒绝、token 是否每次再过 LOOKUP：见 §3。

---

## 7. 上层注入调用方式（不是 begin/end）

本篇对外要填的是 **`port_append_hooks_t`**，经创建入口挂上。说明以 `port.h` Doxygen 为准；字段布局见 §4。

```c
Message_Port_t *create_message_port(const char *name,
                                    const port_append_hooks_t *hooks);
```

| 上层填什么 | 说明 |
|------------|------|
| `append_info_len` | port 尾部 `append_port_info` 多分配的字节数；0 = 无追加区 |
| `init` / `fini` | 可选；创建末 / 释放前，操作 `append_port_info` |
| `ops_allow` | 可选；REGISTER / LOOKUP / SEND / RECV 准入（时机见 §3） |
| `hooks == NULL` | 与「没有 hook」相同：无追加区、一律放行 |

之后仍走既有的 `register_port` / lookup / `send_msg` / `recv_msg`，这几个注入的函数和相关信息，会在上述提到的路径上被调用到，用于判定是否可以准入。

---

## 8. 多架构

与 ISA 无关。准入是软件回调，不碰任何硬件相关的部分。

---

## 9. 测试

`single_port_test` 内包括了 hook gate / token lookup 等。

```bash
cd core && make ARCH=x86_64 config && make run
```

---

## 10. 限制与后续

- 生产未接入 hooks；扩展点已在，**不强制**上层使用。
- SEND / RECV deny 与真关闭不可区分。
- REGISTER 持锁约束严格。
- 将来若挂 deny-SEND，连 `ipc_system_try_deliver` 也会被挡；`deliver_to` 不会。

---

## 11. 变更记录

- 2026-10-17：Maintainer 审定闭环；§4.3 补回 begin 内部顺序一句（供 §3 回指）。
- 2026-10-17：§6 收成「deny ≠ 真关闭」+ 哪些 ipc 路径跑 SEND/RECV 准入；不再以 begin 为叙事中心，关闭通知细节指回 `19`/`20`。
- 2026-10-17：§4 补 `Msg_Port` 的 `append_hooks` / `append_port_info[]`；§7 改为上层注入面（`port_append_hooks_t` + `create_message_port`），明确 begin/end 非对外接口。
- 2026-10-17：§3 再收——只写 `ops_allow` 三处调用；`init`/`fini` 归 §4；去掉与表重复的 ASCII。
- 2026-10-17：用语统一——不用 FAM / call site /「四门」/「钩子」；改称 hook、尾部追加区；文件名改为 `22-Port-hook与准入.md`。
- 2026-10-17：删「与 Linux 类似机制对照」；去掉与 `19`/`20` 及篇内重复。
- 2026-10-17：纠正尾部追加区在 **`Message_Port_t.append_port_info`**，不在 `port_append_hooks_t`；hook 表多为静态指针。
- 2026-10-17：§1 概述补生命周期短述并回指 `19` §4.1。
- 2026-10-04：补 hooks / 尾部追加区布局图与准入调用链。
- 2026-10-05：口语词与翻译腔清理；为首现英文术语补释义。
- 2026-09-27：中文表述润色。
- 2026-09-26：写清 deny≠关闭与 `deliver_to` 旁路；链 `19`/`20`。
- 2026-09-25：语言整理。
- 2026-08-29：整篇重做——外置叙述；纠正 REGISTER / `lookup_name`；生产零 hooks 诚实。
- 2026-08-27：初稿。
