# PSCI 与处理器电源（aarch64）

v0.1 · 2026-09-27

本篇覆盖：`arch/aarch64/psci/psci.c`、`psci_call.S`、`include/arch/aarch64/psci/*.h`，以及与 SMP `cpu_on`、**`arch_shutdown` / powerd** 的边界。
（路径在 **`arch/aarch64/psci/`**，不是 `modules/psci`。）

DTB 查找见 `36-DTB与设备树-aarch64.md`；SMP 串行启核见 `28-SMP启动与处理器拓扑.md`；关机 IPC / opcodes 见 kmsg 与 `40-错误码与panic.md`（本篇只界定终点：`PSCI SYSTEM_OFF`）。

**规范对照：** ARM DEN0022（PSCI）；ARM DEN0028（SMCCC）——`SMC` / `HVC #0`，参数在 `x0–x3`，返回在 `x0`。Function ID 用本仓库硬编码常量（如 `cpu_on` = `0xC4000003`）。

---

## 1. 概述

aarch64 上 **AP 上电** 与 **整机关机** 都通过固件：内核不直接操作复位线，而是按 PSCI 约定发 SMC/HVC。`psci_init` 从 DTB 选定 method，填充函数指针表 `psci_func`；真正进入固件的是汇编桩 `psci_smc` / `psci_hvc`。

PSCI 规范还覆盖 **核 idle / 热插拔 / Trusted OS 迁移** 等；v0.1 **实际接入的只有**：secondary `cpu_on`、以及关机终点 `system_off`（另有包装但未接入业务的 `cpu_off` / `cpu_suspend` / `system_reset`）。动态调频不在 PSCI 范围（ACPI / 平台侧）。

### 1.1 规范里的 idle / 功率域（本仓库未运行 idle 路径）

ARM 把核大致分成：

| 状态 | 直观 | 进入 |
|------|------|------|
| **Run** | 正在运行 | — |
| **StandBy** | 停止执行，上下文保留 | `WFI` / `WFE` |
| **Retention** | 对外调试更「冷」；对 OS 近似 StandBy | 平台定义 |
| **Powerdown** | 上下文可能丢失；唤醒需要入口地址等 | PSCI suspend / 更深状态 |

功率域通常构成树形：**system → cluster → core**。子节点不能比父节点「更浅」地单独深睡（例如 core Retention 时 cluster 不能已 Powerdown）。协调模式默认是 **platform-coordinated**（固件/平台收齐各核意图）；**OS-initiated** 更复杂，v0.1 不考虑。

**热插拔 ≠ 深睡：** 热拔核后 OS 不再调度/收中断到该核，须显式 `cpu_on` 才回来；深睡核仍可被唤醒事件拉起。本仓库用 `cpu_on` 做 **拉起**，不做运行期 hotplug。

读到这里即可理解头文件里为何有 `cpu_suspend` 包装却无调用方——那是规范能力面，不是现行调度 idle。

PSCI 负责固件交互；powerd 只负责「谁请求关机」——两者解耦：

```text
优雅关机：request_poweroff → port "powerd" → SHUTDOWN kmsg
  → powerd_thread → kernel_halt → arch_shutdown → psci_func.system_off()
panic：直接 arch_shutdown，不经 powerd
REBOOT opcode：目前多数仅打印日志；arch_reset() 空壳
```

---

## 2. 目标与边界

**提供：** `psci_init`；`cpu_on` / `cpu_off` / `cpu_suspend` 等包装；`system_off` / `system_reset`；SMC/HVC 桩。

**不做：** 从 DT **单元格读出** function id（只检查属性**是否存在**，然后绑定**硬编码** id）；完整 PSCI 1.x 全部功能都接到业务路径；把 powerd 协议正文写进本篇。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `arch_start_platform` | `psci_init()` |
| `arch_start_smp` | `psci_func.cpu_on(affinity, PHY(ap_entry), context=逻辑 id)` |
| `arch_shutdown`（`power_ctrl.h`） | `psci_func.system_off()` |
| panic / halt 路径 | 最终落到 `arch_shutdown` |

