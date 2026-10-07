# 错误码与 panic

v0.1 · 2026-09-27

本篇覆盖：`include/rendezvos/error.h`、`kernel/system/panic.c`、`include/rendezvos/system/panic.h`、`kernel/system/powerd.c`、`include/rendezvos/system/powerd.h`、各 ISA 的 `arch/*/power_ctrl.h`。

PSCI（Power State Coordination Interface，电源状态协调接口）`system_off` 终点见 `39-PSCI与处理器电源-aarch64.md`；关机 kmsg opcode 见 kmsg 篇；IPC 错误语义见 04 各篇。`common.h` / `limits.h` 非本篇主体。

**硬件对照：** x86 QEMU/SeaBIOS 常见关机口 `0x604` 写 `0x2000`（ACPI PM1a 风格）；复位口 `0x92`（`arch_reset`，powerd reboot **未接入**）。aarch64 走 PSCI SYSTEM_OFF（SMCCC，SMC Calling Convention）。riscv64：`panic.c` 无 `#include` 分支——`arch_shutdown` 未齐。

---

## 1. 概述

core 用两套「出错」的出口，不要混：

1. **可返回的错误：** `error_t`（`typedef int`）。成功 `REND_SUCCESS == 0`；枚举值是**正数**（从 `E_RENDEZVOS = 1024` 起），调用惯例 **`return -E_*`**（负值）。选 1024 起是为了给 Linux 负 errno 留空间——compat 自己映射，**不在** `error.h` 扩 Linux 全表。
2. **不可恢复：** `kernel_panic(msg)` / `kernel_halt()` → 关中断 → 打日志 → `arch_shutdown()` → `while(1) arch_cpu_relax()`。无栈回溯、不停其他核、不经 powerd。

`error_t` 的编码布局（`int` 为 32 位有符号）：

```text
   值轴（int）
   ────────────────────────────────────────────────────────────►
        ←  -E_REND_*  →  │ 0 │  ←  +E_REND_*  →       ←  -errno  →
   ─────┬───────────────┬─────┬──────────────────┬─────────────────
        ↑               ↑     ↑                  ↑
   调用惯例返回值     成功   枚举定义正值      预留给 compat
   （实际传负值）            （从 1024 起）    （如 -ENOENT=-2）

   约定：
     return 0       → 成功
     return -E_*    → 失败（调用方 if (ret < 0) 视为失败）
     return +E_*    → 禁止直接返回给调用方（枚举仅用于定义）
   选 1024 起：避开 [−1023, −1] 的小负 errno，compat 可直接用 Linux
                负 errno 而不与本表冲突。
```

**优雅关机**（测试用例收尾、正常请求）：`rendezvos_request_poweroff()` → lookup 端口 `"powerd"` → `KMSG_OP_SYSTEM_POWER_SHUTDOWN` → powerd 线程 → `kernel_halt` → 同一套 `arch_shutdown`。

`panic.c` 注释写「上层应走 powerd」是**期望**——`cmain`、未处理路径、heartbeat 失败等仍大量 **直接 panic**。两条路径并存是现状，不是文档能抹掉的 bug。

### 与 Linux 类似机制对照

POSIX errno 与 Linux 内核的 `error_t` 取向相反：POSIX errno 是正数（`errno.h`，`EPERM=1`、`ENOENT=2` 等），库函数置 `errno`、调用方查正数；本仓库 `error_t` 是负数（`return -E_REND_*`），枚举本身定义为正、返回时取负，调用方 `if (ret < 0)` 判失败。致命路径上，Linux `panic()` 会跑 `panic_notifier_list`、`crash_kexec`（kdump 抓内核转储）、再 `machine_halt`，把崩溃现场留作事后分析；本仓库 panic 走的是 `pr_error` → `arch_shutdown` → 死循环，aarch64 经 PSCI SYSTEM_OFF、x86 写 `0x604` 关机，目标是「让 QEMU 进程干净退出」而非留现场。v0.1 不引入 kexec / kdump：那需要独立的 crash kernel 预留内存与架构移植，超出当前验证范围。

---

