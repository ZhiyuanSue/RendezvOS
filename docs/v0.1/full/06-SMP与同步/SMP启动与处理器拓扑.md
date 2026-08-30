# SMP 启动与处理器拓扑

v0.1 · 2026-08-29

本篇覆盖：`kernel/smp/smp.c`、`include/rendezvos/smp/smp.h`、`include/rendezvos/smp/cpu_id.h`、`include/rendezvos/cpu_topology.h`（占位）、`arch/*/boot` 里的 `arch_start_smp` / AP 入口。

per-CPU 布局见下一篇；软 IPI 门铃见软 IPI 篇；MADT/跳板细节可深链 `01-启动/平台启动-x86_64` 与 `09-ACPI`；PSCI 细节链 `09-PSCI`。BSP/`cmain` 总序见启动总览。

---

## 1. 概述

core 把「平台唤醒 AP」和「每核上线后的通用 C 路径」拆开：BSP 在 `cmain` 跑完一次 `do_init_call` 后再 `start_smp`；AP 只跑 `start_secondary_cpu`，在 `all_enabled` 栅栏后与 BSP 对称进入 `kernel_handle_msg`。

两套身份模型，别混成「逻辑 id = APIC id」。

| | x86_64 | aarch64 |
|--|--------|---------|
| 发现 | MADT Local APIC | DTB `type=cpu` + `enable-method=psci` |
| 逻辑下标 | **APIC id 直接当 `cpu_id`**（可稀疏、有空洞） | **稠密**（BSP=0，AP=1,2,…） |
| 硬件目标 | ICR dest = APIC id | PSCI `cpu_on(reg=affinity, context=逻辑id)` |
| `BSP_ID` | `arch_cpu_info` 写成 APICID | **固定 0**（要求 BSP `reg==0`） |

`cpu_topology.h` 在 v0.1 **多为空壳**——真正身份来自上表，不是拓扑树 API。

无 `SMP` 宏时 `start_smp` 为空。

---

## 2. 目标与边界

**提供：** `start_smp` / `start_secondary_cpu`；`all_enabled` 栅栏；`NR_CPU` / `CPU_STATE`；双 arch 身份模型说明。

**不做：** hotplug / offline 完整语义；NUMA；spin-table（aarch64 直接拒绝）。

---

## 3. 分层与调用方

**arch** — `arch_start_smp`（唤醒）；`arch_start_core`（每核）；`arch_start_platform`（**仅 BSP**）。

**initcall** — BSP：`do_init_call` **早于** `start_smp`；AP：`all_enabled` **后**再跑全表 → **必须幂等**（如 `powerd` 用 `cpu_number != BSP_ID` return）。

**compat** — 读在线核数 / 选核创建 server 时，分清两套 online 谓词（§4.2）。

---

## 4. 数据结构与不变量

### 4.1 计数与上限

| 符号 | 含义 |
|------|------|
| `RENDEZVOS_MAX_CPU_NUMBER` | 编译上限（默认 128，或 `MIN(NR_CPUS,128)`） |
| `NR_CPU` | **运行时计数**；初值 1；x86 由 MADT 改写，aarch64 在 `arch_start_smp` 重置为 1 再递增 |
| `NR_CPUS` | 构建宏；`==1` 时 aarch64 可不启 AP |
| `CPU_STATE[]` | per-CPU：`no_cpu` / `cpu_disable` / `cpu_enable` |

x86：`NR_CPU` = MADT 合法条目**个数**，**不是** `max_apic_id+1`。扫描唤醒时扫 `0..MAX-1`，对 `cpu_disable` 发 SIPI。

### 4.2 两套 “online”（勿混）

| API | 条件 | 适合 |
|-----|------|------|
| `cpu_is_online`（`smp.h`） | `CPU_STATE==enable` | 稀疏 APIC；IPI 用这个 |
| `cpu_id_is_online`（`thread.c`） | `cpu < NR_CPU` **且** `core_tm!=NULL` | 稠密假设；**APIC 稀疏（如 0,2 且 NR_CPU=2）会把 cpu 2 判离线** |

### 4.3 AP 重跑表

