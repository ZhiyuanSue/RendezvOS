# Port hook 与准入

v0.1 · 2026-09-27（2026-11-19：匿名 / 具名拆分定稿）

本篇覆盖：上层如何通过 **`port_append_hooks_t`** 注入 `init` / `fini` / `ops_allow`。这些回调**只挂在具名 port 上**。测试用例见 `modules/test/single_port_test.c`。

**本篇不重复：** 匿名 Port 与 `ops_life` / `ops_count` 见 `19` §4.1；具名 port 与名表见 `19` §4.1.1 / §4.4；send / recv 会合见 `20`；MSQ 见 `18`；kmsg 见 `21`；名称索引见 `41`。

---

## 1. 概述

有两道检查，不要混在一起：

1. **生命周期**（匿名侧的 `ops_life` 与 `ops_count`）——`close_port` 和正在进行的 send / recv 互斥。见 `19` §4.1。
2. **准入策略**（可选的 `ops_allow`，只在具名侧）——谁可以注册进名表、谁可以按名看见、谁可以经具名封装收发。

匿名侧的 `port_ops_begin` **只**看当前是不是 OPEN，**不会**调用 `ops_allow`。纯匿名 port（`create_message_port`）根本没有 hooks；调用方手里握着指针，就视为允许收发。

按名对外的业务，收发必须走 `named_port_send_msg` / `named_port_recv_msg` / `named_port_try_*`：它们在进入匿名会合之前，会先问一遍发送或接收是否允许。不要把 `Named_Message_Port_t*` 强转成 `Message_Port_t*` 再直接调用 `send_msg` / `recv_msg`，那样会跳过准入。

如果把 Linux namespace 一类策略硬塞进匿名 `send_msg`，core 就会绑死某一种安全模型。线程侧已有 `thread_append_hooks`；具名 port 侧做类似设计：一份 **`port_append_hooks_t`**（通常是静态表，经指针挂到具名 port 上），加上 **`Named_Message_Port_t.append_info[]`**（按 `append_info_len` 与具名对象同块分配），再配上 `init` / `fini` 与 **`ops_allow`**。

core 不内置 capability 之类的安全模型；策略放在外面，默认全放行。`hooks` 或 `ops_allow` 为 `NULL` 时，效果和「没有挂 hook」一样：一律放行。

---

## 2. 目标与边界

**提供：** 经 `port_append_hooks_t` 注入的准入与追加区生命周期回调；具名对象上如何挂 `append_hooks` 与 `append_info[]`；`named_port_*_msg` 在发送 / 接收前如何调用 `ops_allow`。

**不做：** 把 `port_ops_begin` / `end` 当成上层 API（那是匿名会合内部的步骤）；默认拒绝一切；在 hook 里阻塞或 `schedule`；给匿名的 `create_message_port` 绑 hooks。

---

## 3. `ops_allow` 何时被调用

本节只谈准入回调 **`ops_allow`**（为 `NULL` 表示全放行）。`init` / `fini` 只管尾部追加区的创建与销毁，见 §4.2，不参与下面的判定。

| 何时 | `op_type` | `lookup_name` | 拒绝时 |
|------|-----------|---------------|--------|
| `register_named_port`（持名表锁，入表前） | `REGISTER` | **必须为 NULL**（用 `port->name`） | 不入表；返回 hook 给出的 `error_t` |
| `port_table_lookup*` / `thread_lookup_port`（含 token 解析）命中并持有引用之后 | `LOOKUP` | **可以为非 NULL**（索引用的名字） | 放下这份引用，返回 `NULL`（具体错误码会被丢掉） |
| `named_port_send_msg` / `named_port_try_send_msg` | `SEND` | **必须为 NULL** | 不进入匿名 `send_msg`；返回 hook 给出的 `error_t` |
| `named_port_recv_msg` / `named_port_try_recv_msg` | `RECV` | **必须为 NULL** | 不进入匿名 `recv_msg`；返回 hook 给出的 `error_t` |

实现里，具名层在调用 hook **之前**会先校验参数：只有 `LOOKUP` 允许 `lookup_name` 非空；其它类型若带着非空名字指针，直接返回 `-E_IN_PARAM`，不会进 hook。这和拆分前「SEND / RECV 路径上恒传 `NULL`」的约定一致。

匿名侧的 `port_ops_begin`、`send_msg`、`recv_msg`、`ipc_try_*` **不会**再问一遍 allow。线程缓存的 token 每次解析时，仍要过 LOOKUP。

策略拒绝时，请优先返回 **`-E_REND_ACCESS`**，不要用 `-E_REND_PORT_CLOSED`——后者表示会合已经被关掉，见 §6。

---

## 4. 数据结构（本篇焦点：hooks）

