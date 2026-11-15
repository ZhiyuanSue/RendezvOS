# PSCI 与处理器电源（aarch64）

v0.1 · 2026-09-27

本篇覆盖：`arch/aarch64/psci/psci.c`、`psci_call.S`、`include/arch/aarch64/psci/*.h`，以及与 SMP `cpu_on`、**`arch_shutdown` / powerd** 的边界。
（路径在 **`arch/aarch64/psci/`**，不是 `modules/psci`。）

DTB 查找见 DTB 篇（`36`）；SMP 串行启核见 SMP 篇（`28`）；关机 IPC / opcodes 见 kmsg 篇（`21`）与 `40`（本篇只界定终点：`PSCI SYSTEM_OFF`）。

**规范对照：** ARM DEN0022（PSCI）；ARM DEN0028（SMCCC）——`SMC` / `HVC #0`，参数在 `x0–x3`，返回在 `x0`。Function ID 用本仓库硬编码常量（如 `cpu_on` = `0xC4000003`）。

---

## 1. 概述

aarch64 上 **AP 上电** 与 **整机关机** 都通过固件：内核不直接操作复位线，而是按 PSCI 约定发 SMC/HVC。`psci_init` 从 DTB 选定 method，填充函数指针表 `psci_func`；真正进入固件的是汇编桩 `psci_smc` / `psci_hvc`。

PSCI 规范还覆盖 **核 idle / 热插拔 / Trusted OS 迁移** 等；v0.1 **实际接入业务的只有两处**：SMP 启核用的 `cpu_on`（`smp.c`），以及关机终点 `system_off`（`arch_shutdown`）。`migrate` / `cpu_off` / `cpu_suspend` / `system_reset` 只在 init 时绑定进表，没有任何业务调用方。动态调频不在 PSCI 范围（ACPI / 平台侧）。

### 1.1 规范里的 idle / 功率域（本仓库未运行 idle 路径）

ARM 把核大致分成：

| 状态 | 直观 | 进入 |
|------|------|------|
| **Run** | 正在运行 | — |
| **StandBy** | 停止执行，上下文保留 | `WFI` / `WFE` |
| **Retention** | 对外调试更「冷」；对 OS 近似 StandBy | 平台定义 |
| **Powerdown** | 上下文可能丢失；唤醒需要入口地址等 | PSCI suspend / 更深状态 |

功率域通常构成树形：**system、cluster、core**。父节点要进入深睡，子节点必须先至少睡到同一深度（例如 core 还在 Retention 时，cluster 不能已 Powerdown）。协调模式默认是 **platform-coordinated**（固件/平台收齐各核意图）；**OS-initiated** 更复杂，v0.1 不考虑。

**热插拔 ≠ 深睡：** 热拔核后 OS 不再调度/收中断到该核，须显式 `cpu_on` 才回来；深睡核仍可被唤醒事件拉起。本仓库用 `cpu_on` 做 **拉起**，不做运行期 hotplug。

读到这里即可理解头文件里为何有 `cpu_suspend` 包装却无调用方——那是规范能力面，不是现行调度 idle。

PSCI 负责固件交互；powerd 只负责「谁请求关机」——两者解耦：

```text
优雅关机：rendezvos_request_poweroff()（powerd.h 内联）
  → 查 port "powerd" → KMSG_OP_SYSTEM_POWER_SHUTDOWN
  → powerd_thread → kernel_halt → arch_shutdown → psci_func.system_off()
panic：kernel_panic 直接 arch_shutdown，不经 powerd
REBOOT opcode：powerd 仅打印 "reboot not implemented"；arch_reset() 空壳
```

`powerd_thread` 由 `DEFINE_INIT` 起的内核线程创建并自注册 port `"powerd"`；`kernel_panic` / `kernel_halt` 调完 `arch_shutdown` 后都落入 `while(1) arch_cpu_relax()` 死循环——若 `system_off` 意外返回（固件不支持），机器只是停在这个空转循环里，并不会二次关机。