**缺口：** `psci_func.enable` 在 `smp.c` **不检查**——若 init 失败留下空 `cpu_on`，启核将触发空指针。启动 / 拓扑篇已警告。

**另一缺口（启核计数）：** `arch_start_smp` 在 `psci_func.cpu_on(...) != psci_succ` 时仍 **`NR_CPU++` 后 `continue`**——失败核也占一个稠密下标，计数偏大 / 留下空洞（与 `28` 一致）。

---

## 4. 数据结构与不变量

- DTB：`compatible` 含精确条目 `arm,psci`（list 常另有 `arm,psci-0.2`）；`method` = `smc` \| `hvc`。
- 属性存在性门闩：缺 `migrate` / `cpu_on` / `cpu_off` / `cpu_suspend` 任一 → print 并 **提前 return**（可能已 `enable=true` 且绑定了 `system_off`，但 `cpu_on` 仍为 NULL）。依赖 QEMU 旧式 DT 常带这些属性名。
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

ARM SMCCC（SMC Calling Convention）规定在 `EL1` 通过 `SMC #0` 陷入 `EL2/EL3` 固件、或 `HVC #0` 陷入 hypervisor 的寄存器调用约定。PSCI 是 SMCCC 之上的一个服务族，function ID 标识具体操作：

```text
调用前（调用方填）            调用后（固件返回）
┌──────────────────┐         ┌──────────────────┐
│ x0  = function_id │  SMC #0 │ x0  = 返回码       │
│ x1  = arg0        │  ────►  │ x1..x3 = out 参数  │
│ x2  = arg1        │  或     │ （PSCI 多数只用 x0）│
│ x3  = arg2        │  HVC #0 └──────────────────┘
└──────────────────┘
```

`psci_call.S` 假定调用约定已将参数置入 `x0–x3`，指令后 `ret`。EL2/EL3 固件实现 PSCI；内核运行于 EL1。

PSCI 常用 function ID（本仓库硬编码常量，与 ARM DEN0022 一致）：

| 函数 | 32-bit FID | 64-bit FID | 用途 | 本仓库接入 |
|------|-----------|-----------|------|-----------|
| `PSCI_VERSION` | `0x84000000` | — | 查询 PSCI 版本 | init 时调用 |
| `CPU_ON` | `0x84000003` | `0xC4000003` | 启动/唤醒某核 | secondary boot 主路径 |
| `CPU_OFF` | `0x84000002` | `0xC4000002` | 自主关核（热插拔/idle） | 挂表，业务未用 |
| `CPU_SUSPEND` | `0x84000001` | `0xC4000001` | 核进入低功耗 | 挂表，业务未用 |
| `MIGRATE` | `0x84000005` | `0xC4000005` | 把 Trusted OS 迁到另一核 | 仅检查属性存在 |
| `SYSTEM_OFF` | `0x84000008` | — | 整机关机 | `arch_shutdown` 终点 |
| `SYSTEM_RESET` | `0x84000009` | — | 整机复位 | `arch_reset` 空壳 |

> 64-bit FID 范围 `0xC4000000..`，调用约定采用 SMC64；32-bit FID `0x84000000..` 采用 SMC32。aarch64 内核对 `CPU_ON` 等用 64-bit FID（`0xC4000003`），返回码在 `x0` 低 32 位。

### 6.2 `psci_init`

```text
find compatible 精确条目 "arm,psci"（list 逐条全等，非前缀）
读 method → 选 psci_smc 或 psci_hvc
enable=true；绑定 version / system_off / system_reset
若存在 migrate/cpu_on/cpu_off/cpu_suspend 属性名
  → 绑定对应包装（仍用硬编码 id）
psci_print_version()  // 再发一次 PSCI_VERSION
```