| 动作 | BSP | AP |
|------|-----|-----|
| `arch_start_platform` | 是 | **否** |
| `arch_start_core` | 是 | **是** |
| `init_proc` | 是 | **是** |
| `do_init_call` | SMP **前** | `all_enabled` **后** |
| `kernel_handle_msg` | 是 | 是 |

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/smp/smp.c` | `start_smp`、`start_secondary_cpu`、`all_enabled` |
| `arch/x86_64/.../smp` / boot | INIT/SIPI、跳板 `0x1000` |
| `arch/aarch64/...` + PSCI | `cpu_on`、`ap_entry` |
| `cpu_topology.h` | 占位 |

---

## 6. 流程

### 6.1 BSP → `start_smp`

`cmain`：… → `init_proc` → `do_init_call` → `kernel_port_register` → **`start_smp`** → `kernel_handle_msg`。

`start_smp`：`all_enabled=FAIL` → `arch_start_smp` → **`all_enabled=SUCC`**（arch 返回后置位；AP 在此之前 spin）。

### 6.2 AP：`start_secondary_cpu`

```text
cpu_id = setup_info->cpu_id
arch_enable_percpu → virt_mm_init → arch_start_core
CPU_STATE = enable
init_proc()
wait all_enabled
do_init_call() → kernel_handle_msg
```

### 6.3 x86 硬件：INIT / SIPI

| 步骤 | 含义 |
|------|------|
| 拷贝 `ap_start` → 物理 **`0x1000`** | SIPI 向量须 &lt;1MiB 页帧 |
| INIT（ALL_EXCLUDE_SELF）+ 延时 | 广播复位 |
| 逐核 SIPI×2，dest=APIC id，vector=`PPN(0x1000)` | Intel MP 双 SIPI；写入该核栈/`cpu_id` |
| 跳板 16→32→64 → `start_secondary_cpu` | 共享高半页表 |
| 擦跳板 / 临时页表 | 防误用 |

实现是 **ICR INIT + SIPI×2**，不是旧文 BIPI/FIPI。  
MADT **不看** Local APIC enable flags——disabled 条目仍可能占 `NR_CPU` 并被尝试启动。

### 6.4 aarch64 硬件：PSCI

| 步骤 | 含义 |
|------|------|
| DTB cpu，`enable-method=psci` | 否则拒绝 |
| `cpu_on(reg=affinity, context=NR_CPU)` | x0 到 AP = **稠密逻辑 id** |
| `ap_entry`：`str x0 → cpu_id` → EL1+MMU → secondary | |
| BSP wait `CPU_STATE[NR_CPU]==enable` 再 `NR_CPU++` | **串行**启核 |

### 6.5 platform vs core

| | platform（一次） | core（每核） |
|--|------------------|--------------|
| x86 | ACPI + PCI | GDT/TSS、interrupt/irq、**smp_ipi_init**、**tlb flush init**、syscall、timer… |
| aarch64 | DT、PSCI、**GIC distributor** | interrupt、**GIC CPU IF**、**smp_ipi_init**、timer、syscall（**无** TLB-IPI init） |

### 6.6 BSP_ID / percpu 时序注意

`arch_enable_percpu(BSP_ID)` 时 `BSP_ID` 往往仍为 **0**，之后 x86 才写成 APICID。APIC≠0 时 `percpu()` 仍指 slot 0，而大量路径用 `per_cpu(..., BSP_ID)`——依赖「QEMU BSP APIC=0」的隐含假设（启动总览已点）。

---

## 7. 公开 API

```c
void start_smp(void);
void start_secondary_cpu(void);   /* AP 入口，非上层调 */
bool cpu_is_online(cpu_id_t cpu);
/* NR_CPU, CPU_STATE, all_enabled, BSP_ID */
```

`cpu_id_is_online` / `task_manager_for_cpu` → 亲和性篇。

---

## 8. 多架构

上表。riscv/loongarch 非主线。

---

## 9. 测试

`smp_test` / affinity。本篇未复测。

---

## 10. 限制与后续

- 拓扑头空壳。  
- 两套 online 在稀疏 APIC 下冲突。  
- MADT disabled 未过滤；aarch64 失败仍可能 `NR_CPU++`。  
- BSP APIC≠0 的 GS 窗风险。

---

## 11. 变更记录

- 2026-08-29：整篇重做——双身份模型；NR_CPU/online 二分；INIT-SIPI/PSCI 硬件链；AP 重跑表；纠正「APIC 映射表」叙事。
- 2026-08-27：初稿。