---

## 2. 目标与边界

**提供：** `psci_init`；`cpu_on` / `cpu_off` / `cpu_suspend` 等包装；`system_off` / `system_reset`；SMC/HVC 桩。

**不做：** 从 DT **单元格读出** function id（只检查属性**是否存在**，然后绑定**硬编码** id）；把完整 PSCI 1.x 的全部功能都接到业务路径；把 powerd 协议正文写进本篇。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| 平台启动（`arch/aarch64/boot/start_arch.c`） | `psci_init()`：DTB 构建、内核参数扫描之后、`gic_probe()` 之前 |
| `arch_start_smp`（`arch/aarch64/boot/smp.c`） | `psci_func.cpu_on(affinity, PHY(ap_entry), context=逻辑 id)` |
| `arch_shutdown`（`power_ctrl.h`，`static inline`） | `psci_func.system_off()`，**不检查** `enable` 或指针 |
| panic / halt 路径（`panic.c`） | `kernel_panic` / `kernel_halt` 最终落到 `arch_shutdown` |

全仓库范围内 `psci_init` 只有 `start_arch.c` 一处调用；`psci_func` 表项的实际消费者只有 `smp.c` 的 `cpu_on` 与 `arch_shutdown` 落到的 `system_off` 两个。

**缺口一（不查 enable）：** `psci_func.enable` 在 `smp.c` **不检查**——若 init 失败留下空 `cpu_on`，启核将触发空指针。`arch_shutdown` 同样裸调 `system_off`：init 失败时连 panic / halt 的收尾都会踩空函数指针。启核侧的下一类失败路径缺口见下文缺口二，SMP 篇（`28`）§6.5 对其另有完整记录。

**缺口二（启核失败不推进遍历）：** `arch_start_smp` 在 `psci_func.cpu_on(...) != psci_succ` 时 `NR_CPU++` 后 `continue`，而这条 `continue` **并不推进 `cpu_node`**（只有循环体末尾的 `next_cpu_node` 标签才推进）——于是失败核被**无限重试**，`NR_CPU` 无界增长，每轮还多分配 16 页栈。缺 `reg`、`enable-method` 非 `"psci"` 时的 `continue` 是同一类问题。机制详见 `28` §6.5。

**缺口三（未初始化指针入参）：** `smp.c` 对 `property_read_string(method_prop, &method_str)` 的返回码不检查。`property_read_string` 本身是 NULL 安全的（`prop` 为 NULL 时返回 `-E_IN_PARAM` 且不写 `str`），但调用方忽略错误码后，紧跟着的 `strcmp_s(method_str, ...)` 读的是**未初始化**的指针。QEMU virt 的 cpu 节点都带 `enable-method`，故实际不触发。

---

## 4. 数据结构与不变量