缺任一属性名则 print 并 **提前 return**（可能已 `enable=true` 且绑定了 `system_off`，但后续指针仍为 NULL）。

### 6.3 启核

见拓扑篇：`enable-method` 必须为 `"psci"`（拒绝 spin-table）；串行 `cpu_on` + 等待 `CPU_STATE`。secondary boot 从固件唤醒到进入内核调度的大致流程：

```text
主核 (boot CPU)                          AP (被唤醒核)
─────────────────                       ─────────────────
for each cpu 节点 in DT:
  affinity = reg (MPIDR)
  psci_func.cpu_on(
    affinity,
    PHY(ap_entry),        ── SMC #0 ──►  固件上电该核
    context=逻辑id)                       跳到 ap_entry (PA)
  若返回 != psci_succ:
    NR_CPU++ ; continue  ← 缺口          ap_entry (汇编):
  else:                                   设栈 → 进入 C 入口
    NR_CPU++ ; continue                   置 CPU_STATE=ONLINE
  等待 CPU_STATE == ONLINE  ◄─────────────  通知主核
```

**失败路径：** `cpu_on` 返回非 `psci_succ` 时实现仍 `NR_CPU++` 再扫描下一 cpu 节点——不回滚该下标。

### 6.4 关机

```text
arch_shutdown → psci_func.system_off()
  → SMC/HVC(psci_system_off_func_id=0x84000008)
```

成功则不应返回。`arch_reset` 为空——reboot 未接入。

---

## 7. 公开 API

本篇涉及的接口分布在：aarch64 PSCI / SMCCC——`psci_init`、`psci_func` 表、`psci_*` 包装与 FID 常量（`psci.h`）、`psci_smc`/`psci_hvc` 桩、错误码枚举；以及本 arch 的 **`arch_shutdown` → SYSTEM_OFF**（`power_ctrl.h`）。以头文件注释为准（`psci.h` / `power_ctrl.h`；已与 `.c` / `.S` 核对）。

**本篇不涉及：** `arch_start_smp` 串行启核编排 → `28`；DTB 查找原语 → `36`；powerd / `request_poweroff` / `kernel_panic` → `40`；x86 `power_ctrl`（非 PSCI）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 平台 | `build_device_tree` → **`psci_init()`** →（其后）GIC |
| 启核 | `arch_start_smp` → **`psci_func.cpu_on(affinity, PHY(ap_entry), context=逻辑id)`** |
| 关机终点 | panic/halt 或 powerd→halt → **`arch_shutdown`** → `psci_func.system_off()` |

优雅关机先经过 `"powerd"` IPC（`40`），勿在业务里直接 `cpu_off`。

### 7.2 Init 与表

```c
void psci_init(void);
extern struct psci_func_64 psci_func;  /* .enable, .cpu_on, .system_off, … */
```

| 要点 | 说明 |
|------|------|
| method | DT `method` = `smc` \| `hvc` → 选桩 |
| compatible | 查找精确 `"arm,psci"`（NUL list **逐条全等**；常与 `arm,psci-0.2` 并存于同一 list） |
| FID | **硬编码** `#define`；只检查属性**名**是否存在，不读单元格 id |
| 半初始化状态 | 缺 `migrate`/`cpu_on`/… 任一属性名时提前 return；此时可能已 `enable=true` 且绑定了 `system_off`，但 `cpu_on` 仍为 NULL |
| smp 缺口 | **不**查 `psci_func.enable` 再调 `cpu_on`；**`cpu_on` 失败仍可能 `NR_CPU++`** |

### 7.3 热路径包装

```c
i64 psci_cpu_on_64(u64 target_cpu, u64 entry_pa, u64 context_id);  /* FID 0xC4000003 */
void psci_system_off(void);   /* FID 0x84000008；成功不应返回 */
u32 psci_version(void);
/* + cpu_off / suspend / migrate / … 多数仅注册到表，业务少用 */
```

