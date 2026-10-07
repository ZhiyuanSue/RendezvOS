# Port 钩子与准入门

v0.1 · 2026-09-27

本篇覆盖：`kernel/ipc/port.c`、`include/rendezvos/ipc/port.h`（ops_life / ops_count / `port_append_hooks_t` / `port_ops_begin|end`）、以及 `ipc.c` 里对 begin 的调用点。测试用例：`modules/test/single_port_test.c`。

Port 对象与两层会合见 `19`；send / recv 阻塞时序、醒后 flag、orphan drop、PORT_CLOSED **行为细节**见 `20`；设计框架见 `18` §1；`KMSG_OP_SYSTEM_PORT_CLOSED` 登记见 kmsg 篇；名称索引见基础设施篇。

---

## 1. 概述

Port 热路径要同时做两件互不相干的事：

1. **生命周期门** — unregister 与正在进行的 send / recv 互斥（`ops_life` + `ops_count`）。
2. **策略门** — 谁可以 register / 看见名字 / send / recv。

若把 Linux credential（凭证）、namespace（命名空间）嵌入 `send_msg`，core 就会绑定一种安全模型。线程侧已有 `thread_append_hooks`，调度可换 `scheduler`——port 侧做对称设计：**`port_append_hooks_t`** FAM 尾区 + `init` / `fini` + **`ops_allow`**。

core 保持 capability-neutral（权能中立，不内置 capability 系统）；策略外置，默认全放行。

### 与 Linux 类似机制对照

Linux LSM（Linux Security Modules）是内核里通用的 hook 框架：每个安全模块把若干 `struct security_hook_list` 注册到全局 `security_hook_heads` 链表，运行时由 `security_hook_*` 入口遍历调用，做准入判定与审计。本仓库的 `port_append_hooks_t` 走的是另一条路：钩子不是全局链表，而是 per-port 的 FAM（柔性数组）尾区，在 `create_message_port` 时一次性绑定、运行时不再增删。差异本质在于——LSM 解决「跨子系统、可热插拔的安全策略叠加」，本仓库只解决「单个 port 的准入回调 + 私有上下文」，所以选了更轻、生命周期更紧的绑定方式。

`hooks=NULL` / `ops_allow=NULL` = 与「没钩子」时一样全放行。索引键仍是 **name 字符串**；actor（发起操作的主体）只靠 `get_cpu_current_thread()`，不传额外主体参数。

**现状：** 机制已冻结；**生产路径（linux_layer / servers）一律 `create_message_port(..., NULL)`**。非 NULL hooks **仅**测试用例安装。不是「compat 已在 ops_allow 做 credential」。

---

## 2. 目标与边界

**提供：** unregister vs begin 的互斥；统一 admission（准入，决定一次操作是否放行）回调；可选 FAM 尾区。

**不做：** 默认 deny；audit log；在 hook 里阻塞 / `schedule`（应快速返回）；把 MSQ 并发改成串行——`ops_count` **不**串行化 send / send。

---

## 3. 分层与调用方

| 场景 | 行为 |
|------|------|
| create | `create_message_port(name, hooks)` → 可选 `init` → 再 `register_port` |
| register | 表锁内 `ops_allow(REGISTER, NULL)` → 通过才 `name_index_register` |
| lookup | 表命中后 `ops_allow(LOOKUP, name)`；拒绝 → put + 返回 NULL（errno **丢弃**） |
| send / recv | `port_ops_begin`：life 须 REGISTERED → `ops_allow(SEND|RECV, NULL)` → `ops_count++` → 再查 life |
| unregister | 从表中摘除 → CLOSING → 等 `ops_count==0`（可 schedule）→ `port_clean_thread_queue` → CLOSED → put |

测试用例：`port_hook_gate_self_test` 等拆开 LOOKUP vs SEND、token 再门禁。

---

## 4. 数据结构与不变量

### 4.1 两套枚举（别混）

```c
enum port_ops_type {
        PORT_OPS_LOOKUP, PORT_OPS_SEND, PORT_OPS_RECV, PORT_OPS_REGISTER
};

/* ops_life：ACTIVE → REGISTERED → CLOSING → CLOSED */
```

`port_ops_begin` **只接受** SEND / RECV；传 LOOKUP / REGISTER 直接失败。

### 4.2 hooks