- DTB：`compatible` 含精确条目 `arm,psci`（list 常另有 `arm,psci-0.2`）；`method` = `smc` \| `hvc`。
- 属性存在性门闩：**依次**检查 `migrate` → `cpu_on` → `cpu_off` → `cpu_suspend` 四个属性名（长度实参含结尾 0：8/7/8/12）；缺任一即 print 并 **提前 return**，且**不清已置上的 `enable`**——可能停在 `enable=true` 且绑定了 `system_off`、但 `cpu_on` 仍为 NULL 的半初始化状态。依赖 QEMU 旧式 DT 常带这些属性名。
- 正常路径也只绑定 **22 个函数指针中的 7 个**（version、system_off、system_reset、migrate、cpu_on、cpu_off、cpu_suspend）；其余 15 个（affinity_info、mem_protect、feature、cpu_freeze、node_hw_state、system_suspend、state_residency 等）永远保持 NULL。
- `psci_print_version()` 只在四道门闩全部通过后执行；提前 return 时不会打印版本。
- **不读** 属性里的 u32 function id——一律用头文件 `#define`（如 `psci_64_cpu_on_func_id`）。
- `cpu_on`：`target_cpu` = MPIDR affinity（来自 cpu 节点 `reg`）；`context_id` = 稠密逻辑 id（与 x86「APIC id = 下标」不同，见 SMP 篇（`28`））。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `arch/aarch64/psci/psci.c` | init、包装、硬编码 id 调用 |
| `psci_call.S` | `smc #0` / `hvc #0` |
| `include/.../psci.h` | `psci_func_64`、function id |
| `power_ctrl.h` | `arch_shutdown` 调 `system_off`；`arch_reset` 空 |
| `arch/.../boot/smp.c` | 串行 `cpu_on` |
| `kernel/system/powerd.c` | IPC 关机策略（细节见 `40`） |

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
| `CPU_OFF` | `0x84000002` | —（规范未定义 64-bit 形式） | 自主关核（热插拔/idle） | 挂表，业务未用 |
| `CPU_SUSPEND` | `0x84000001` | `0xC4000001` | 核进入低功耗 | 挂表，业务未用 |
| `MIGRATE` | `0x84000005` | `0xC4000005` | 把 Trusted OS 迁到另一核 | 仅检查属性存在 |
| `SYSTEM_OFF` | `0x84000008` | — | 整机关机 | `arch_shutdown` 终点 |
| `SYSTEM_RESET` | `0x84000009` | — | 整机复位 | `arch_reset` 空壳 |

> 64-bit FID 范围 `0xC4000000..`，调用约定采用 SMC64；32-bit FID `0x84000000..` 采用 SMC32。aarch64 内核对 `CPU_ON` 等用 64-bit FID（`0xC4000003`），返回码在 `x0` 低 32 位。`CPU_OFF` 是特例：PSCI 规范与 `psci.h` 都只定义 32-bit 形式（`psci_cpu_off_func_id 0x84000002`），没有 `0xC4000002`。

### 6.2 `psci_init`

```text
find compatible 精确条目 "arm,psci"（list 逐条全等，非前缀）
  缺节点 / 缺 method / method 陌生 → print 后 enable=false 返回
读 method → 选 psci_smc 或 psci_hvc
enable=true；绑定 version / system_off / system_reset
按序门闩：migrate → cpu_on → cpu_off → cpu_suspend 属性名
  全在 → 绑定对应包装（仍用硬编码 id）；缺任一 → print 并提前 return
psci_print_version()  // 仅全通路径；再发一次 PSCI_VERSION
```

缺任一属性名则 print 并 **提前 return**，**不清 `enable`**——可能停在 `enable=true` 且绑定了 `system_off`、但后续指针仍为 NULL 的半初始化状态。

`psci_print_version` 把固件返回的版本号译成字母档：major 1 时 minor 0/1/2/3 → `C`/`D`/`E`/`F`；`0.2` → `B`；其余打 `error version`——即内核只认到 PSCI 1.0 时代（`psci.h` 的 FID 集合也止于此，`SYSTEM_OFF_2` 等 1.0+ 功能虽在头文件里有常量却无人调用——另注：`psci.h` 给 `psci_32_system_off_2_func_id` 定的值是 `0x84000015`，与规范表中 SYSTEM_OFF_2 的 `0x84000017` 不符，grep 对表时留意）。

### 6.3 启核

见 SMP 篇（`28`）：`enable-method` 必须为 `"psci"`（`strcmp_s` 限长 5 精确匹配，拒绝 spin-table）；串行 `cpu_on` + 等待 `CPU_STATE`。主核遍历 `type=cpu` 设备树节点、逐颗叫醒 AP 的流程：

