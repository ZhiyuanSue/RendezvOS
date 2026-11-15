# 错误码与 panic

v0.1 · 2026-09-27

本篇覆盖：`include/rendezvos/error.h`、`kernel/system/panic.c`、`include/rendezvos/system/panic.h`、`kernel/system/powerd.c`、`include/rendezvos/system/powerd.h`、各 ISA 的 `include/arch/*/power_ctrl.h`。

PSCI（Power State Coordination Interface，电源状态协调接口）`system_off` 终点见 PSCI 篇（`39`）；关机 kmsg opcode 见 kmsg 篇（`21`）；IPC 错误语义见 `04` 各篇。`common.h` / `limits.h` 非本篇主体。

**硬件对照：** x86 QEMU/SeaBIOS 常见关机口 `0x604` 写 `0x2000`（ACPI PM1a 风格，源码宏 `_X86_POWER_SHUTDOWN_`）；复位口 `0x92` bit0（宏 `_X86_INIT_REGISTER_`，`arch_reset`，powerd reboot **未接入**）。aarch64 走 PSCI SYSTEM_OFF（SMCCC，SMC Calling Convention）。riscv64：`panic.c` 里 `#elif defined _RISCV64_` 分支**存在但是空的**，且头树里根本没有 `power_ctrl.h`（`arch_cpu_relax` 宏也没有）——riscv64 目标下 `panic.c` 直接编不过；loongarch 目录存在，但 `panic.c` 连分支都没有。

---

## 1. 概述

core 用两套「出错」的出口，不要混：

1. **可返回的错误：** `error_t`（`typedef int`）。成功 `REND_SUCCESS == 0`；枚举值是**正数**（从 `E_RENDEZVOS = 1024` 起），调用惯例 **`return -E_*`**（负值）。选 1024 起是为了给 Linux 负 errno 留空间——兼容层自己映射，**不在** `error.h` 扩 Linux 全表。
2. **不可恢复：** `kernel_panic(msg)` / `kernel_halt()` 打 `pr_error`、调 `arch_shutdown()`，若竟然返回则再打一条「unreachable」日志，最后 `while(1) arch_cpu_relax()`（x86 为 `pause`、aarch64 为 `yield`，定义在各 ISA `sync/barrier.h`）。注意**实现并不关中断**——打印途中时钟/IPI 依然可以进来，这是现状而非笔误（§6）。无栈回溯、不停其他核、不经 powerd。

`error_t` 的编码布局（`int` 为 32 位有符号）：

```text
   值轴（int）
   ────────────────────────────────────────────────────────────►
        ←  -E_REND_*  →  │ 0 │  ←  +E_REND_*  →       ←  -errno  →
   ─────┬───────────────┬─────┬──────────────────┬─────────────────
        ↑               ↑     ↑                  ↑
   调用惯例返回值     成功   枚举定义正值      预留给兼容层
   （实际传负值）            （从 1024 起）    （如 -ENOENT=-2）

   约定：
     return 0       → 成功
     return -E_*    → 失败（调用方 if (ret < 0) 视为失败）
     return +E_*    → 禁止直接返回给调用方（枚举仅用于定义）
   选 1024 起：避开 [−1023, −1] 的小负 errno，兼容层可直接用 Linux
                负 errno 而不与本表冲突。
```

**优雅关机**（测试用例收尾、正常请求）：`rendezvos_request_poweroff()`（`powerd.h` 中 `static inline`）lookup 端口 `"powerd"`，发 `KMSG_OP_SYSTEM_POWER_SHUTDOWN`（值 `1`，定义在 `kmsg_system.h`；REBOOT 为 `2`）；powerd 线程收到后 `kernel_halt`，再走同一套 `arch_shutdown`。端口不存在返回 `-E_RENDEZVOS`，消息构造失败返回 `-E_REND_NO_MEM`，入队失败原样转回发送方错误码；各失败路径均有 `ref_put` 收尾。

`panic.c` 注释写「上层应走 powerd」是**期望**——实际 `kernel_panic` 调用点共 15 处：`cmain` 启动九个阶段各一处（setup_info 为空、`prepare_arch`/`phy_mm_init`/`virt_mm_init`/`arch_start_core` 等任一失败）、`trap.c` 的「Unhandled trap: unknown IRQ」、`time.c` 与 aarch64 `generic_time.c` 的定时器探测失败、`mm_user_utils.c` 两处兜底。`kernel_halt` 另有三处：aarch64/x86 `pmm.c` 各一（启动期内存拓扑解析失败即 `arch_init_pmm_error`，**不是**运行期物理页耗尽）与 powerd 的 SHUTDOWN 分发。两条路径并存是现状，不是文档能抹掉的 bug。

