# kmsg 与 TLV 序列化

v0.1 · 2026-09-27

本篇覆盖：`kernel/ipc/kmsg.c`、`kernel/ipc/ipc_serial.c`、`include/rendezvos/ipc/kmsg.h`、`include/rendezvos/ipc/kmsg_system.h`、`include/rendezvos/ipc/ipc_serial.h`。

`Msg_Data` / Port 会合见 `18`；send / recv 与 system 投递见 `19`；设计框架与 MSQ / EBR 见 `22`（建议先读 §1）。

---

## 1. 概述

Port 用于路由消息发送的匹配者；`Message_t` 只是队列节点。具体业务还要约定传递什么、什么格式——若每个 server 自造二进制布局，协议会碎片化。因此 core 内置可选的结构化信封 **kmsg**，给出一种格式化消息的封装规范。

kmsg = 固定头 `kmsg_hdr_t` + **`ipc_serial` TLV**（Type-Length-Value，类型-长度-值；API 名是 `ipc_serial`），再封装为 `Msg_Data_t`（`msg_type == MSG_DATA_TAG_KMSG`）。本篇不解释兼容层各 opcode 载荷字段的含义。

`kmsg` 与 `ipc_serial` 字段、两层职责与 wire 见 §4。`kmsg` 与 `ipc_serial` 拆在不同文件：**kmsg 包一层 ipc_serial**，不是并列两套序列化。在特殊情况下，可以直接用裸 `Msg_Data`（测试、自定义 tag）在里面自定义内容，不走 kmsg。

---

## 2. 目标与边界

**core中提供：** 对于消息的紧凑 TLV 编解码；kmsg 对消息的封装；少量 system 使用的 opcode 登记。

**core中不做：** 解释具体 compat opcode等。

---

## 3. 分层与调用方

kmsg 只负责「把参数编成可投递的 `Msg_Data`」以及「从收到的消息里认出数据格式。TLV 由接收方按 `opcode` 自行 `ipc_serial_decode`进行解码。消息会合仍走收发篇那一套：先 enqueue消息，再`send_msg` ；或者先 `recv_msg` ，再从收队列获取消息。

在 core 里真正使用 kmsg 的地方很少：`port.c`（`PORT_CLOSED`）、`time.c`（timer）、`powerd`（shutdown）。大量调用方主要在兼容层设计的 servers 里面。

另外，在测例中，`single_ipc_test`、port / smp IPC 等测例多半用裸 `create_message_data`，而不是封装好的kmsg格式，因此，不能认为「凡 IPC 消息都走 kmsg」。

在生命周期上，`ipc_serial_decode` 解码得出的 `s` / `t` 指针落在**当前消息缓冲内**；若要把相关数据留到消息释放之后继续使用，须先拷贝。

---

## 4. 数据结构与不变量

### 4.1 两层职责：`ipc_serial` 与 `kmsg`

`ipc_serial` 与 `kmsg` 两层之间的关系是 **kmsg 包一层 ipc_serial**，不是并列两套序列化；走 kmsg 时 TLV 仍必须经 `ipc_serial`，kmsg的头与载荷不要各 alloc 再拷一次，而是统一进行分配。

| | `ipc_serial` | `kmsg` |
|--|--------------|--------|
| 干啥用的 | TLV 字节流以及相关的`encode_into` / `decode`等编码解码操作 | 包含一个描述kmsg的头部，加上把整段载荷嵌进 `Msg_Data`，当作`MSG_DATA_TAG_KMSG`类型的msg |

### 4.2 kmsg封装

数据结构如下：

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

| 字段 | 含义 |
|------|------|
| `magic` | `KMSG_MAGIC`（小端字节 `'L','M','S','G'`）；缓冲合法性校验，`kmsg_from_msg` 比对；不表示布局代际 |
| `module` | 惯例 = 目标 port 的 `service_id`（快检「是不是给我的」）；路由仍靠 port 名 |
| `opcode` | 该 port 上的具体操作；system 操作码在 `kmsg_system.h`，兼容层自编号时建议不要与 system 段重叠 |
| `payload_len` | 跟在后面的 TLV 字节数；须与 `Msg_Data.data_len - 12`（即 `offsetof(kmsg_t, payload)`）一致 |
| `payload` | 该操作的 TLV 参数；编解码双方对 `(opcode 语境下的) fmt` 必须一致 |