```text
主核 (boot CPU)                          AP (被唤醒核)
─────────────────                       ─────────────────
for each cpu 节点 in DT:
  缺 reg / method 非 psci
    → print 错误 → continue  ← 缺口二：不推进节点
  reg == BSP_ID → 跳过，推进下一节点
  分 16 页 AP 栈（分配失败 → 打日志、推进下一节点，判空齐全）
  affinity = reg (MPIDR)
  psci_func.cpu_on(
    affinity,
    PHY(ap_entry),        ── SMC #0 ──►  固件上电该核
    context=NR_CPU)                       跳到 ap_entry (PA)
  若返回 != psci_succ:                      ap_entry (汇编):
    NR_CPU++ ; continue  ← 缺口二：同节点     设栈 → 进入 C 入口
      无限重试，NR_CPU 无界增长              置 CPU_STATE=enable
  成功：记录节点到 per_cpu；
    无超时忙等 CPU_STATE[NR_CPU]==enable ◄──  通知主核
    NR_CPU++
```

**失败路径：** 与直觉相反，三条错误 `continue`（缺 `reg`、method 非 psci、`cpu_on` 失败）**都不会推进 `cpu_node`**——循环里只有末尾的 `next_cpu_node` 标签才推进，所以失败核被**同一节点无限重试**：错误日志刷屏、`NR_CPU` 无界增长（`per_cpu` 下标随之越出上界）、`cpu_on` 失败路径每轮还额外分配 16 页栈。成功路径的等待也**没有超时**：`cpu_on` 成功但 AP 在写下 `cpu_enable` 前挂死，主核就永远停在忙等里（与 x86_64 的「十秒放弃」不对称）。完整机制与 x86 对照见 `28` §6.5；是否改动代码另行评估。

### 6.4 关机

```text
arch_shutdown（static inline）→ psci_func.system_off()
  → SMC/HVC(psci_system_off_func_id=0x84000008)
```

`system_off` 包装返回 `void`；成功则固件断电、不应返回。上游两条链路（实现均在 `kernel/system/`，协议细节见 `40`）：

- **优雅关机**：`rendezvos_request_poweroff()`（`powerd.h` 内联，查 port `"powerd"` 发 `KMSG_OP_SYSTEM_POWER_SHUTDOWN`；port 缺失返回 `-E_RENDEZVOS`）→ `powerd_thread` 收到后调 `kernel_halt()`。调用方包括 `main.c`（port 注册 / 消息循环失败）、`kernel/smp/smp.c`、`kernel/mm/mm_user_utils.c` 以及 `modules/test/test.c`（测例收尾）。
- **panic**：`kernel_panic` **不经 powerd** 直接 `arch_shutdown()`；两者随后都落入 `while(1) arch_cpu_relax()` 兜底。

`arch_reset()` 是空壳（源码注释：「no reboot here, we don't realise it now」）；`powerd` 收到 `KMSG_OP_SYSTEM_POWER_REBOOT` 也只打印 `"reboot not implemented"`。注意 `arch_shutdown` 不检查 `enable` / 指针：若 `psci_init` 失败，到这里的收尾会调用空函数指针。

---

## 7. 公开 API

本篇涉及的接口分布在四处：`psci.h`（`psci_init`、`psci_func` 表、`psci_*` 包装与 FID 常量）、`psci_call.S`（`psci_smc` / `psci_hvc` 桩）、`psci_error.h`（错误码枚举），以及本 arch 的电源终点——`arch_shutdown` 落到 SYSTEM_OFF（`power_ctrl.h`）。以头文件注释为准（均已与 `.c` / `.S` 核对）。

**本篇不涉及：** `arch_start_smp` 串行启核编排（见 `28`）；DTB 查找原语（见 `36`）；powerd / `request_poweroff` / `kernel_panic`（见 `40`）；x86 `power_ctrl`（非 PSCI）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 平台 | `build_device_tree` 之后 **`psci_init()`**，再往后才是 GIC |
| 启核 | `arch_start_smp` 调 **`psci_func.cpu_on(affinity, PHY(ap_entry), context=逻辑id)`** |
| 关机终点 | panic/halt，或 powerd 走到 halt 后 **`arch_shutdown`**，再 `psci_func.system_off()` |