### 与 Linux 类似机制对照

POSIX errno 与 Linux 内核的 `error_t` 取向相反：POSIX errno 是正数（`errno.h`，`EPERM=1`、`ENOENT=2` 等），库函数置 `errno`、调用方查正数；本仓库 `error_t` 是负数（`return -E_REND_*`），枚举本身定义为正、返回时取负，调用方 `if (ret < 0)` 判失败。致命路径上，Linux `panic()` 会跑 `panic_notifier_list`、`crash_kexec`（kdump 抓内核转储）、再 `machine_halt`，把崩溃现场留作事后分析；本仓库 panic 走的是 `pr_error`、`arch_shutdown`，再进死循环，aarch64 经 PSCI SYSTEM_OFF、x86 写 `0x604` 关机，目标是「让 QEMU 进程干净退出」而非留现场。v0.1 不引入 kexec / kdump：那需要独立的 crash kernel 预留内存与架构移植，超出当前验证范围。

---

## 2. 目标与边界

**提供：** 错误码约定与常用码表；panic/halt；powerd 客户端 inline + BSP 服务线程骨架。

**不做：** Linux errno 全表；panic 调试器 / kdump；跨核停机广播；完整 reboot（opcode 已登记，目前仅记录日志）。

---

## 3. 分层与调用方

| 路径 | 行为 |
|------|------|
| 普通 API | 查 `==0` 或 `<0`；勿把正枚举作为返回值直接传给调用方 |
| powerd 服务 | `DEFINE_INIT(powerd_init)` 全核都会跑，但 `powerd_init` 开头运行时判 `percpu(cpu_number) != BSP_ID` 即返回（BSP-only 靠**判断**而非条件编译），再判 `percpu(core_tm)` 就绪才 `gen_thread_from_func` 起线程（线程名也叫 `"powerd"`），起线程失败仅 `pr_error`；线程内 `create_message_port`+`register_port`，任一失败即 `return`（注册失败会先 `delete_message_port_structure` 回收） |
| 客户端 | `rendezvos_request_poweroff()`（`powerd.h` inline） |
| 消息校验 | 收包后校验 `kmsg_from_msg` 非空且 `hdr.module == port->service_id`，否则**静默丢弃**（无日志，排障时注意）；`opcode` 读出并 `ref_put` 之后才分发；非 SHUTDOWN/REBOOT 打 `unknown opcode` |
| reboot opcode | 已识别（值 `2`），`pr_error "reboot not implemented"`，不调 `arch_reset` |
| panic / 直接 halt | **不经** powerd，立刻 `arch_shutdown` |

---

## 4. 数据结构与不变量

### 4.1 常用码（按实测密度）

下表计数以 `kernel/`、`modules/`、`arch/` 的 `.c` 中 `return -E_*` 形式统计（比较用法如 `ret == -E_REND_AGAIN` 不计入）：

| 返回惯例 | 枚举 | 次数 | 域 / 备注 |
|----------|------|------|-----------|
| `0` | `REND_SUCCESS` | — | 成功 |
| `-E_IN_PARAM` | `E_IN_PARAM` | 216 | 几乎全核；最常用 |
| `-E_RENDEZVOS` | `E_RENDEZVOS` | 95 | 通用失败兜底 |
| `-E_REND_TEST` | `E_REND_TEST` | 83 | 计数第三，但域几乎仅 `modules/test` 测例判失败 |
| `-E_REND_AGAIN` | `E_REND_AGAIN` | 15 | 可重试：无 waiter、`send_pending` 槽忙、buddy reclaim 等（**不是** OOM 同义词） |
| `-E_REND_NOFOUND` | `E_REND_NOFOUND` | 12 | page_slice / radix / unmap / timer |
| `-E_REND_NO_MEM` | `E_REND_NO_MEM` | 10 | 分配失败：page_slice(_copy) / ipc / time / fp |
| `-E_REND_RC_UNEQUAL` | `E_REND_RC_UNEQUAL` | 7 | refcount / ownership：VSpace mask / teardown；msg 坏 ref |
| `-E_REND_PORT_CLOSED` | `E_REND_PORT_CLOSED` | 7 | send / recv |
| `-E_REND_OVERFLOW` | `E_REND_OVERFLOW` | 5 | IPI / page_slice / radix（**不仅** IPI） |
| `-E_REND_NO_MSG` | `E_REND_NO_MSG` | 3 | 空队列 |
| （无调用） | `E_REND_IPC` | 0 | **仅枚举，全仓库零引用**——头文件注释写「IPC protocol failure」，但 IPC 各失败路径实际返回的是更具体的码；死码，勿编造语义 |
| （无调用） | `E_REND_ABANDON` | 0 | **仅枚举**；死码，勿编造语义 |