匿名 Port 与 `ops_life` / `ops_count` 见 `19` §4.1。本篇只写 **具名对象上与 hook 相关的成员**，以及上层怎么注入。

### 4.1 具名对象上挂 hook 的字段

```c
struct Named_Msg_Port {
        /* PORT_COMMON 前缀 = 嵌入的匿名会合，见 19 §4.1 */
        struct Port_Table *table;
        char name[PORT_NAME_LEN_MAX];
        const struct port_append_hooks *append_hooks;
        char append_info[]; /* 长度 = append_hooks->append_info_len */
};
typedef struct Named_Msg_Port Named_Message_Port_t;
```

| 字段 | 含义 |
|------|------|
| `append_hooks` | 指向一份 **`port_append_hooks_t`**（上层通常给静态 const 表）。为 `NULL` 表示没有追加区，准入一律放行。创建时写入，之后不换表。 |
| `append_info[]` | 紧跟在定长部分后面的字节，**与具名 port 同块分配**。长度由 `append_hooks->append_info_len` 决定。core **不解释**里面的内容；`init` / `fini` / `ops_allow` 通过 `port->append_info` 自己使用。 |

分配大小由 `named_message_port_total_size(hooks)` 算出：`sizeof(Named_Message_Port_t)`，再加上 hooks 非空时的 `append_info_len`。

### 4.2 上层注入表：`port_append_hooks_t`

```c
enum port_ops_type {
        PORT_OPS_LOOKUP,
        PORT_OPS_REGISTER,
        PORT_OPS_SEND,
        PORT_OPS_RECV,
};

typedef error_t (*port_ops_allow_t)(Named_Message_Port_t *port,
                                    enum port_ops_type op_type,
                                    const char *lookup_name);

typedef struct port_append_hooks {
        size_t append_info_len;
        port_append_init_t init;
        port_append_fini_t fini;
        port_ops_allow_t ops_allow;
} port_append_hooks_t;
```

两层不要混：

| 对象 | 谁分配 | 角色 |
|------|--------|------|
| `port_append_hooks_t` | 上层（多为静态） | **函数指针表，外加追加区长度**；定长，**本身不是**尾部追加区 |
| `append_info[]` | `create_named_message_port` 与具名对象同块分配 | **每个 port 私有**的字节；寿命跟着具名 port |

同一份 hook 表可以挂到多个具名 port；私有数据则跟单个对象绑定。回调一律传入 `Named_Message_Port_t*`，用 `port->append_info` 取私有区。

```text
   port_append_hooks_t（通常静态）
   ┌──────────────┬───────┬───────┬────────────┐
   │append_info_  │ init  │ fini  │ ops_allow  │
   │len           │       │       │            │
   └──────────────┴───────┴───────┴────────────┘
                         │
                         │  创建具名 port 时写入 append_hooks
                         ▼
   Named_Message_Port_t（一次性按 named_message_port_total_size 分配）
   ┌────────────────────────────────────────┬──────────────────────────┐
   │  PORT_COMMON，以及 table / name / hooks │  append_info_len 字节     │
   │  append_hooks 指向上面那张表            │  append_info[]            │
   └────────────────────────────────────────┴──────────────────────────┘
```

`ops_allow` 返回 `REND_SUCCESS` 表示允许；其它 `error_t` 表示拒绝。调用时机见 §3；`lookup_name` 的规则也见 §3 的表。

### 4.3 与 `port_ops_begin` / `end`（匿名）

`port_ops_begin` / `end` 是 ipc 内部步骤，只管生命周期和 `ops_count`，**没有** `ops_allow`。顺序见 `19` §4.1：先确认是 OPEN，再增加 `ops_count`，然后再确认一次仍是 OPEN。阻塞路径必须先 `end` 再 `schedule`（见 `20`）。hooks 表只交给 **`create_named_message_port`**，不会交给匿名的 `create_message_port`。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `named_port.h` / `named_port.c` | hooks 类型、具名创建 / 注册 / 查找、按名收发封装、`ops_allow` 参数校验 |
| `port.h` / `port.c` | 匿名会合、`port_ops_begin`（不问 allow）、`close_port` |
| `ipc.c` | 匿名 `send_msg` / `recv_msg` / try（只经 begin，不问 allow） |
| `thread.c` 里的 `thread_lookup_port` | token 解析时仍过 LOOKUP |
| `single_port_test.c` | LOOKUP / REGISTER / SEND / RECV 拒绝，以及带 token 的查找 |

---

## 6. `ops_allow` 拒绝，和真的关掉 port，不是一回事

| ipc 路径 | 会不会跑发送 / 接收的 `ops_allow` |
|----------|-----------------------------------|
| `named_port_send_msg` / `named_port_recv_msg` / `named_port_try_*` | 会 |
| 匿名 `send_msg` / `recv_msg` / `ipc_try_*`（包括直接对着内部匿名会合调用） | **不会** |
| `ipc_system_try_deliver` | **不会**（走匿名 try；具名策略挡不住系统投递） |
| `ipc_system_deliver_to` | **不会**（清队列时注入 `PORT_CLOSED` 是刻意旁路） |