## 2. 目标与边界

**提供：** 错误码约定与常用码表；panic/halt；powerd 客户端 inline + BSP 服务线程骨架。

**不做：** Linux errno 全表；panic 调试器 / kdump；跨核停机广播；完整 reboot（opcode 已登记，目前仅记录日志）。

---

## 3. 分层与调用方

| 路径 | 行为 |
|------|------|
| 普通 API | 查 `==0` 或 `<0`；勿把正枚举作为返回值直接传给调用方 |
| powerd 服务 | BSP-only `DEFINE_INIT` 起线程；线程内 `create`+`register` `"powerd"`，循环 `recv` |
| 客户端 | `rendezvos_request_poweroff()`（`powerd.h` inline） |
| reboot opcode | 已识别，`pr_error` 未实现 |
| panic / 直接 halt | **不经** powerd，立刻 `arch_shutdown` |

---

## 4. 数据结构与不变量

### 4.1 常用码（按出现密度）

| 返回惯例 | 枚举 | 域 / 备注 |
|----------|------|-----------|
| `0` | `REND_SUCCESS` | |
| `-E_IN_PARAM` | `E_IN_PARAM` | 几乎全核；最常用 |
| `-E_RENDEZVOS` | `E_RENDEZVOS` | 通用失败兜底 |
| `-E_REND_AGAIN` | `E_REND_AGAIN` | 可重试：IPC 非阻塞、buddy reclaim 耗尽等（**不是** OOM 同义词；hook 放弃用 `NO_MEM`） |
| `-E_REND_NOFOUND` | `E_REND_NOFOUND` | page_slice / radix / unmap / timer |
| `-E_REND_OVERFLOW` | `E_REND_OVERFLOW` | IPI / page_slice / radix（**不仅** IPI） |
| `-E_REND_NO_MEM` | `E_REND_NO_MEM` | buddy / page_slice |
| `-E_REND_PORT_CLOSED` | `E_REND_PORT_CLOSED` | send / recv |
| `-E_REND_IPC` | `E_REND_IPC` | 协议 / 资源 |
| `-E_REND_NO_MSG` | `E_REND_NO_MSG` | 空队列 |
| `-E_REND_RC_UNEQUAL` | `E_REND_RC_UNEQUAL` | VSpace mask / teardown |
| `-E_REND_TEST` | `E_REND_TEST` | 几乎仅 `modules/test` |
| （无调用） | `E_REND_ABANDON` | **仅枚举**；死码，勿编造语义 |

### 4.2 各 ISA 怎么真正关机

`kernel_panic` / `kernel_halt` 最终会调 `arch_shutdown`：

| Arch | 行为 |
|------|------|
| x86_64 | `outw(0x604, 0x2000)`——QEMU 上常常能关机；**不是** isa-debug-exit（`0x501` 那一类） |
| aarch64 | `psci_func.system_off()`（须 `psci_init` 已经把函数指针填上） |
| riscv64 | `panic.c` 里对应分支是空的——没有真正的关机实现 |

`arch_reset`：x86 按写 `0x92` bit0 那一类方式复位；aarch64 是空函数；powerd 收到 **REBOOT 也不会调它**。签名见 §7。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `error.h` | 枚举 |
| `panic.c` / `panic.h` | `kernel_panic` / `kernel_halt` |
| `powerd.c` | BSP 线程 + SHUTDOWN/REBOOT 分发 |
| `powerd.h` | `rendezvos_request_poweroff` |
| `arch/*/power_ctrl.h` | `arch_shutdown` / `arch_reset` |

---

## 6. 流程

```text
kernel_panic(msg) / kernel_halt()
  → local_irq_disable()        // 关中断：避免打印途中被时钟/IPI 打断
  → pr_error(msg)              // 经 log 通路输出到 UART/serial
  → arch_shutdown()            // 期望不返回：
       x86_64   → outw(0x604, 0x2000)        // QEMU ACPI PM1a 风格关机口
       aarch64  → psci_func.system_off()    // SMC/HVC（Secure Monitor Call / Hypervisor Call）调 PSCI SYSTEM_OFF
       riscv64  → (空)                       // 缺实现，落死循环
  → while (1) arch_cpu_relax()  // arch_shutdown 失效时的兜底死循环

注：当前实现不做栈回溯（无 unwinding、不打印调用栈），
     不广播 IPI 停其他核——多核场景下其他 CPU 可能继续运行，
     直到自身触发异常或被 QEMU 整机断电带停。
```

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