```c
typedef error_t (*port_ops_allow_t)(Message_Port_t *port,
                                    enum port_ops_type type,
                                    const char *lookup_name);

typedef struct port_append_hooks {
        size_t append_info_len;
        port_append_init_t init;
        port_append_fini_t fini;
        port_ops_allow_t ops_allow;
} port_append_hooks_t;
```

`port_append_hooks_t` 采用 **FAM（flexible array member，柔性数组成员）** 模式：结构体本身定长，调用方在分配时按 `append_info_len` 在结构体尾部追加一段连续字节，作为钩子的私有上下文（如 credential 缓存、namespace 指针等）。`create_message_port` 一次分配 `sizeof(port_append_hooks_t) + append_info_len`，`init` 回调拿到这段尾区指针初始化，`fini` 反向清理。这样钩子无需独立二次分配，也避免钩子数据与 port 生命周期错位。

钩子表与 FAM 尾区布局示意：

```text
   port_append_hooks_t（由 create_message_port 一次性分配）
   ┌────────────────────────────────────────┬──────────────────────────┐
   │  sizeof(port_append_hooks_t)           │  append_info_len 字节     │
   │  ┌──────────────┬───────┬───────┐       │  （FAM 尾区，钩子私有）   │
   │  │append_info_  │ init  │ fini  │       │  credential / namespace   │
   │  │len           │       │       │       │  缓存等                   │
   │  ├──────────────┴───────┴───────┤       │                           │
   │  │  ops_allow (函数指针)        │       │                           │
   │  └──────────────────────────────┘       │                           │
   └────────────────────────────────────────┴──────────────────────────┘
   ▲ init / fini / ops_allow 均可经 container_of 取得尾区
```

| `lookup_name` | 何时 |
|---------------|------|
| 非 NULL | **仅** `PORT_OPS_LOOKUP`（索引键） |
| NULL | SEND / RECV / **REGISTER** |

（旧稿写「REGISTER 传 name」是错的。）

`ops_allow`：`REND_SUCCESS` 放行；其它 `error_t` 拒绝。

### 4.3 ops_life / ops_count

| 状态 | 含义 |
|------|------|
| ACTIVE | 已 create，未入表；不可 begin |
| REGISTERED | 在表；可 begin |
| CLOSING | unregister 后；不可 begin |
| CLOSED | 清完 thread_queue |

不变量：

- 仅 REGISTERED 可 begin；失败路径 **不得** `ops_count++`。
- begin 后双重检查仍 REGISTERED，否则 dec 并失败。
- 阻塞 send / recv：**`schedule` 前必须 `port_ops_end`**（否则 unregister 等 count 会永远等待）。
- 清队必须在 unregister 路径、**不能**推迟到 final free（见 port 注释 deadlock case）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `port.h` / `port.c` | life、begin / end、hooks、clean_thread_queue |
| `ipc.c` | 四入口先 begin；失败折叠 errno |
| `thread.c` `thread_lookup_port` | token 也走 LOOKUP 门 |
| `single_port_test.c` | 唯一非 NULL hooks 生产式用法 |

---

## 6. 流程

### 6.1 四门 call site

| Gate | 调用点 | 拒绝效果 |
|------|--------|----------|
| `init` | create 末尾 | create 返回 NULL（不调 fini，直接 free） |
| `fini` | `delete_message_port_structure` 开头 | — |
| `ops_allow(REGISTER, NULL)` | `register_port`，**持表 MCS 锁**，register 前 | 返回 hook 的 `error_t` |
| `ops_allow(LOOKUP, name)` | `port_lookup_finish`（含 resolve_token） | put + NULL |
| `ops_allow(SEND/RECV, NULL)` | `port_ops_begin` | begin false → ipc **统一** `-E_REND_PORT_CLOSED` |

REGISTER 在表锁内调 hook：**禁止**在 hook 里同表 lookup / register（死锁）。

四门准入调用链（自上而下，左侧为入口，右侧为门结果）：