| | `ops_allow` 拒绝 | `unregister_named_port` 真正关闭 |
|--|------------------|-----------------------------------|
| port 还在名表里吗？ | **是**（是否还能被查到，取决于 LOOKUP 策略） | 否（先关掉内部匿名会合，再从名表摘掉） |
| 会给等待者打 `PORT_CLOSED` 消息或 flag 吗？ | **否** | 会（见 `19` §3；醒后返回值见 `20`） |
| 调用方看到什么 | 宜为 `-E_REND_ACCESS`（好和关闭区分开） | `-E_REND_PORT_CLOSED`，或接收成功后再 dequeue 到 closed 消息 |

因此：LOOKUP 拒绝只是「这次拿不到指针」；发送 / 接收拒绝只是「这次不进入会合」，port **并没有**被关掉。已经睡在 port 上的等待者醒过来之后，**不会**再跑一遍 `ops_allow`——要把他们叫醒并收尾，只能调用 `unregister_named_port`。

---

## 7. 上层怎么注入（不是 begin / end）

本篇对外要填的是 **`port_append_hooks_t`**，经具名创建入口挂上。说明以 `named_port.h` 的 Doxygen 为准；字段布局见 §4。

```c
Named_Message_Port_t *create_named_message_port(const char *name,
                                                const port_append_hooks_t *hooks,
                                                struct list_entry *owner_list);
```

`owner_list` 是可选归属链表头（任意 `list_entry`，常见为线程 `owned_named_ports`；语义见 `19` §4.1 / §6.4），与 hooks 正交。

| 上层填什么 | 说明 |
|------------|------|
| `append_info_len` | 具名对象尾部 `append_info` 多分配多少字节；0 表示没有追加区 |
| `init` / `fini` | 可选；创建结束时 / 释放前，用来初始化或清理 `append_info` |
| `ops_allow` | 可选；覆盖注册、查找、发送、接收（时机见 §3） |
| `hooks == NULL` | 和「没有 hook」相同：没有追加区，一律放行 |

之后：`register_named_port`、按名查找，以及 **`named_port_send_msg` 一类封装**。纯匿名路径继续用 `send_msg` / `recv_msg`，与 hooks 无关。

---

## 8. 多架构

与具体指令集无关。准入是软件回调，不碰硬件。

---

## 9. 测试

`single_port_test`：按名收发使用 `named_port_*_msg`；hook 测试用例覆盖 LOOKUP、带 token 的 LOOKUP、以及 SEND / RECV / REGISTER 被拒绝的情形（期望返回 `-E_REND_ACCESS`）。

```bash
cd core && make ARCH=x86_64 config && make run
```

---

## 10. 限制与后续

- 生产环境可以继续不挂 hooks；扩展点已经在，**不强制**上层使用。
- `REGISTER` 在持锁时回调，约束很严：回调里不要再对同一张表做 register / lookup，也不要 `schedule`。
- 系统投递路径不会问具名侧的发送 allow；若策略要挡住「任意投递到这个端点」，需要另做设计，不要假设 `try_deliver` 会问 hook。

---

## 11. 变更记录

- 2026-11-19：底子重评批次——核对：关闭通知锚点已随匿名/具名定稿改指 `19` §3/§4.1；本批无新增事实硬伤，评语覆盖。
- 2026-11-19：匿名 / 具名定稿——hooks 只挂具名；发送 / 接收经 `named_port_*_msg`；拒绝用 `-E_REND_ACCESS`；去掉「在 begin 里问 allow」的过渡说法；结构体与 API 对齐 `named_port.h`。同日按审阅意见润色用语。
- 2026-10-17：Maintainer 审定闭环；§4.3 补回 begin 内部顺序一句（供 §3 回指）。
- 2026-10-17：§6 收成「拒绝 ≠ 真关闭」+ 哪些 ipc 路径会跑发送 / 接收准入；不再以 begin 为叙事中心，关闭通知细节指回 `19` / `20`。
- 2026-10-17：§4 补 append 字段；§7 改为上层注入面；§3 再收；用语统一；纠正尾部追加区归属。
- 2026-10-04：补 hooks / 尾部追加区布局图与准入调用链。
- 2026-09-27：中文表述润色。
- 2026-09-26：写清拒绝 ≠ 关闭，以及 `deliver_to` 旁路；链到 `19` / `20`。
- 2026-08-29：整篇重做——外置叙述；纠正 REGISTER / `lookup_name`；生产零 hooks 诚实。
- 2026-08-27：初稿。