`E_REND_IPC` 与 `E_REND_ABANDON` 是仅有的两个死码：枚举里占位（1027、1029），任何 `.c`/`.h` 都不引用。旧版表格曾给 `E_REND_IPC` 编排了「IPC 协议失败」的使用域，已按代码现状作废。

### 4.2 各 ISA 怎么真正关机

`kernel_panic` / `kernel_halt` 最终会调 `arch_shutdown`：

| Arch | 行为 |
|------|------|
| x86_64 | `outw(_X86_POWER_SHUTDOWN_, 0x2000)` 即 `outw(0x604, 0x2000)`——QEMU 上能关机；源码注释自己写明「which is just QEMU/SeaBIOS… on a real machine 应走 ACPI 表」，且带一句玩笑注释 `have no idea,maybe it works`；**不是** isa-debug-exit（`0x501` 那一类） |
| aarch64 | `psci_func.system_off()`——`static inline` **不检查** `psci_init` 是否成功、指针是否为 NULL（`39` 缺口一） |
| riscv64 | 无 `power_ctrl.h`：`panic.c` 的 `_RISCV64_` 分支为空，`arch_shutdown` 与 `arch_cpu_relax` 均无定义——riscv64 目标编译 `panic.c` 直接失败；loongarch 连分支都没有 |

`arch_reset`：x86 `outb(0x92, 1)` 置 bit0（注释「Soft reset via port 0x92 bit0. **unused now**」）；aarch64 是空函数（注释原文「no reboot here, we don't realise it now」，sic）；powerd 收到 **REBOOT 也不会调它**。签名见 §7。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `error.h` | 枚举 |
| `panic.c` / `panic.h` | `kernel_panic` / `kernel_halt` |
| `powerd.c` | BSP 线程 + SHUTDOWN/REBOOT 分发 |
| `powerd.h` | `rendezvos_request_poweroff` |
| `include/arch/*/power_ctrl.h` | `arch_shutdown` / `arch_reset`（均 `static inline`） |
| `arch/x86_64/io_port.h` | `_X86_POWER_SHUTDOWN_ = 0x604`、`_X86_INIT_REGISTER_ = 0x92` |
| `include/rendezvos/ipc/kmsg_system.h` | `KMSG_OP_SYSTEM_POWER_SHUTDOWN = 1`、`KMSG_OP_SYSTEM_POWER_REBOOT = 2` |

---

## 6. 流程

```text
kernel_panic(msg)                                kernel_halt()
  → pr_error("KERNEL PANIC: %s",                       → pr_error("KERNEL: System halted.")
             msg ? msg : "(unknown)")                   → arch_shutdown()
  → pr_error("CPU %lu: System halted.",                 → pr_error("[kernel halt] goto
             percpu(cpu_number))                                  unreachable place,halt")
  → arch_shutdown()   // 期望不返回：              → while (1) arch_cpu_relax()
       x86_64   → outw(0x604, 0x2000)   // QEMU ACPI PM1a 风格关机口
       aarch64  → psci_func.system_off()  // SMC/HVC 调 PSCI SYSTEM_OFF
       riscv64  → （编不出此文件）          // 无 power_ctrl.h，缺实现
  → pr_error("[kernel panic] goto unreachable place,halt")
  → while (1) arch_cpu_relax()   // arch_shutdown 失效时的兜底死循环
```