```text
   create_message_port(name, hooks)
        │
        └─► hooks.init? ──失败──► 直接 free，返回 NULL
                          ──成功──► （继续 register_port）

   register_port(table, port)  ──持表 MCS 锁──►
        │
        ├─► ops_allow(REGISTER, NULL) ──拒绝──► 返回 hook 的 error_t
        └─► name_index_register  ──成功──► ACTIVE → REGISTERED；表持一 ref

   thread_lookup_port / port_table_lookup*  ──表命中、bump ref──►
        │
        └─► ops_allow(LOOKUP, name) ──拒绝──► ref_put + 返回 NULL
                                  ──放行──► 返回 port（持 ref）

   send_msg / recv_msg / ipc_try_*  ──► port_ops_begin(SEND|RECV)
        │
        ├─► life != REGISTERED ──► false（ipc 折成 -E_REND_PORT_CLOSED）
        ├─► ops_allow(SEND|RECV, NULL) ──拒绝──► false（同上）
        ├─► ops_count++
        └─► 再查 life 仍 REGISTERED ──否则 dec + false──► 进入会合路径
   ……会合/transfer…… ──► port_ops_end（ops_count--）；阻塞路径先 end 再 schedule
```

### 6.2 send / recv（ipc）

`send_msg` / `ipc_try_send_msg` → begin(SEND)；失败时 drop orphan + `-E_REND_PORT_CLOSED`。
`recv_msg` / `ipc_try_recv_msg` → begin(RECV)；失败直接 CLOSED。

成功：match 后 end；阻塞：enqueue 成功后 **先 end 再 schedule**；醒后看 `THREAD_FLAG_IPC_PORT_CLOSED`。

`ipc_system_try_deliver` → 内部 try_send → **会**过 SEND 门。
`ipc_system_deliver_to` → 直接打入线程队列 → **不**过 port_ops（清队注入用这条）。

### 6.3 与真关闭的差异

| | ops_allow deny | unregister 清队（**send/recv 等待者都 ready**，通知方式不同） |
|--|----------------|--------------------------------------------------|
| 注入 PORT_CLOSED kmsg | 否 | **仅**当时 `block_on_receive`（`deliver_to`；成功则 pending↑） |
| 置 `IPC_PORT_CLOSED` flag | 否 | **send 等待者恒置**；recv **仅当**清队后 `recv_pending_cnt==0`（投递失败） |
| 对外 errno / 返回 | SEND / RECV 伪装 CLOSED | send：`-E_REND_PORT_CLOSED`；recv：有 kmsg → `SUCCESS`+dequeue opcode，有 flag → CLOSED |

**Deny ≠ 关闭 port**：port 仍 REGISTERED；LOOKUP deny 只是「这次拿不到」。已 begin 的阻塞等待者醒后**不再**跑 ops_allow——真正关闭靠 unregister。清队权威表见 `19` §6.2；阻塞 API 醒后约定见 `20` §6.2。

LOOKUP 通过 ≠ SEND 通过（测试用例刻意拆开）。Token 缓存**不能**绕过 LOOKUP——每次 resolve 再门禁。

---

## 7. 公开 API

本篇涉及的接口分布在：准入 / 生命周期门面——`port_append_hooks_t`、`port_ops_allow_t`、`port_ops_begin` / `end`、`PORT_OPS_LIFE_*` / `port_ops_type`。说明以 `port.h` Doxygen 为准（已与 `.c` 核对）。

**本篇不涉及：** port 创建/表/unregister 清队过程细节 → `19`（本篇只钉门禁交汇）；send/recv 阻塞状态机 → `20`；kmsg PORT_CLOSED 载荷 → `21`。

生产路径今日一律 `create_message_port(..., NULL)`；非 NULL hooks **仅**测试用例。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 带 hooks 创建 | `create_message_port(name, hooks)` → 可选 **`init`**（失败则 create NULL，不调 fini）→ `register_port` |
| 注册 | 表锁内 **`ops_allow(REGISTER, NULL)`** → index 注册（ACTIVE→REGISTERED） |
| 查找 | 表命中 bump ref → **`ops_allow(LOOKUP, name)`**；拒绝 put→NULL |
| send/recv | **`port_ops_begin(SEND\|RECV)`** → … → match 后 **`port_ops_end`**；阻塞路径 **先 end 再 `schedule`** |
| 销毁 | unregister 清队 → last ref → **`fini`** → free |

### 7.2 Hooks 与枚举

```c
enum port_ops_type {
        PORT_OPS_LOOKUP, PORT_OPS_SEND, PORT_OPS_RECV, PORT_OPS_REGISTER
};

typedef error_t (*port_ops_allow_t)(Message_Port_t *port,
                                    enum port_ops_type op_type,
                                    const char *lookup_name);

typedef struct port_append_hooks {
        size_t append_info_len;
        port_append_init_t init;
        port_append_fini_t fini;
        port_ops_allow_t ops_allow;
} port_append_hooks_t;
```