优雅关机先经过 `"powerd"` IPC（`40`），勿在业务里直接 `cpu_off`。

### 7.2 Init 与表

```c
void psci_init(void);
extern struct psci_func_64 psci_func;  /* .enable, .cpu_on, .system_off, … */
```

| 要点 | 说明 |
|------|------|
| method | DT `method` = `smc` \| `hvc`，据此选桩 |
| compatible | 查找精确 `"arm,psci"`（NUL list **逐条全等**；常与 `arm,psci-0.2` 并存于同一 list） |
| FID | **硬编码** `#define`；只检查属性**名**是否存在，不读单元格 id |
| 半初始化状态 | 按 `migrate` → `cpu_on` → `cpu_off` → `cpu_suspend` 顺序设门闩，缺任一属性名时提前 return 且不清 `enable`；此时可能已 `enable=true` 且绑定了 `system_off`，但 `cpu_on` 仍为 NULL |
| 绑定数量 | 正常路径也只绑 22 个指针中的 7 个，其余 15 个恒为 NULL |
| smp 缺口 | **不**查 `psci_func.enable` 再调 `cpu_on`；`cpu_on` 失败的 `continue` 不推进设备树节点，同核无限重试（`28` §6.5） |

### 7.3 热路径包装

```c
i64 psci_cpu_on_64(u64 target_cpu, u64 entry_pa, u64 context_id);  /* FID 0xC4000003 */
void psci_system_off(void);   /* FID 0x84000008；成功不应返回 */
u32 psci_version(void);
/* + cpu_off / suspend / migrate / … 多数仅注册到表，业务少用 */
```

`target_cpu` = MPIDR affinity（cpu `@reg`）；`context_id` = 稠密逻辑 id。包装层最多传 3 个参数（`x4` 及以上从不使用）；`psci_system_off` / `psci_system_reset` 返回 `void`。

两处源码现状值得留意：`psci.h` 还声明了 13 个 `*_32` 原型函数（`psci_cpu_on_32` 等），**全仓库没有任何实现**——头文件注释自陈「虽想 32 位版本大概不会用，仍记录下来」，一旦有人调用就是链接错误；`psci.c` 里的 `static int psci_call_type` 只写不读，`psci_version` / `psci_cpu_off` 返回变量上的预初始化值（`-psci_not_support` / `-psci_denied`）也会被立即覆写，均为死代码。

### 7.4 Arch 电源终点

```c
static inline void arch_shutdown(void);  /* → psci_func.system_off() */
static inline void arch_reset(void);     /* 空；reboot 未接入 */
```

错误码（`psci_error.h`）：`enum psci_error_code` 取值 0..9——`succ=0`、`not_support`、`invalid_parameter`、`denied`、`already_on`、`on_pending`、`psci_internel_failure`（**源码即如此拼写**，grep 排障时注意是 `internel` 不是 `internal`）、`not_present`、`disabled`、`invalid_address`；固件按 SMCCC 约定返回**负值** `-psci_*`，与 `psci_succ` 比较即可判成败。

桩：`psci_smc` / `psci_hvc`（`psci_call.S`）——`x0–x3` 入、`x0` 出；仅一条 `smc`/`hvc #0` 加一条 `ret`，不建栈帧。

---

## 8. 多架构

仅 aarch64。x86 关机采用另一套 `power_ctrl`（非 PSCI）。

---

## 9. 测试

无专门的 PSCI 测例，靠间接链路覆盖：`modules/test/test.c` 在测例收尾处调 `rendezvos_request_poweroff()`。因此 QEMU virt（开 SMP）跑完测例后**正常退出**，即同时证明了 `psci_init` 选桩绑表、`cpu_on` 逐颗启核、powerd 收 kmsg、`kernel_halt` 落到 `SYSTEM_OFF` 的整条链路可用。反向的失败形态有辨识度：init 门闩不过打属性名错误、启核错误路径刷 `psci start ap %d fail`、`CPU_STATE` 忙等卡死则表现为无任何新输出的挂起。