kmsg 在 `Msg_Data_t` 的 data 指针指向的 buffer 中的字节布局：

```text
   ┌──────────────── Msg_Data_t buffer (data_len = 12 + payload_len) ────────────────┐
   │  offset 0              4        6        8        12                              │
   │  ┌─────────┬─────────┬─────────┬─────────┬──────────────────────────────────┐  │
   │  │  magic  │ module  │ opcode  │  pl_len │   payload[...] (TLV, ≤ PAGE_SIZE) │  │
   │  │  4 byte │ 2 byte  │ 2 byte  │ 4 byte  │                                  │  │
   │  └─────────┴─────────┴─────────┴─────────┴──────────────────────────────────┘  │
   └────────────────────────────────────────────────────────────────────────────────┘
```
在 Msg_Data_t 头里，`msg_type`= MSG_DATA_TAG_KMSG 只表示 **载荷布局是 kmsg**类型。其它 tag + 裸缓冲（或者其他格式的msg封装）也是合法的。

### 4.3 TLV 数据编解码（`ipc_serial`）

```text
   最开始一个 u32 param_count
   随后跟着 param_count 个参数块:  u8 type_tag | u32 value_len | u8 value[value_len]
```

举个例子字节布局示例（fmt="qi"，2 个参数：一个 i64、一个 i32，fmt见后续）：

这里有两个参数，所以最开始的param_count的u32的值为2，占用4byte，

跟着这4byte之后是第一个参数，参数类型是q，value_len的值为8，表示后面的value长8bytes，再跟着真正的参数value

再之后是第二个参数，参数类型是i，value_len的值为4，表示后面的value长4bytes，再跟着真正的参数value

```text
   ┌────────────┬─────┬──────────┬──────────────────────┬─────┬──────────┬──────────────┐
   │ param_count│ tag │ value_len│       value          │ tag │ value_len│    value     │
   │  u32 = 2   │ 'q' │  u32 = 8 │  i64 (8 byte)        │ 'i' │  u32 = 4 │ i32 (4 byte) │
   │  4 byte    │ 1 B │  4 byte  │       8 byte         │ 1 B │  4 byte  │   4 byte     │
   └────────────┴─────┴──────────┴──────────────────────┴─────┴──────────┴──────────────┘
   ◄──────────── param_count (4 byte) ────────────► ◄──── N 个 TLV ────►
```

| 字符 | encode 入参 | decode 出参 | 宽度 |
|------|-------------|-------------|------|
| `p` | `void*` | `void**` | `sizeof(void*)` |
| `q` | `i64` | `i64*` | 8 |
| `i` | `int`→写 `i32` | `i32*` | 4 |
| `u` | `u32` | `u32*` | 4 |
| `s` | `char*`（`NULL`→`value_len=0`） | `char**`（缓冲内；len0→空指针） | `strlen+1`（**含末尾 `'\0'`**） |
| `t` | 同 `s` | 同 `s` | wire 同 `s`；约定语义 = reply port 名 |

空白字符在 fmt 中忽略。encode 的 tag 与 decode 的 fmt 字符须逐字节相等（`'t'` 编的不能用 `'s'` 解）。空 `fmt=""` 仍写出 4 字节 `param_count=0`。

### 4.4 `kmsg_from_msg` 校验什么

校验内容与 §4.2 字段表一致：
1. `kmsg_from_msg` 检查消息非空
2. `msg_type == MSG_DATA_TAG_KMSG`
3. 缓冲至少能放下头
4. `magic == KMSG_MAGIC`
5. `payload_len == data_len - 12`。

**不**检查 module / opcode / TLV 是否合法——那是接收方自己比对、自己 decode 的事。

### 4.5 所有权

`kmsg_create`：一次分配 hdr + payload → `encode_into` → `create_message_data(..., free_msgdata_ref_default)`，最后得到的refcount=1。失败一律返回 `NULL`。

`Message_t` 与 `Msg_Data` **独立** ref。

### 4.6 System opcode 约定