`target_cpu` = MPIDR affinity（cpu `@reg`）；`context_id` = 稠密逻辑 id。

### 7.4 Arch 电源终点

```c
static inline void arch_shutdown(void);  /* → psci_func.system_off() */
static inline void arch_reset(void);     /* 空；reboot 未接入 */
```

桩：`psci_smc` / `psci_hvc`（`psci_call.S`）——`x0–x3` 入、`x0` 出。

---

## 8. 多架构

仅 aarch64。x86 关机采用另一套 `power_ctrl`（非 PSCI）。

---

## 9. 测试

间接：virt SMP + shutdown。

---

## 10. 限制与后续

- DT 只查属性名是否存在；若 0.2 布局不同，可能停在半初始化状态（`enable=true` 但 `cpu_on` 为空）。
- smp 不查 `enable`；`cpu_on` 可能为空。
- **`cpu_on` 失败仍 `NR_CPU++`**（稠密下标空洞 / 计数偏大）。
- 不从 DT 读 function id。
- `arch_reset` / reboot 空。
- powerd 协议细节回链 40 / kmsg。

---

## 11. 变更记录

- 2026-10-05：最终词句顺畅。
- 2026-10-05：任务 1/3/5 精读——改翻译腔——「本篇只钉终点」→「本篇只界定终点」、「内核不直接拨复位线」→「内核不直接操作复位线」、「真正进固件」→「真正进入固件」、「把函数指针表填好」→「填充函数指针表」、「未跑 idle 路径」→「未运行 idle 路径」、「在跑」→「正在运行」、「停执行，上下文在」→「停止执行，上下文保留」、「上下文可能丢」→「上下文可能丢失」、「唤醒要入口地址」→「唤醒需要入口地址」、「功率域常成树」→「功率域通常构成树形」、「拔核后」→「热拔核后」、「PSCI 懂固件；powerd 只懂」→「PSCI 负责固件交互；powerd 只负责」、「挂硬编码 id / 挂对应包装 / 挂表」→「绑定硬编码 id / 绑定对应包装 / 注册到表」、「全家桶」→「全部功能」、「把参数放进」→「将参数置入」、「内核跑在 EL1」→「内核运行于 EL1」、「调用约定走 SMC64/SMC32」→「调用约定采用 SMC64/SMC32」、「绑了 system_off」→「绑定了 system_off」、「等 CPU_STATE」→「等待 CPU_STATE」、「进 C 入口」→「进入 C 入口」、「启核会空指针」→「启核将触发空指针」、「关机走另一套」→「关机采用另一套」、「都走固件」→「都通过固件」、「优雅关机先走 powerd IPC」→「优雅关机先经过 powerd IPC」。
- 2026-10-04：补硬件知识——§6.1 SMCCC 寄存器调用约定图与 PSCI 常用 function ID 表（CPU_ON/OFF/SUSPEND、MIGRATE、SYSTEM_OFF/RESET，含 32/64-bit FID 范围说明）、§6.3 secondary boot 从固件唤醒到 CPU_STATE=ONLINE 的时序图；改翻译腔——「bring-up」→「拉起」、「现多只打 log」→「目前多数仅打印日志」、「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-02：§1.1 补 idle/功率域/热插拔 vs bring-up 规范语境；标明现行只接线 cpu_on + system_off。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：核对——compatible 精确 `"arm,psci"`（非前缀星号写法）；半初始化；`cpu_on` 失败仍 `NR_CPU++` 写入 §3/§6/§7/§10。
- 2026-09-26：§7 全文审阅——`psci.h`/`power_ctrl.h`/SMCCC 桩 Doxygen；硬编码 FID vs DT 门闩；vs `28`/`36`/`40`。
- 2026-09-25：语言轮——SMCCC/DEN0022；硬编码 id vs DT 属性门闩；`arch/` 真路径；关机终点。
- 2026-08-29：整篇重做——arch 路径；powerd 边界；smp 不查 enable。
- 2026-08-27：初稿。