---

## 10. 限制与后续

- **机制层面**：DT 只查属性名存在性、不读 function id，若 DT 布局不同可能停在半初始化状态（`enable=true` 但 `cpu_on` 为空）；只接入 `cpu_on` / `system_off` 两个业务点，`cpu_off` / `cpu_suspend` 等 idle 与热插拔能力有表无调用；`arch_reset` / reboot 空。
- **缺防护**：`smp.c` 不查 `enable` 即调 `cpu_on`；`arch_shutdown` 不查 `enable` 即调 `system_off`，init 失败时 panic / halt 收尾踩空指针；`property_read_string` 返回码被忽略，`method_str` 可能未初始化即入 `strcmp_s`。
- **启核错误路径（最重）**：三条错误 `continue` 均不推进设备树节点，同一节点无限重试且 `NR_CPU` 无界增长（§6.3、`28` §6.5）；成功路径等待 `CPU_STATE` 无超时，与 x86_64 的十秒放弃不对称。
- **死代码 / 拼写**：22 个函数指针只绑 7 个；13 个 `*_32` 原型声明无实现（调用即链接错误）；`psci_call_type` 只写不读；`psci_internel_failure` 拼写以源码为准。
- **powerd 边界**：关机 IPC 协议细节见 `40` 与 kmsg 篇（`21`）。

---

## 11. 变更记录
- 2026-11-15：Maintainer 复审前可读性通读（篇章架构 / 篇章引用 / 词句）。事实修正三处：① §7.4 错误码枚举实为 **0..9** 共 10 项（`psci_succ` 至 `psci_invalid_address`），原「取值 0..8」不实；② §3 缺口一「启动 / 拓扑篇已警告」归属不实——平台启动篇（`05`）未涉及此缺口，SMP 篇 §6.5 警告的对象是失败路径（缺口二），改为径指缺口二与 `28` §6.5；③ §6.2 补注 `psci_32_system_off_2_func_id` 仓库值 `0x84000015` 与规范表 `0x84000017` 不符。引用四处：篇首三个全文件名缩为 DTB 篇（`36`）/ SMP 篇（`28`）/ kmsg 篇（`21`）与 `40`；「拓扑篇」×3 统一为 SMP 篇（`28`）；§5 表「细节见 40」补反引号；§6.3 图内「缺口①／②」与 §3「缺口一／二／三」编号冲突，两处同属缺口二，按 §3 口径统一。词句六处：§1.1「子节点不能比父节点「更浅」地单独深睡」改写为「父节点要进入深睡，子节点必须先至少睡到同一深度」；§2 「不做」清单补「把」字免读成断言；§6.4 「、以及」顺接；§7 首电报体改为按四处文件列举；§7.3 / §10「约 13 个 `*_32`」改为准确值 13（实数恰 13）；§10 末条「回链」改为正式引用并加粗引导语。核实属实项：门闩顺序 migrate→cpu_on→cpu_off→cpu_suspend 与长度 8/7/8/12（含结尾 0）、22 指针仅绑 7、版本字母映射（1.0–1.3→C/D/E/F，0.2→B）、`psci_call_type` 只写不读、`psci_internel_failure` 拼写、`psci_call.S` 无栈帧、`property_read_string` 的 NULL 语义、poweroff 调用方四处（main/smp/mm_user_utils/test）、`arch_reset` 注释原文、`reboot not implemented` 打印、`CPU_OFF` 无 64-bit FID、缺门闩属性时 return 不清 `enable`。格式扫描（setext / 标题空行 / 分隔线 / 实体 / 引号）无命中。余沿 11-15 复核条目，未变。