两个实现细节值得注意：其一，`kernel_panic`/`kernel_halt` **都不关中断**——代码里没有任何 `local_irq_*` 调用，时钟/IPI 仍可打断关机日志的输出（现状记档，非文档笔误）；其二，`arch_shutdown` 之后还有一条「unreachable」`pr_error`——看到这条日志就说明本应终结一切的 `arch_shutdown` 竟返回了（典型场景：x86 写 `0x604` 被 QEMU 忽略、PSCI 固件收下 SYSTEM_OFF 却没真正断电；注意若指针为 NULL 则根本走不到这条日志——那会在调用处直接触发异常）。`arch_cpu_relax`：x86 `pause`、aarch64 `yield`（各 ISA `sync/barrier.h`）。`msg` 为空时打印 `(unknown)`，不会二次崩溃。

注：当前实现不做栈回溯（无 unwinding、不打印调用栈），也不广播 IPI 停其他核——多核场景下其他 CPU 可能继续运行，直到自身触发异常或被 QEMU 整机断电带停。

powerd 与 PSCI 的关系（aarch64 视角）：

```text
   用户态 / 测试用例            core 内核                   硬件 / 固件
   ──────────────             ────────────                ──────────
   request_poweroff() ──IPC──► powerd 线程
                              kernel_halt()
                              arch_shutdown()
                              psci_func.system_off() ──SMC/HVC──► PSCI firmware
                                                                       │
                                                                       ▼
                                                                  CPU 进入
                                                                  shutdown 状态
   注：x86 上 arch_shutdown 走 I/O 端口 0x604（ACPI PM1a 风格），
       不经 PSCI；PSCI 仅 aarch64 有。
```

powerd 的失效面：`powerd_init` 里 `core_tm` 未就绪或 `gen_thread_from_func` 失败只打日志；线程内 port 创建失败直接 `return`，注册失败先 `delete_message_port_structure` 再 `return`——无论哪种，`"powerd"` 端口缺席后 `request_poweroff` 一律 `-E_RENDEZVOS`，测试用例「跑完关机」失效，只能退回直接 panic/halt 出口。

---

## 7. 公开 API

本篇涉及的接口分布在四处：可返回错误约定（`error.h`）、不可恢复出口（`panic.h`，实现 `panic.c`）、优雅关机客户端与 BSP `powerd` 服务（`powerd.h` / `powerd.c`）、x86 电源钩子 `arch_shutdown` / `arch_reset`（`include/arch/x86_64/power_ctrl.h`）。以头文件注释为准（均已与 `.c` 核对）。

**本篇不涉及：** aarch64 `arch_shutdown` 到 PSCI SYSTEM_OFF 的细节（见 `39`）；关机 kmsg opcode 常量正文（见 `21`）；IPC send/recv 语义（见 `20`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 可恢复 API | 成功 `0`；失败 **`return -E_*`**（枚举为正，返回为负） |
| 测试用例 / 优雅关机 | **`rendezvos_request_poweroff()`** 发 `"powerd"` SHUTDOWN，经 `kernel_halt` 到 `arch_shutdown` |
| 致命 | **`kernel_panic(msg)`** 或 **`kernel_halt()`**——**不经** powerd |
| powerd 起服 | `DEFINE_INIT` 阶段（BSP 判断通过后）`gen_thread_from_func(powerd_thread)`；create+register `"powerd"` 发生在**线程体内**而非 init 阶段，之后循环 `recv` |

### 7.2 错误码（`error.h`）

```c
enum Error_t {                       /* 值连续：1024 起共 12 个 */
        REND_SUCCESS = 0,
        E_RENDEZVOS = 1024,  /* 起跳，给小 errno 留位 */
        E_IN_PARAM,          /* 1025 */
        E_REND_TEST,         /* 1026 */
        E_REND_IPC,          /* 1027  死码：全仓库零引用 */
        E_REND_AGAIN,        /* 1028 */
        E_REND_ABANDON,      /* 1029  死码：全仓库零引用 */
        E_REND_NO_MSG,       /* 1030 */
        E_REND_RC_UNEQUAL,   /* 1031 */
        E_REND_NOFOUND,      /* 1032 */
        E_REND_OVERFLOW,     /* 1033 */
        E_REND_NO_MEM,       /* 1034 */
        E_REND_PORT_CLOSED,  /* 1035 */
};
/* error_t = int（common/types.h:43） */
```

勿把正枚举值当作返回值直接传给调用方。常用码见 §4.1；两个死码（`E_REND_IPC`、`E_REND_ABANDON`）在枚举中占位但无人使用，删除与否不影响 ABI 之外的一切。

### 7.3 Panic / halt

