# 错误码与 panic

v0.1 · 2026-09-26

本篇覆盖：`include/rendezvos/error.h`、`kernel/system/panic.c`、`include/rendezvos/system/panic.h`、`kernel/system/powerd.c`、`include/rendezvos/system/powerd.h`、两侧 `arch/*/power_ctrl.h`。

PSCI `system_off` 终点见 `39-PSCI与处理器电源-aarch64.md`；关机 kmsg opcode 见 kmsg 篇；IPC 错误语义见 04 各篇。`common.h` / `limits.h` 非本篇主体。

**硬件对照：** x86 QEMU/SeaBIOS 常见关机口 `0x604` 写 `0x2000`（ACPI PM1a 风格）；复位口 `0x92`（`arch_reset`，powerd reboot **未接**）。aarch64 走 PSCI SYSTEM_OFF（SMCCC）。riscv64：`panic.c` 无 `#include` 分支——`arch_shutdown` 未齐。

---

## 1. 概述

core 用两套「出问题」的出口，不要混：

1. **可返回的错误：** `error_t`（`typedef int`）。成功 `REND_SUCCESS == 0`；枚举值是**正数**（从 `E_RENDEZVOS = 1024` 起），调用惯例 **`return -E_*`**（负值）。选 1024 起是为了给 Linux 负 errno 留空间——compat 自己映射，**不在** `error.h` 扩 Linux 全表。  
2. **不可恢复：** `kernel_panic(msg)` / `kernel_halt()` → 打日志 → `arch_shutdown()` → `while(1) arch_cpu_relax()`。无栈回溯、不停其他核、不经 powerd。

**优雅关机**（测例收尾、正常请求）：`rendezvos_request_poweroff()` → lookup 端口 `"powerd"` → `KMSG_OP_SYSTEM_POWER_SHUTDOWN` → powerd 线程 → `kernel_halt` → 同一套 `arch_shutdown`。

`panic.c` 注释写「上层应走 powerd」是**愿望**——`cmain`、未处理路径、heartbeat 失败等仍大量 **直接 panic**。两条路径并存是现状，不是 bug 文档能抹掉的。

---

## 2. 目标与边界

**提供：** 错误码约定与常用码表；panic/halt；powerd 客户端 inline + BSP 服务线程骨架。

**不做：** Linux errno 全表；panic 调试器 / kdump；跨核停机广播；完整 reboot（opcode 已登记，现只打 log）。

---

## 3. 分层与调用方

| 路径 | 行为 |
|------|------|
| 普通 API | 查 `==0` 或 `<0`；勿把正枚举当返回值直接甩给调用方 |
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
| `-E_REND_AGAIN` | `E_REND_AGAIN` | IPC 非阻塞重试 |
| `-E_REND_NOFOUND` | `E_REND_NOFOUND` | page_slice / radix / unmap / timer |
| `-E_REND_OVERFLOW` | `E_REND_OVERFLOW` | IPI / page_slice / radix（**不仅** IPI） |
| `-E_REND_NO_MEM` | `E_REND_NO_MEM` | buddy / page_slice |
| `-E_REND_RETRY` | `E_REND_RETRY` | buddy 少见；勿假定 OOM 一律 `E_RENDEZVOS` |
| `-E_REND_PORT_CLOSED` | `E_REND_PORT_CLOSED` | send / recv |
| `-E_REND_IPC` | `E_REND_IPC` | 协议 / 资源 |
| `-E_REND_NO_MSG` | `E_REND_NO_MSG` | 空队列 |
| `-E_REND_RC_UNEQUAL` | `E_REND_RC_UNEQUAL` | VSpace mask / teardown |
| `-E_REND_TEST` | `E_REND_TEST` | 几乎仅 `modules/test` |
| （无调用） | `E_REND_ABANDON` | **仅枚举**；死码，勿编造语义 |

### 4.2 `arch_shutdown`

| Arch | 行为 |
|------|------|
| x86_64 | `outw(0x604, 0x2000)`——QEMU 上常能关机；**不是** isa-debug-exit（`0x501` 一类） |
| aarch64 | `psci_func.system_off()`（须 `psci_init` 已绑指针） |
| riscv64 | `panic.c` 空 `#elif`——编译进 panic 时若无 `arch_shutdown` 会挂 |

`arch_reset`：x86 写 `0x92` bit0 风格复位；aarch64 空；powerd **REBOOT 不调用它**。

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
  → pr_error
  → arch_shutdown()     // 期望不返回
  → while (1) arch_cpu_relax()

rendezvos_request_poweroff()
  → thread_lookup_port("powerd")
  → kmsg SHUTDOWN → send_msg
  → powerd_thread: recv → kernel_halt() → 同上
```

powerd 在 port 创建/注册失败时线程直接 return——此后 `request_poweroff` 会 `-E_RENDEZVOS`，测例「跑完关机」可能失效。

---

## 7. 公开 API

```c
/* error.h — enum Error_t；返回用 -E_* */
void kernel_panic(const char *msg);  /* noreturn */
void kernel_halt(void);              /* noreturn */
error_t rendezvos_request_poweroff(void);
```

---

## 8. 多架构

上表。关机硬件/固件接口不同，错误码本身与 ISA 无关。

---

## 9. 测试

测例框架收尾常 `request_poweroff`（见 43）。本篇未复测。

---

## 10. 限制与后续

- panic 简陋；直接 panic 与 powerd 双路径并存。  
- reboot 未齐；riscv shutdown 未齐。  
- `E_REND_ABANDON` 死码。  
- powerd 起不来时无优雅关机出口。

---

## 11. 变更记录

- 2026-09-26：语言轮——负返回惯例；码表；x86 `0x604`/`0x92`；powerd vs 直接 panic；riscv 缺口。  
- 2026-08-29：整篇重做。  
- 2026-08-27：初稿。