| opcode | 值 | fmt | 谁发出 |
|--------|----|-----|--------|
| POWER_SHUTDOWN | 1 | `""` | powerd 路径 |
| POWER_REBOOT | 2 | `""` | 已经登记；实现尚未接齐 |
| TIMER_EXPIRE | 3 | `"q"` | timer |
| TIMER_CANCEL | 4 | `"q"` | timer |
| PORT_CLOSED | 5 | `"q"`（恒传 0，**没有业务载荷**） | `port_clean_thread_queue` → **只**投给当时 `block_on_receive` 的等待者（send 一侧port关闭之后的`send_msg` 返回直接用 flag 标记线程被唤醒来自于port被关闭，见 `18` §6.2） |
| SYSTEM_END | 6 | — | 建议兼容层的 opcode 从 SYSTEM_END+1 起编（后续可能继续补充system的opcode，所以建议兼容层都用宏表示自己的起始opcode）；这是用于划分界限的宏，不是一条会发出去的消息 |

不同兼容层协议可以各自从 `SYSTEM_END+1` 起号——靠 **不同 port / `service_id`** 隔开，**不是**全机一份全局唯一的 opcode 空间。怎么投递见收发篇。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `kmsg.c` / `.h` | `kmsg_create`、`kmsg_from_msg` |
| `ipc_serial.c` / `.h` | measure / encode / decode |
| `kmsg_system.h` | system opcode 表 |
| `message.c` | Msg_Data 载体 |

`ipc_serial_encode_alloc` / 独立 `encode_va`：树内近乎无调用方。

---

## 6. 流程

典型发送调用顺序：
1. `kmsg_create(module, opcode, fmt, …)` 得到带 `MSG_DATA_TAG_KMSG` 的 `Msg_Data`
2. `create_message_with_msg` 将`Msg_Data`包成 `Message_t`，若不再单独持有 data 指针则 `ref_put` 一次
3. `enqueue_msg_for_send`
4. `send_msg` 发送消息。

典型接收调用顺序：
1. `recv_msg`接收消息
2. `dequeue_recv_msg`从接收队列获取消息
3. `kmsg_from_msg` 校验信封
4. 调用者按 `opcode` 确定是哪个消息。
5. `ipc_serial_decode` 解码 payload。

---

## 7. 公开 API

本篇涉及的接口分布在：`kmsg.h` / `kmsg_system.h` / `ipc_serial.h`。

**本篇不涉及：** `Msg_Data` / Port → `18`；send/recv → `19`。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 发送 kmsg | **`kmsg_create(module, op, fmt, …)`** → `create_message_with_msg` → put 多余 data 引用 → `enqueue` → `send_msg`（`19`） |
| 接收 | `recv` → `dequeue` → **`kmsg_from_msg`** → 比对 opcode → **`ipc_serial_decode(payload, payload_len, fmt, …)`** |
| 热路径自编码 | （少见）`measure_va` → 自备缓冲 → `encode_into_va`；`kmsg_create` 已内嵌此序 |

空 `fmt=""` 仍产生 **4 字节** `param_count=0` 的 payload（不是「无缓冲」）。

### 7.2 kmsg 封装（`kmsg.h`）

```c
Msg_Data_t *kmsg_create(u16 module, u16 opcode, const char *fmt, ...);
const kmsg_t *kmsg_from_msg(const Message_t *msg);
```

| 接口 | 说明 |
|------|------|
| `kmsg_create` | measure → 拒绝 `> KMSG_MAX_PAYLOAD` → 一次 alloc hdr+payload → encode_into → `create_message_data(MSG_DATA_TAG_KMSG, …)`。失败 NULL。不查 port、不填 reply。 |
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
| `encode_va` / `encode_alloc` | 自分配辅助；**树内无 C 调用方**（近乎死代码）；热路径走 `kmsg_create`。 |

fmt 字符：`p/q/i/u/s/t`；`'t'` 与 `'s'` 虽然实质上都是字符串但 **tag 必须对上** 才能 decode。`fmt==NULL` 各入口（`measure_va` / `encode_into_va` / `decode`）显式拒绝返回 `-E_IN_PARAM`。

### 7.4 System opcode（`kmsg_system.h`）