```c
void kernel_panic(const char *msg) __attribute__((noreturn));
void kernel_halt(void)             __attribute__((noreturn));
```

两者均带 `noreturn` 属性（`panic.h`）。无栈回溯、不停其他核、不关中断；`msg` 传 NULL 安全（打印 `(unknown)`）。riscv64：`panic.c` 编不过（缺 `arch_shutdown` 与 `arch_cpu_relax`），见 §4.2。

### 7.4 Powerd

```c
#define RENDEZVOS_POWERD_PORT_NAME "powerd"
error_t rendezvos_request_poweroff(void);  /* inline in powerd.h */
```

| 行为 | 说明 |
|------|------|
| SHUTDOWN（1） | `pr_info` 后落到 `kernel_halt` |
| REBOOT（2） | 只 `pr_error "reboot not implemented"`；**不**调 `arch_reset` |
| 未知 opcode | `pr_error "unknown opcode %u"` |
| 消息校验 | 非 kmsg 或 `module` 不匹配 **静默丢弃**（无日志）；`ref_put` 先于分发 |
| port 起败 | 线程 return 后，此后 request 得 `-E_RENDEZVOS` |

### 7.5 Arch 关机钩子

| Arch | `arch_shutdown` | `arch_reset` |
|------|-----------------|--------------|
| x86 | `outw(0x604, 0x2000)`（本篇） | `outb(0x92, 1)`；reboot 未接入 |
| aarch64 | `psci_func.system_off()`，**不查** `psci_init` 成败（细节与缺口见 `39`） | 空 |
| riscv64 | **缺失**（`panic.c` 因此编不过） | — |
| loongarch | 头树有目录、`panic.c` 无分支——同缺 | — |

---

## 8. 多架构

上表。关机硬件/固件接口不同，错误码本身与 ISA 无关。

---

## 9. 测试

没有针对错误码/panic 的专门测例，但有一贯穿始终的间接验证链：`modules/test/test.c` 收尾调用 `rendezvos_request_poweroff()`（见 `43`）——QEMU 里测例跑完进程能正常退出，即证明 `request_poweroff → powerd 收包校验 → kernel_halt → arch_shutdown` 全链工作正常（aarch64 上还顺带覆盖 PSCI SYSTEM_OFF）。测例内部判失败统一用 `-E_REND_TEST`，这是该码计数高达 83 的全部来源。反过来的失效形态也可辨识：若测例跑完 QEMU 不退出、串口刷 `unknown opcode` 或 `recv_msg failed`，问题在 powerd 收包侧；若 `-E_RENDEZVOS` 后静默挂死，多半 `"powerd"` 端口没起来（§6）。

---

## 10. 限制与后续

- panic 简陋：不关中断、无栈回溯、不停其他核；直接 panic 与 powerd 双路径并存（§1）。
- reboot 未齐（opcode 2 只打日志，`arch_reset` 无人调用）；riscv64 连 `panic.c` 都编不过，loongarch 无分支。
- `E_REND_IPC`、`E_REND_ABANDON` 两个死码占位。
- powerd 起不来时无优雅关机出口；powerd 静默丢弃非本模块消息，无日志可查。

---

