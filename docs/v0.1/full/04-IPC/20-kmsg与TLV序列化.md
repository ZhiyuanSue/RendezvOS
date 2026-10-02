# kmsg 与 TLV 序列化

v0.1 · 2026-09-27

本篇覆盖：`kernel/ipc/kmsg.c`、`kernel/ipc/ipc_serial.c`、`include/rendezvos/ipc/kmsg.h`、`include/rendezvos/ipc/kmsg_system.h`、`include/rendezvos/ipc/ipc_serial.h`。

`Msg_Data` / Port 会合见 `18-Port与消息模型.md`；send / recv 与 system 投递见 `19-阻塞与非阻塞收发.md`；MSQ / EBR 见无锁篇；compat RPC（追加 `'t'`、coop）属上层文档，本篇只钉 core 边界。

---

## 1. 概述

Port 只接通「谁」；`Message_t` 只是队列壳。业务还需要「哪种操作、带哪些参数」——若每个 server 自造二进制布局，协议会碎。

kmsg 是**可选的结构化信封**：

- 固定头 `kmsg_hdr_t` + **`ipc_serial` TLV**（文档里常叫 TLV；API 名是 `ipc_serial`）
- 打成 `Msg_Data_t`，`msg_type == MSG_DATA_TAG_KMSG`
- **不是** RPC、不路由、不解释 compat opcode

| 字段 | 含义 |
|------|------|
| `module` | 惯例 = 目标 port 的 `service_id`（快检「是不是给我的」）；**路由仍靠 port 名** |
| `opcode` | 操作身份；system 在 `kmsg_system.h`，compat 各自编号 |
| `payload` | TLV；`(opcode 语境下的) fmt` 两端必须一致——编译器不查 |

无 in-band version：layout 变就 **bump `KMSG_MAGIC` 并全量同步**。适合同镜像内核协作，不适合开放跨版本 wire。

拆 `ipc_serial` / `kmsg`：前者只管字节流；后者只管 hdr + 打进 `Msg_Data`。热路径一次分配 hdr + payload，payload 区直接 `encode_into`。

---

## 2. 目标与边界

**提供：** 紧凑 TLV 编解码；kmsg 信封；少量 system opcode 登记。

**不做：** schema 注册 / JSON / capability；在 `kmsg_create` 里查 port 或自动填 reply；解释具体 compat opcode；跨架构 wire 保证（`memcpy` 本机 endian，`p` 随指针宽）。

`'t'` 只是 **wire 钩子**（语义约定 = 全局表里的 reply port 名）；命名语法归 compat，本篇不规定。

---

## 3. 分层与调用方

**造消息：** `kmsg_create(module, op, fmt, …)` → `create_message_with_msg` → `ref_put(Msg_Data)` → enqueue → `send_msg`。

**收：** `recv` → dequeue → `kmsg_from_msg` → 看 opcode → `ipc_serial_decode(payload, len, fmt, …)`。

**core 生产点很少：** `port.c`（PORT_CLOSED）、`time.c`（timer）、`powerd`（shutdown）。海量 call site 在 linux_layer / servers。

**测例注意：** `single_ipc_test` / port / smp IPC 多用**裸** `create_message_data`；真正走 kmsg 的 core 测例主要是 timer 等。不是「IPC 测例都用 kmsg」。

解码得到的 `s` / `t` 指针指向 **message buffer 内**；异步处理须拷贝。

---

## 4. 数据结构与不变量

### 4.1 信封

```c
#define MSG_DATA_TAG_KMSG 1
#define KMSG_MAGIC 0x47534d4cu   /* 递增地址字节：'L','M','S','G' — 不是 'MSG' */
#define KMSG_MAX_PAYLOAD PAGE_SIZE

typedef struct {
        u32 magic;
        u16 module;
        u16 opcode;
        u32 payload_len;
} kmsg_hdr_t;          /* 12 字节，无尾填充 */

typedef struct {
        kmsg_hdr_t hdr;
        u8 payload[];
} kmsg_t;
```

`msg_type` 只表示 **carrier 布局是 kmsg**；操作身份是 `(module, opcode)`。其它 tag + 裸缓冲合法（测例常用）。

### 4.2 TLV wire（`ipc_serial`）

```text
u32 param_count
×N:  u8 type_tag | u32 value_len | u8 value[value_len]
```