powerd 在 port 创建/注册失败时线程直接 return——此后 `request_poweroff` 会 `-E_RENDEZVOS`，测试用例「跑完关机」可能失效。

---

## 7. 公开 API

本篇涉及的接口分布在：可返回错误约定（`error.h`）、不可恢复出口（`kernel_panic` / `kernel_halt`）、优雅关机客户端与 BSP `powerd` 服务（`powerd.h` / `powerd.c`）、x86 `arch_shutdown`/`arch_reset`（`arch/x86_64/power_ctrl.h`）。以头文件注释为准（见上列头文件；已与 `.c` 核对）。

**本篇不涉及：** aarch64 `arch_shutdown`→PSCI SYSTEM_OFF 细节 → `39`；关机 kmsg opcode 常量正文 → `21`；IPC send/recv 语义 → `20`。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 可恢复 API | 成功 `0`；失败 **`return -E_*`**（枚举为正，返回为负） |
| 测试用例 / 优雅关机 | **`rendezvos_request_poweroff()`** → `"powerd"` SHUTDOWN → `kernel_halt` → `arch_shutdown` |
| 致命 | **`kernel_panic(msg)`** 或 **`kernel_halt()`**——**不经** powerd |
| powerd 起服 | BSP `DEFINE_INIT` → `gen_thread_from_func(powerd_thread)` → create+register `"powerd"` → 循环 `recv` |

### 7.2 错误码（`error.h`）

```c
enum Error_t {
        REND_SUCCESS = 0,
        E_RENDEZVOS = 1024,  /* 起跳，给小 errno 留位 */
        E_IN_PARAM, E_REND_IPC, E_REND_AGAIN, E_REND_NOFOUND,
        E_REND_OVERFLOW, E_REND_NO_MEM, E_REND_PORT_CLOSED, …
        E_REND_ABANDON,      /* 仅枚举；死码 */
};
/* error_t = int（common/types.h） */
```

勿把正枚举值当作返回值直接传给调用方。常用码见 §4.1。

### 7.3 Panic / halt

```c
void kernel_panic(const char *msg);  /* noreturn：pr_error → arch_shutdown → spin */
void kernel_halt(void);              /* noreturn：同上，固定文案 */
```

无栈回溯、不停其他核。riscv64：`panic.c` 无 `#include` 分支——缺 `arch_shutdown` 会卡死。

### 7.4 Powerd

```c
#define RENDEZVOS_POWERD_PORT_NAME "powerd"
error_t rendezvos_request_poweroff(void);  /* inline in powerd.h */
```

| 行为 | 说明 |
|------|------|
| SHUTDOWN | → `kernel_halt` |
| REBOOT | 只 `pr_error`；**不**调 `arch_reset` |
| port 起败 | 线程 return → 此后 request 得 `-E_RENDEZVOS` |

### 7.5 Arch 关机钩子

| Arch | `arch_shutdown` | `arch_reset` |
|------|-----------------|--------------|
| x86 | `outw(0x604, 0x2000)`（本篇） | `outb(0x92, 1)`；reboot 未接入 |
| aarch64 | `psci_func.system_off()`（细节 `39`） | 空 |
| riscv | **缺失** | — |

---

## 8. 多架构

上表。关机硬件/固件接口不同，错误码本身与 ISA 无关。

---

## 9. 测试

测试用例框架收尾常 `request_poweroff`（见 43）。

---

## 10. 限制与后续

- panic 简陋；直接 panic 与 powerd 双路径并存。
- reboot 未齐；riscv shutdown 未齐。
- `E_REND_ABANDON` 死码。
- powerd 起不来时无优雅关机出口。

---

## 11. 变更记录

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