## 11. 变更记录
- 2026-11-15：Maintainer 复审前可读性通读（篇章架构 / 篇章引用 / 词句）。事实修正三处：① §4.1 计数复测——`return -E_IN_PARAM` 实为 **216**（原 213）、`return -E_REND_NO_MEM` 实为 **10**（原 9），差值恰与新增的 `kernel/task/fp.c`（3 处 `E_IN_PARAM`、1 处 `E_REND_NO_MEM`）吻合；② 同表 `E_REND_NO_MEM` 使用域「buddy / page_slice」不实——实测十处分布在 page_slice(_copy) / ipc / time / fp，buddy 路径并不返回该码；③ `kernel_panic` 调用点「约 15 处」落实为恰 15 处（cmain 九、`trap.c` 一、`time.c` 二、`generic_time.c` 一、`mm_user_utils.c` 二）。词句四处：§1 优雅关机段「原样返回 `-E_RENDEZVOS`」不准——`powerd.h` 实为三类返回（端口缺席 `-E_RENDEZVOS`、构造失败 `-E_REND_NO_MEM`、入队失败原样转发），改为分列；§1「若竟返回」改「若竟然返回」；§7 首电报体改为按四处文件列举；§4.2 x86 玩笑注释按源码原文改 `have no idea,maybe it works`（原多一空格）。引用四处：篇首全文件名缩为 PSCI 篇（`39`）、kmsg 篇补号（`21`）、「见 04 各篇」补反引号、§9「见 43」补反引号。核实属实项：`arch_cpu_relax` 定义于各 ISA `sync/barrier.h`（x86 `pause`、aarch64 `yield`）、`0x604`/`0x92` 宏值与 ACPI 注释、`Soft reset via port 0x92 bit0. unused now`、kmsg opcode `1u`/`2u`、aarch64 `arch_reset` 注释原文、panic/halt 打印序列逐字（含 `(unknown)` NULL 安全）、powerd BSP 运行时判断 / `core_tm` 守卫 / 注册失败先 `delete_message_port_structure` / module 不匹配静默丢弃 / `ref_put` 先于分发 / `unknown opcode %u` / `reboot not implemented`、`E_REND_IPC` 与 `E_REND_ABANDON` 全仓零引用（1027、1029）、枚举 1024–1035 共 12 项、`error_t = int`（`include/common/types.h:43`）、`-E_REND_TEST` 83 处全部在 `modules/test`、riscv64 空分支且无 `power_ctrl.h`、loongarch 无分支。格式扫描（setext / 标题空行 / 分隔线 / 实体 / 引号）无命中。余沿 11-15 精读条目，未变。

- 2026-11-15：全文对照 error.h/types.h/panic.c/panic.h/powerd.c/powerd.h/kmsg_system.h/两 ISA power_ctrl.h/io_port.h/两 pmm.c 精读——**删去「panic 关中断」的不实表述**（代码无任何 `local_irq_*`，§6 流程图按 `kernel_panic`/`kernel_halt` 实际打印序列重写，补「unreachable」兜底日志、`msg` NULL 安全、relax 指令 x86 `pause`/aarch64 `yield`）；`E_REND_IPC` 定案为**全仓库零引用死码**（旧表「IPC 协议失败」使用域作废），§4.1 按 `return -E_*` 实测计数重排（IN_PARAM 213/RENDEZVOS 95/TEST 83/…）；§7.2 枚举按源码真实顺序与数值全列（ABANDON 实际在 1029 非末尾）；powerd 补 BSP 运行时判断、`core_tm` 守卫、注册失败先 `delete_message_port_structure`、module 校验**静默丢弃**、`ref_put` 先于分发、未知 opcode 打印；`kernel_panic` 调用面落到实处（cmain 九处/trap.c unknown IRQ/time.c/generic_time/mm_user_utils 约 15 处；`kernel_halt` 三处，pmm.c 两处为**启动期内存解析失败**而非运行期 OOM）；riscv64 表述精确化（空分支+无 `power_ctrl.h`+无 `arch_cpu_relax` → 编不过；loongarch 无分支）；x86 端口宏名、kmsg opcode 数值 1/2、`include/arch/` 路径修正；§9 重写为真实验证链与失效形态辨识。
- 2026-10-04：补 `error_t` 编码布局 ASCII 图（值轴、正负约定、1024 起跳缘由）；扩写 panic 流程（关中断、UART 输出、arch_shutdown 各 ISA 分支、无栈回溯/不广播停他核说明）；补 powerd↔PSCI 关系图（aarch64 SMC/HVC、x86 走 0x604 不经 PSCI）；改「甩给调用方」为「传给调用方」。
- 2026-10-04：§4.2 改题为「各 ISA 怎么真正关机」，不再以函数名起题。
- 2026-10-01：删除重复码 `E_REND_RETRY`；buddy reclaim 耗尽并入 `-E_REND_AGAIN`（§4.1）。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——error/panic/powerd/x86 power_ctrl Doxygen；负返回惯例；vs `39`/`21`。
- 2026-09-26：语言轮——负返回惯例；码表；x86 `0x604`/`0x92`；powerd vs 直接 panic；riscv 缺口。
- 2026-08-29：整篇重做。
- 2026-08-27：初稿。

- 2026-10-05：任务 1/3/5 精读——正文 5 处「测例」改「测试用例」；「不停他核」改「不停其他核」；「现只打 log」改「目前仅记录日志」、「会挂」改「会卡死」；首次出现补 PSCI、SMCCC、SMC/HVC 全称。
- 2026-10-05：补「与 Linux 类似机制对照」小节；最终词句顺畅。