- 2026-11-15：全文对照代码复核——**修正 CPU_OFF 64-bit FID**（表中原有并不存在的 `0xC4000002`；PSCI 规范与 `psci.h` 均只定义 32-bit `0x84000002`）；**重写启核失败路径**——三条错误 `continue` 均不推进设备树节点，同一节点无限重试且 `NR_CPU` 无界增长（与 `28` §6.5 对齐，修正原「跳到下一节点留空洞」的旧表述）；§3 补 `arch_shutdown` 同样裸调、`method_str` 未初始化即入 `strcmp_s` 两个缺口；§4/§7.2 补门闩顺序 migrate→cpu_on→cpu_off→cpu_suspend、提前 return 不清 `enable`、22 指针仅绑 7、版本打印仅全通路径执行；§6.2 补版本字母映射（1.0–1.3→C/D/E/F，0.2→B）；§6.4 补 powerd 线程来历、halt 后 `while(1)` 兜底、`reboot not implemented` 打印、`request_poweroff` 调用方清单；§7.3 补 `*_32` 原型无实现、`psci_call_type` 死变量、包装至多 3 参；§7.4 新增错误码枚举与 `psci_internel_failure` 拼写提醒、桩无栈帧；§9 重写为真实验证链（`test.c` 收尾调 `rendezvos_request_poweroff`）；§10 改分组条目；AP 栈分配判空齐全记为正面例子。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：任务 1/3/5 精读——改翻译腔——「内核不直接拨复位线」→「内核不直接操作复位线」、「真正进固件」→「真正进入固件」、「把函数指针表填好」→「填充函数指针表」、「未跑 idle 路径」→「未运行 idle 路径」、「在跑」→「正在运行」、「停执行，上下文在」→「停止执行，上下文保留」、「上下文可能丢」→「上下文可能丢失」、「唤醒要入口地址」→「唤醒需要入口地址」、「功率域常成树」→「功率域通常构成树形」、「拔核后」→「热拔核后」、「PSCI 懂固件；powerd 只懂」→「PSCI 负责固件交互；powerd 只负责」、「挂硬编码 id / 挂对应包装 / 挂表」→「绑定硬编码 id / 绑定对应包装 / 注册到表」、「全家桶」→「全部功能」、「把参数放进」→「将参数置入」、「内核跑在 EL1」→「内核运行于 EL1」、「调用约定走 SMC64/SMC32」→「调用约定采用 SMC64/SMC32」、「绑了 system_off」→「绑定了 system_off」、「等 CPU_STATE」→「等待 CPU_STATE」、「进 C 入口」→「进入 C 入口」、「启核会空指针」→「启核将触发空指针」、「关机走另一套」→「关机采用另一套」、「都走固件」→「都通过固件」、「优雅关机先走 powerd IPC」→「优雅关机先经过 powerd IPC」。
- 2026-10-04：补硬件知识——§6.1 SMCCC 寄存器调用约定图与 PSCI 常用 function ID 表（CPU_ON/OFF/SUSPEND、MIGRATE、SYSTEM_OFF/RESET，含 32/64-bit FID 范围说明）、§6.3 secondary boot 从固件唤醒到 CPU_STATE=ONLINE 的时序图；改翻译腔——「bring-up」→「拉起」、「现多只打 log」→「目前多数仅打印日志」、「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-02：§1.1 补 idle/功率域/热插拔 vs bring-up 规范语境；标明现行只接线 cpu_on + system_off。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：核对——compatible 精确 `"arm,psci"`（非前缀星号写法）；半初始化；`cpu_on` 失败仍 `NR_CPU++` 写入 §3/§6/§7/§10。
- 2026-09-26：§7 全文审阅——`psci.h`/`power_ctrl.h`/SMCCC 桩 Doxygen；硬编码 FID vs DT 门闩；vs `28`/`36`/`40`。
- 2026-09-25：语言轮——SMCCC/DEN0022；硬编码 id vs DT 属性门闩；`arch/` 真路径；关机终点。
- 2026-08-29：整篇重做——arch 路径；powerd 边界；smp 不查 enable。
- 2026-08-27：初稿。