| 宏 | 值 | fmt | 用途 |
|----|----|-----|------|
| `KMSG_OP_SYSTEM_POWER_SHUTDOWN` | 1 | `""` | powerd |
| `KMSG_OP_SYSTEM_POWER_REBOOT` | 2 | `""` | 登记 |
| `KMSG_OP_SYSTEM_TIMER_EXPIRE` / `_CANCEL` | 3 / 4 | `"q"` | timer |
| `KMSG_OP_SYSTEM_PORT_CLOSED` | 5 | `"q"`（恒 0） | unregister 清队时 **仅**注入给阻塞 recv；send 侧用 `THREAD_FLAG_IPC_PORT_CLOSED`（`18` §6.2） |
| `KMSG_OP_SYSTEM_END` | 6 | — | 上层 opcode 起点（不要重叠） |

---

## 8. 多架构

编解码用本机 `memcpy`（本机 endian、本机 `sizeof(void*)`），没有跨 ISA 的稳定 ABI。`KMSG_MAGIC` 数值按小端字节序读作 `'L','M','S','G'`；当前目标 x86_64 / aarch64 均为小端。

---

## 9. 测试

core里面缺少相关测例，但是可以使用 timer 等发送 system kmsg 实现定时器（能正常定时就可以）；裸 Msg_Data IPC 测试用例不覆盖本篇。

---

## 10. 限制与后续

调用约定靠程序员自己守：`fmt` 里每个字符对应哪一种 C 类型（如 `'q'`→`i64`），必须和后面实参一致。库在运行时只看见一串字符和一块 `va_list`，会按字符去 `va_arg(假定类型)`，它无法再核对「你传进来的到底是不是这个类型」。

因此：`fmt` 写成 `"q"` 却传了 `int`，编译器通常不报错，库也返回成功，结果却是 C 的未定义行为（UB）。

能拦住的是另一类错：不认识的 fmt 字符、编解码 tag/个数/长度对不上等情况，会报错 `-E_IN_PARAM` / `NULL`。  

这是「用 fmt 字符串现场描述参数」这种方案自带的代价。

---

## 11. 变更记录

- 2026-10-07：对照源码终审——修正 `kmsg.h` 中 `MSG_DATA_TAG_KMSG`「routing uses module/opcode」错注；§3/§4.3/§8 与 `ipc_serial`/`kmsg_from_msg` 对齐（NUL、`NULL` 串、空白 fmt、本机 endian）。§10 只保留 fmt/`va_list` 尖角；§1 概述瘦身；去掉 magic 代际说法。
- 2026-10-05：§6.1 / §7.3 同步代码修复——`ipc_serial_measure_va` / `encode_into_va` / `decode` 三个入口加 `!fmt` 检查（返回 `-E_IN_PARAM`）；§6.1 移除「`fmt==NULL` 无防护（会崩溃）」，§7.3 「`fmt` 须非 NULL」改为「各入口显式拒绝」。
- 2026-10-04：补 kmsg 在 `Msg_Data_t` 缓冲中的字节布局图（§4.1）与 TLV wire 字节布局示例图（§4.2，含空 fmt 仍占 4 字节说明）。
- 2026-10-05：任务 1/3/5 精读——口语词与翻译腔清理（碎→碎片化、打进→嵌入、拒→拒绝、会崩→会崩溃、吃干净→消耗完、近死→近乎死代码、比 opcode→比对 opcode、勿重叠→不要重叠）；为首现缩写补全称/释义（TLV、RPC、in-band version、wire、call site、carrier、UB、nest token）；§3/§9「测例」→「测试用例」。
- 2026-10-05：补「与 Linux 类似机制对照」小节；最终词句顺畅。
- 2026-10-04：§4.3 / §4.5 改题为概念名（去掉函数名 / 文件名当小节标题）。
- 2026-09-27：中文表述润色（母语习惯）；「真源」改为「以…为准」。
- 2026-09-26：PORT_CLOSED 生产点改写——kmsg **只**面向阻塞 recv；纠正旧稿「仅唤醒 recv」易读成「send 不醒」；与 `18` §6.2 / `kmsg_system.h` 对齐。
- 2026-09-26：§7 全文审阅——`ipc_serial.h` / `kmsg_create` 编排序 Doxygen；写清空 fmt=4 字节、decode 无残留、`encode_alloc` 近死、system opcode 表。
- 2026-09-25：语言整理；§8 补指针宽说明。
- 2026-08-29：整篇重做——信封叙述；MAGIC=`LMSG`；`s` 含 NUL；空 fmt 仍有 4 字节；`t` / `s` 不可混解；system 表；encode_alloc 近死；测例与 RPC 边界。
- 2026-08-27：初稿。