| 接口 / 字段 | 说明 |
|-------------|------|
| `ops_allow` | `REND_SUCCESS` 放行。`lookup_name` **仅 LOOKUP 非 NULL**；SEND/RECV/**REGISTER 恒 NULL**。REGISTER 在表 MCS 锁内——禁同表重入。 |
| `init` / `fini` | create 末 / `delete_message_port_structure` 初。 |
| `append_info_len` | FAM 尾区字节数。 |
| `port_ops_type` vs `PORT_OPS_LIFE_*` | 门种类 ≠ 生命周期；**不要混**。 |

### 7.3 begin / end 与 life

```c
bool port_ops_begin(Message_Port_t *port, enum port_ops_type op_type);
void port_ops_end(Message_Port_t *port);
```

| 接口 | 说明 |
|------|------|
| `port_ops_begin` | 只接受 SEND/RECV。序：REGISTERED → `ops_allow` → `ops_count++` → 再查 REGISTERED。失败不增 count。ipc 把 false 折成 `-E_REND_PORT_CLOSED`。 |
| `port_ops_end` | `ops_count--`。阻塞前必调。 |
| `PORT_OPS_LIFE_*` | ACTIVE→REGISTERED→CLOSING→CLOSED（见 `19` unregister 编排序）。 |

`ops_count` **不**串行化并发 send；只挡 unregister。

### 7.4 与 ipc / 真关闭的交汇

| 路径 | 过门？ |
|------|--------|
| `send_msg` / `recv_msg` / `ipc_try_*` | begin(SEND/RECV) |
| `ipc_system_try_deliver` | 经 try_send → **会**过 SEND |
| `ipc_system_deliver_to` | **不**过 port_ops（清队注入用此） |

**Deny ≠ 关闭 port**：无 PORT_CLOSED kmsg / flag；port 仍 REGISTERED。LOOKUP deny ≠ SEND deny（可拆开测）。Token resolve **每次**再跑 LOOKUP。真正关闭时 send/recv 等待者都醒，但 send=flag、recv=优先 kmsg（`19` §6.2）。

---

## 8. 多架构

与 ISA 无关。门禁是软件回调，不碰中断控制器或页表。

---

## 9. 测试

`single_port_test` 内 hook gate / token lookup 等。

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

---

## 10. 限制与后续

- 生产未接入 hooks；扩展点已在，**不强制**上层使用。
- SEND / RECV deny 与真关闭不可区分。
- REGISTER 持锁约束严格。
- 将来若挂 deny-SEND，连 `ipc_system_try_deliver` 也会被挡；`deliver_to` 不会。

---

## 11. 变更记录

- 2026-10-04：补 `port_append_hooks_t` 与 FAM 尾区布局图（§4.2，含 FAM 概念解释）与四门准入调用链 ASCII 图（§6.1）。
- 2026-10-05：任务 1/3/5 精读——口语词与翻译腔清理（焊进→嵌入、绑死→绑定、摘表→从表中摘除、死等→永远等待、拖到→推迟到、直打→直接打入、拿到→取得、勿混→不要混）；为首现英文术语补释义（capability-neutral、actor、admission）；多处「测例」→「测试用例」。
- 2026-10-05：补「与 Linux 类似机制对照」小节；最终词句顺畅。
- 2026-09-27：中文表述润色（母语习惯）；「关港」改为「关闭 port」；「真源 / 契约」改为「以…为准 / 约定」；两侧通知差异表述更顺口。
- 2026-09-26：§6.3 / §7.4 钉死关闭 port 清队时两侧通知不同（两侧 ready；kmsg 仅 recv；flag 规则）并链 `19`/`20`；与 `port.h` unregister Doxygen 一致。
- 2026-09-26：§7 全文审阅——强化 `ops_allow` / `port_ops_begin` Doxygen（REGISTER 锁内、deny≠关闭 port、begin 四步）；写清编排与 deliver_to 旁路。
- 2026-09-25：语言整理；与阻塞篇 / 清队边界不变。
- 2026-08-29：整篇重做——外置叙述；纠正 REGISTER / `lookup_name`；生产零 hooks 诚实；deny vs 真关闭；四门表；与清队 / `deliver_to` 边界。
- 2026-08-27：初稿。