| 字符 | encode 入参 | decode 出参 | wire |
|------|-------------|-------------|------|
| `p` | `void*` | `void**` | `sizeof(void*)` |
| `q` | `i64` | `i64*` | 8 |
| `i` | `int`→写 `i32` | `i32*` | 4 |
| `u` | `u32` | `u32*` | 4 |
| `s` | `char*` | `char**`（缓冲内） | `strlen+1`（**含 NUL**）；`NULL`→len0→指针 NULL |
| `t` | 同 `s` | 同 `s` | 同形；**语义** = reply port 名 |

空白在 fmt 中忽略。**encode 的 tag 与 decode 的 fmt 字符须逐字节相等**——`'t'` 编的不能用 `'s'` 解。

空 `""` 仍有 **4 字节** `param_count=0`（不是「无 payload」）。`""` 字符串 len=1 指向 `'\0'`；与 `NULL` 不同。

### 4.3 `kmsg_from_msg` 校验

查：非空、`msg_type==KMSG`、缓冲 ≥ hdr、magic、`payload_len == data_len - 12`。
**不**查 module / opcode / TLV 合法性（接收方自比、自 decode）。

### 4.4 所有权

`kmsg_create`：分配 hdr + payload → encode → `create_message_data(..., free_msgdata_ref_default)`，refcount=1。失败 `NULL`。
`Message_t` 与 `Msg_Data` **独立** ref；释放经 EBR 后再 put Msg_Data（见 EBR / Port 篇）。

### 4.5 System opcode（`kmsg_system.h`）

| opcode | 值 | fmt | 谁产 |
|--------|----|-----|------|
| POWER_SHUTDOWN | 1 | `""` | powerd 路径 |
| POWER_REBOOT | 2 | `""` | 登记；实现可未齐 |
| TIMER_EXPIRE | 3 | `"q"` token | timer |
| TIMER_CANCEL | 4 | `"q"` | timer |
| PORT_CLOSED | 5 | `"q"`（恒传 0，**无语义载荷**） | `port_clean_thread_queue` → **仅**投给当时 `block_on_receive` 的等待者（send 等待者改走 flag，见 `18` §6.2） |
| SYSTEM_END | 6 | — | 上层「从 7 起编号」的篱笆 |

不同 compat 协议可各自 `SYSTEM_END+1` 起号——靠 **不同 port / `service_id`** 隔离，**不是**全局唯一 opcode 空间。投递细节归收发篇。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `kmsg.c` / `.h` | `kmsg_create`、`kmsg_from_msg` |
| `ipc_serial.c` / `.h` | measure / encode / decode |
| `kmsg_system.h` | system opcode 表 |
| `message.c` | Msg_Data 载体 |

`ipc_serial_encode_alloc` / 独立 `encode_va`：**无仓库内调用方**（近死代码）；热路径走 `kmsg_create` 内 `encode_into`。不是必经路径。

---

## 6. 流程

### 6.1 create

`measure_va` → 拒 `>KMSG_MAX_PAYLOAD` → percpu `kallocator` 分配 → `encode_into_va` → 包装 Msg_Data。坏 fmt / 过大 / 无 allocator → `NULL`。`fmt==NULL` **无防护**（会崩）。

### 6.2 decode

要求 `param_count` == fmt 参数数、tag / len 匹配、**整缓冲吃干净**（`off==buf_len`）；`s` / `t` 非零长末字节须 `'\0'`。错误几乎统一 `-E_IN_PARAM`。

### 6.3 与 compat RPC（边界一句）

上层可先 `encode_into(业务 fmt)`，再 **原地 `param_count++` 追加 `'t'`**。此时 core `decode(业务 fmt)` 会因 count / 残留失败——须带 `t` 解码或先扫 reply。细节不进本篇。

---

## 7. 公开 API

本篇拥有：`kmsg.h` / `kmsg_system.h` / `ipc_serial.h`。说明以头文件 Doxygen 为准（已与 `.c` 核对）。

**本篇不拥有：** `Msg_Data` / Port → `18`；send/recv → `19`；compat RPC / `'t'` 命名语法 → 上层文档。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 发送 kmsg | **`kmsg_create(module, op, fmt, …)`** → `create_message_with_msg` → put 多余 data 引用 → `enqueue` → `send_msg`（`19`） |
| 接收 | `recv` → `dequeue` → **`kmsg_from_msg`** → 比 opcode → **`ipc_serial_decode(payload, payload_len, fmt, …)`** |
| 热路径自编码 | （少见）`measure_va` → 自备缓冲 → `encode_into_va`；`kmsg_create` 已内嵌此序 |

空 `fmt=""` 仍产生 **4 字节** `param_count=0` 的 payload（不是「无缓冲」）。

### 7.2 kmsg 信封（`kmsg.h`）

