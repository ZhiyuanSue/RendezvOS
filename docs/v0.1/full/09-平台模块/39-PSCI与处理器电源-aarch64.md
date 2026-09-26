# PSCI 与处理器电源（aarch64）

v0.1 · 2026-09-25

本篇覆盖：`arch/aarch64/psci/psci.c`、`psci_call.S`、`include/arch/aarch64/psci/*.h`，以及与 SMP `cpu_on`、**`arch_shutdown` / powerd** 的边界。  
（路径在 **`arch/aarch64/psci/`**，不是 `modules/psci`。）

DTB 查找见 `36-DTB与设备树-aarch64.md`；SMP 串行启核见 `28-SMP启动与处理器拓扑.md`；关机 IPC / opcodes 见 kmsg 与 `40-错误码与panic.md`（本篇只钉终点：`PSCI SYSTEM_OFF`）。

**规范对照：** ARM DEN0022（PSCI）；ARM DEN0028（SMCCC）——`SMC` / `HVC #0`，参数在 `x0–x3`，返回在 `x0`。Function ID 用本仓库硬编码常量（如 `cpu_on` = `0xC4000003`）。

---

## 1. 概述

aarch64 上 **AP 上电** 与 **整机关机** 都走固件：内核不直接拨复位线，而是按 PSCI 约定发 SMC/HVC。`psci_init` 从 DTB 选定 method，把函数指针表 `psci_func` 填好；真正进固件的是汇编桩 `psci_smc` / `psci_hvc`。

PSCI 懂固件；powerd 只懂「谁请求关机」——两者解耦：

```text
优雅关机：request_poweroff → port "powerd" → SHUTDOWN kmsg
  → powerd_thread → kernel_halt → arch_shutdown → psci_func.system_off()
panic：直接 arch_shutdown，不经 powerd
REBOOT opcode：现多只打 log；arch_reset() 空壳
```

---

## 2. 目标与边界

**提供：** `psci_init`；`cpu_on` / `cpu_off` / `cpu_suspend` 等包装；`system_off` / `system_reset`；SMC/HVC 桩。

**不做：** 从 DT **单元格读出** function id（只检查属性**是否存在**，然后挂**硬编码** id）；完整 PSCI 1.x 全家桶都接到业务路径；把 powerd 协议正文写进本篇。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `arch_start_platform` | `psci_init()` |
| `arch_start_smp` | `psci_func.cpu_on(affinity, PHY(ap_entry), context=逻辑 id)` |
| `arch_shutdown`（`power_ctrl.h`） | `psci_func.system_off()` |
| panic / halt 路径 | 最终落到 `arch_shutdown` |

**缺口：** `psci_func.enable` 在 `smp.c` **不检查**——若 init 失败留下空 `cpu_on`，启核会空指针。启动 / 拓扑篇已警告。

---

## 4. 数据结构与不变量

- DTB：`compatible` 前缀 `arm,psci`（可命中 `arm,psci-0.2`）；`method` = `smc` \| `hvc`。  
- 属性存在性门闩：缺 `migrate` / `cpu_on` / `cpu_off` / `cpu_suspend` 任一 → print 并 **提前 return**（可能已 `enable=true` 且绑了 `system_off`，但 `cpu_on` 仍为 NULL）。依赖 QEMU 旧式 DT 常带这些属性名。  
- **不读** 属性里的 u32 function id——一律用头文件 `#define`（如 `psci_64_cpu_on_func_id`）。  
- `cpu_on`：`target_cpu` = MPIDR affinity（来自 cpu 节点 `reg`）；`context_id` = 稠密逻辑 id（与 x86「APIC id = 下标」不同，见拓扑篇）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `arch/aarch64/psci/psci.c` | init、包装、硬编码 id 调用 |
| `psci_call.S` | `smc #0` / `hvc #0` |
| `include/.../psci.h` | `psci_func_64`、function id |
| `power_ctrl.h` | `arch_shutdown` → `system_off`；`arch_reset` 空 |
| `arch/.../boot/smp.c` | 串行 `cpu_on` |
| `kernel/system/powerd.c` | IPC 关机策略（细节 → 40） |

---

## 6. 流程

### 6.1 硬件：SMCCC 一跳

```text
x0 = function_id
x1..x3 = args
SMC #0 或 HVC #0
x0 = 返回值（错误码见 psci_error.h）
```

`psci_call.S` 假定调用约定已把参数放进 `x0–x3`，指令后 `ret`。EL2/EL3 固件实现 PSCI；内核跑在 EL1。

### 6.2 `psci_init`

```text
find compatible "arm,psci"*
读 method → 选 psci_smc 或 psci_hvc
enable=true；绑 version / system_off / system_reset
若存在 migrate/cpu_on/cpu_off/cpu_suspend 属性名
  → 挂对应包装（仍用硬编码 id）
psci_print_version()  // 再发一次 PSCI_VERSION
```

### 6.3 启核

见拓扑篇：`enable-method` 必须为 `"psci"`（拒绝 spin-table）；串行 `cpu_on` + 等 `CPU_STATE`。

### 6.4 关机

```text
arch_shutdown → psci_func.system_off()
  → SMC/HVC(psci_system_off_func_id=0x84000008)
```

成功则不应返回。`arch_reset` 为空——reboot 未接线。

---

## 7. 公开 API

```c
void psci_init(void);
extern struct psci_func_64 psci_func;
/* 典型：psci_func.cpu_on / system_off / version … */

static inline void arch_shutdown(void);  /* power_ctrl.h */
static inline void arch_reset(void);     /* 空 */
```

上层优雅关机走 `rendezvos_request_poweroff` 一类 IPC，勿在业务里直接乱调 `cpu_off`。

---

## 8. 多架构

仅 aarch64。x86 关机走另一套 `power_ctrl`（非 PSCI）。

---

## 9. 测试

间接：virt SMP + shutdown。本篇未复测。

---

## 10. 限制与后续

- DT 只查属性存在性；标准 0.2 若用不同属性布局会半初始化。  
- smp 不查 `enable`；`cpu_on` 可能为空。  
- 不从 DT 读 function id。  
- `arch_reset` / reboot 空。  
- powerd 协议细节回链 40 / kmsg。

---

## 11. 变更记录

- 2026-09-25：语言轮——SMCCC/DEN0022；硬编码 id vs DT 属性门闩；`arch/` 真路径；关机终点。  
- 2026-08-29：整篇重做——arch 路径；powerd 边界；smp 不查 enable。  
- 2026-08-27：初稿。
