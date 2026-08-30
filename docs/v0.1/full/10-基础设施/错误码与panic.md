# 错误码与 panic

v0.1 · 2026-08-29

本篇覆盖：`include/rendezvos/error.h`、`kernel/system/panic.c`、`include/rendezvos/system/panic.h`、`kernel/system/powerd.c`、`include/rendezvos/system/powerd.h`、arch `power_ctrl.h`。

powerd / PSCI 关机终点见 `09-PSCI`；kmsg opcode 见 kmsg 篇；IPC 错误语义见 04 各篇。`common.h` / `limits.h` 非本篇主体。

---

## 1. 概述

**`error_t`（`int`）**：成功 `REND_SUCCESS == 0`；core 自有码是 **正数枚举**（`E_RENDEZVOS = 1024` 起）；调用惯例 **`return -E_*`**（负值）。动机：便于与 Linux 负 errno 风格对齐，compat 自己映射，**不在** `error.h` 扩 Linux 全表。

**不可恢复：** `kernel_panic(msg)` → `pr_error` + `arch_shutdown` + `while(1) arch_cpu_relax`。`kernel_halt()` 同类但无 `KERNEL PANIC:` 前缀。无栈回溯、不停其他核。

**优雅关机：** 测例/正常收尾应 `rendezvos_request_poweroff()`（lookup `"powerd"` → `KMSG_OP_SYSTEM_POWER_SHUTDOWN`）→ powerd → `kernel_halt`。注释「上层应走 powerd」是**愿望**——`cmain`/heartbeat/未处理 IRQ 等仍大量 **直接 panic**。

---

## 2. 目标与边界

**提供：** 错误码约定；panic/halt；powerd 客户端/服务端骨架说明。

**不做：** Linux errno 表；panic 调试器；跨核停机。

---

## 3. 分层与调用方

| 路径 | 行为 |
|------|------|
| API 返回 | 查 `==0` 或 `<0` |
| powerd | BSP-only `DEFINE_INIT` 起线程；线程内 create+register `"powerd"`，循环 recv |
| reboot opcode | 已登记，现多只打 log |
| panic | 直接 `arch_shutdown`，不经 powerd |

---

## 4. 数据结构与不变量

### 4.1 常用码（按密度）

| 码 | 域 | 备注 |
|----|-----|------|
| `-E_IN_PARAM` | 几乎全核 | 最常用 |
| `-E_RENDEZVOS` | 通用失败 | 兜底 |
| `-E_REND_AGAIN` | IPC 非阻塞 | 重试 |
| `-E_REND_NOFOUND` | page_slice/radix/unmap/timer | |
| `-E_REND_OVERFLOW` | IPI/page_slice/radix | 不仅 IPI |
| `-E_REND_NO_MEM` | buddy/page_slice | |
| `-E_REND_RETRY` | buddy 少见 | 勿假定 OOM 一律 `E_RENDEZVOS` |
| `-E_REND_PORT_CLOSED` | send/recv | |
| `-E_REND_IPC` | 协议/资源 | |
| `-E_REND_NO_MSG` | 空队列 | |
| `-E_REND_RC_UNEQUAL` | VSpace mask/teardown | |
| `-E_REND_TEST` | 测例 | 几乎仅 `modules/test` |
| `E_REND_ABANDON` | **仅枚举无调用** | 预留/死码，勿编造语义 |

### 4.2 arch_shutdown

| Arch | 行为 |
|------|------|
| x86 | `outw(0x604, 0x2000)`（ACPI-ish）；**不是** isa-debug-exit |
| aarch64 | `psci_func.system_off()` |
| riscv | `panic.c` 空分支——须写明未齐 |

---

## 5. 代码对应

见 § 真源路径。powerd client 在 `powerd.h` inline。

---

## 6. 流程

```text
panic/halt → arch_shutdown → spin relax
request_poweroff → port powerd → SHUTDOWN kmsg → halt → arch_shutdown
```

---

## 7. 公开 API

```c
void kernel_panic(const char *msg);
void kernel_halt(void);
error_t rendezvos_request_poweroff(void);
/* error_t / REND_SUCCESS / E_* ：error.h */
```

---

## 8. 多架构

上表 shutdown。

---

## 9. 测试

测例失败仍常 `request_poweroff`（见测试篇）。本篇未复测。

---

## 10. 限制与后续

- panic 简陋；直接 panic 与 powerd 双路径并存。  
- reboot 未齐。  
- `E_REND_ABANDON` 死码。

---

## 11. 变更记录

- 2026-08-29：整篇重做——负返回惯例；码表；x86 0x604；powerd vs 直接 panic；ABANDON 死码。
- 2026-08-27：初稿。