```c
Msg_Data_t *kmsg_create(u16 module, u16 opcode, const char *fmt, ...);
const kmsg_t *kmsg_from_msg(const Message_t *msg);
```

| 接口 | 说明 |
|------|------|
| `kmsg_create` | measure → 拒 `> KMSG_MAX_PAYLOAD` → 一次 alloc hdr+payload → encode_into → `create_message_data(MSG_DATA_TAG_KMSG, …)`。失败 NULL。不查 port、不填 reply。 |
| `kmsg_from_msg` | 校验 tag / 长度 / `KMSG_MAGIC` / `payload_len` 一致性；**不**验 module/opcode/TLV。 |

常量：`MSG_DATA_TAG_KMSG=1`；`KMSG_MAGIC=0x47534d4c`（字节 `'L','M','S','G'`）；`KMSG_MAX_PAYLOAD=PAGE_SIZE`。

### 7.3 TLV（`ipc_serial.h`）

```c
error_t ipc_serial_measure_va(const char *fmt, va_list ap, u32 *total_out);
error_t ipc_serial_encode_into_va(void *buf, u32 total, const char *fmt, va_list ap);
error_t ipc_serial_decode(const void *buf, u32 buf_len, const char *fmt, ...);
void *ipc_serial_encode_va(const char *fmt, u32 *out_len, va_list ap);
void *ipc_serial_encode_alloc(const char *fmt, u32 *out_len, ...);
```

| 接口 | 说明 |
|------|------|
| `measure_va` | 算出含 4 字节 count 的总长；不消费调用方 `va_list`（内部 copy）。 |
| `encode_into_va` | 写入调用方缓冲；`off` 必须恰等于 `total`。 |
| `decode` | wire `param_count` 须等于 fmt 参数个数；**读完须 `off==buf_len`**（有残留则失败——追加 `'t'` 须纳入 fmt）。`s`/`t` 指针指向缓冲内。 |
| `encode_va` / `encode_alloc` | 自分配辅助；**树内无 C 调用方**（近死）；热路径走 `kmsg_create`。 |

fmt 字符：`p/q/i/u/s/t`；`'t'` 与 `'s'` wire 同形但 **tag 必须对上** 才能 decode。`fmt` 须非 NULL。

### 7.4 System opcode（`kmsg_system.h`）

| 宏 | 值 | fmt | 用途 |
|----|----|-----|------|
| `KMSG_OP_SYSTEM_POWER_SHUTDOWN` | 1 | `""` | powerd |
| `KMSG_OP_SYSTEM_POWER_REBOOT` | 2 | `""` | 登记 |
| `KMSG_OP_SYSTEM_TIMER_EXPIRE` / `_CANCEL` | 3 / 4 | `"q"` | timer |
| `KMSG_OP_SYSTEM_PORT_CLOSED` | 5 | `"q"`（恒 0） | unregister 清队时 **仅**注入给阻塞 recv；send 侧用 `THREAD_FLAG_IPC_PORT_CLOSED`（`18` §6.2） |
| `KMSG_OP_SYSTEM_END` | 6 | — | 上层 opcode 起点（勿重叠） |

---

## 8. 多架构

本机 endian + 本机指针宽；无跨 ISA wire ABI。`p` 在 x86_64 / aarch64 都是 8 字节指针槽，但跨架构镜像之间仍不能当稳定 ABI。

---

## 9. 测试

timer 等 system kmsg；裸 Msg_Data IPC 测例**不**覆盖本篇。本篇未复测。

---

## 10. 限制与后续

- 无版本字段；错 fmt = UB。
- deny / 错误粒度粗。
- opcode 数字可跨协议「撞号」——靠 port 隔离。
- RPC / reply 命名 / nest token → compat。

---

## 11. 变更记录

- 2026-09-27：中文表述润色（母语习惯）；「真源」改为「以…为准」。
- 2026-09-26：PORT_CLOSED 生产点改写——kmsg **只**面向阻塞 recv；纠正旧稿「仅唤醒 recv」易读成「send 不醒」；与 `18` §6.2 / `kmsg_system.h` 对齐。
- 2026-09-26：§7 全文审阅——`ipc_serial.h` / `kmsg_create` 编排序 Doxygen；写清空 fmt=4 字节、decode 无残留、`encode_alloc` 近死、system opcode 表。
- 2026-09-25：语言整理；§8 补指针宽说明。
- 2026-08-29：整篇重做——信封叙述；MAGIC=`LMSG`；`s` 含 NUL；空 fmt 仍有 4 字节；`t` / `s` 不可混解；system 表；encode_alloc 近死；测例与 RPC 边界。
- 2026-08-27：初稿。
