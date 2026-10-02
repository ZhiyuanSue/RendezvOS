# SMP 启动与处理器拓扑

v0.1 · 2026-09-27

本篇覆盖：`kernel/smp/smp.c`、`include/rendezvos/smp/smp.h`、`include/rendezvos/smp/cpu_id.h`、`include/rendezvos/cpu_topology.h`（占位）、`arch/x86_64/boot/smp.c` + `boot.S`（`ap_start`）、`arch/aarch64/boot/smp.c` + `boot.S`（`ap_entry`）、PSCI `cpu_on`。

per-CPU 布局见下一篇；软 IPI 门铃见软 IPI 篇；MADT / 跳板细节可深链 `01-启动/平台启动-x86_64` 与 `09-ACPI`；PSCI 细节链 `09-PSCI`；ICP / ICR 字段见 `26-APIC`。BSP / `cmain` 总序见启动总览。

**官方手册：** Intel SDM（MP 初始化、INIT-SIPI-SIPI、ICR）；ARM DEN0022（PSCI）+ ARM ARM（二次核入口 EL）；ACPI MADT；DTB `cpu` / `enable-method`。

---

## 1. 概述

core 把「平台唤醒 AP」和「每核上线后的通用 C 路径」拆开：BSP 在 `cmain` 跑完一次 `do_init_call` 后再 `start_smp`；AP 只跑 `start_secondary_cpu`，在 `all_enabled` 栅栏后与 BSP 对称进入 `kernel_handle_msg`。

两套身份模型，别混成「逻辑 id = APIC id」。

| | x86_64 | aarch64 |
|--|--------|---------|
| 发现 | MADT Local APIC | DTB `type=cpu` + `enable-method=psci` |
| 逻辑下标 | **APIC id 直接当 `cpu_id`**（可稀疏、有空洞） | **稠密**（BSP=0，AP=1,2,…） |
| 硬件目标 | ICR dest = APIC id | PSCI `cpu_on`：`reg`=DTB affinity，`context`=逻辑 id |
| `BSP_ID` | `arch_cpu_info` 写成 APICID | **固定 0**（要求 BSP `reg==0`） |

`cpu_topology.h` 在 v0.1 **多为空壳**——真正身份来自上表，不是拓扑树 API。

无 `SMP` 宏时 `start_smp` 为空。

---

## 2. 目标与边界

**提供：** `start_smp` / `start_secondary_cpu`；`all_enabled` 栅栏；`NR_CPU` / `CPU_STATE`；双 arch 身份模型与硬件唤醒链。

**不做：** hotplug / offline 完整语义；NUMA；spin-table（aarch64 直接拒绝）。

---

## 3. 分层与调用方

**arch** — `arch_start_smp`（唤醒）；`arch_start_core`（每核）；`arch_start_platform`（**仅 BSP**）。

**initcall** — 每个核调用一次 `do_init_call`。BSP 的这一次在 `start_smp` 之前，AP 的这一次在 `all_enabled` 之后，因此 BSP 一定先做完。函数里若只该在 BSP 或某个指定核上做事，自己判断当前核（如 `powerd` 在 `cpu_number != BSP_ID` 时返回）。

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
| `arch/x86_64/boot/smp.c` | 拷贝跳板、INIT、SIPI×2、等 `CPU_STATE` |
| `arch/x86_64/boot/boot.S` `ap_start` | 实模式→32→64 → `start_secondary_cpu` |
| `arch/aarch64/boot/smp.c` | DTB 枚举 + `psci_func.cpu_on` 串行启核 |
| `arch/aarch64/boot/boot.S` `ap_entry` | 收 context（逻辑 id）→ EL1+MMU → secondary |
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

### 6.3 x86 硬件：INIT-SIPI-SIPI（SDM + ICR）

Intel MP 启动惯例（本实现遵循）：

1. **把 AP 入口码放到 &lt;1MiB 的页对齐物理地址**——SIPI 的向量字段是 **页帧号**（`PPN(addr)`），不是任意线性地址。本仓库用物理 **`0x1000`**（`ap_phy_addr` / `_RENDEZVOS_X86_64_AP_PHY_ADDR_`）。
2. **INIT IPI**（delivery mode INIT，shorthand ALL_EXCLUDE_SELF）——把其它核拉回等待 STARTUP 的状态；延时约 10ms。
3. **SIPI × 2**（delivery mode STARTUP，dest=目标 APIC id，vector=`PPN(0x1000)`）——Intel 建议发两次；中间 `udelay(200)`。
4. AP 在 `ap_start`：16 位 → 临时 GDT → 32 位 → 开长模式 → 共享高半页表 → `start_secondary_cpu`。
5. BSP 轮询该核 `CPU_STATE` 变 `enable`（超时约 10s 打日志）；全部尝试完后 **擦跳板** + 清临时页表，防残留入口被滥用。

**与 APIC 篇的衔接：** 全部经 `APIC_send_IPI` → ICR（xAPIC 先 HIGH 后 LOW）。此时 LAPIC 必须已 enable；`arch_start_smp` 开头还会 **`arch_disable_irq`**，避免启动窗口被设备 IRQ 打断。

**栈与 cpu_id：** 发 SIPI 前 BSP 在 `setup_info` 里写入该 AP 的 `ap_boot_stack_ptr` 与 `cpu_id=i`（i 即 APIC id）。跳板读这些字段进 C。

**MADT：** 枚举 Local APIC 条目填 `CPU_STATE` / `NR_CPU`；**不看** enable 标志——disabled 条目仍可能占计数并被尝试启动（缺口）。

实现是 **ICR INIT + SIPI×2**，不是旧文 BIPI/FIPI。

### 6.4 aarch64 硬件：PSCI `CPU_ON`（DEN0022）

ARM 二次核不跑 x86 式 SIPI。virt / 服务器路径靠 **PSCI**：

| 步骤 | 含义 |
|------|------|
| DTB `cpu`，`enable-method=psci` | 否则拒绝（spin-table 等未接） |
| `psci_func.cpu_on(affinity, entry_pa, context)` | `affinity` 来自 DTB `cpu` 的 `reg`（MPIDR 亲和字段）；`entry` = `ap_entry` 物理地址；**context = 稠密逻辑 id（当前 `NR_CPU`）** |
| `ap_entry` | `str` context → `cpu_id`；升 EL1、开 MMU；进 secondary |
| BSP | wait `CPU_STATE[NR_CPU]==enable` 再 `NR_CPU++`——**串行**启核 |

失败时实现仍可能 `NR_CPU++`（代码路径）——会留下空洞 / 计数偏大，属已知缺口。

PSCI 本身经 SMC / HVC；属性与 function id 见 PSCI 篇。本篇只写清「谁把哪颗核叫醒、context 里塞什么」。

### 6.5 platform vs core

| | platform（一次） | core（每核） |
|--|------------------|--------------|
| x86 | ACPI + PCI | GDT/TSS、interrupt/irq、**smp_ipi_init**、**tlb flush init**、syscall、timer… |
| aarch64 | DT、PSCI、**GIC distributor** | interrupt、**GIC CPU IF**、**smp_ipi_init**、timer、syscall（**无** TLB-IPI init） |

### 6.6 BSP_ID / percpu 时序注意

`arch_enable_percpu(BSP_ID)` 时 `BSP_ID` 往往仍为 **0**，之后 x86 才写成 APICID。APIC≠0 时 `percpu()` 仍指 slot 0，而大量路径用 `per_cpu(..., BSP_ID)`——依赖「QEMU BSP APIC=0」的隐含假设（启动总览已点）。

---

## 7. 公开 API

本篇拥有：SMP 上线约定——`start_smp` / `start_secondary_cpu`、`cpu_is_online`、`CPU_STATE` / `cpu_status`、`NR_CPU`、`all_enabled`、`BSP_ID`、`arch_start_smp`（双 arch）。说明改写自 `smp.h` / `cpu_id.h` / `arch/*/smp.h` Doxygen（已与 `smp.c`、`arch/*/boot/smp.c` 核对）。

**本篇不拥有：** `cpu_id_is_online` / `task_manager_for_cpu` → `16`；per-CPU `percpu()` 布局 → `29`；软 IPI → `30`；MADT / PSCI 表细节 → `35` / `39`；`arch_start_core` 体内序 → `04`/`05`。

`cpu_topology.h`：**无可用 API**（结构体占位）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| BSP | `cmain`：… → `init_proc` → **`do_init_call`** → `kernel_port_register` → **`start_smp`** → `kernel_handle_msg` |
| `start_smp` 内 | `all_enabled=FAIL` → **`arch_start_smp`** → `all_enabled=SUCC` |
| AP | 跳板 → **`start_secondary_cpu`**：percpu → virt_mm → `arch_start_core` → `CPU_STATE=enable` → `init_proc` → **等 all_enabled** → `do_init_call` → `kernel_handle_msg` |

无 `SMP` 宏时 `start_smp` 直接 return。

### 7.2 上线与 online

```c
void start_smp(struct setup_info *arch_setup_info);
void start_secondary_cpu(struct setup_info *arch_setup_info); /* asm AP 入口 */
bool cpu_is_online(cpu_id_t cpu_id);

extern int NR_CPU;                 /* 运行时计数；初值 1 */
extern cpu_id_t BSP_ID;
enum cpu_status { no_cpu, cpu_disable, cpu_enable };
/* DEFINE_PER_CPU(volatile u64, CPU_STATE) */
```

| 接口 / 符号 | 说明 |
|-------------|------|
| `start_smp` | 参数是 `setup_info*`（**不是** `void`）。无 SMP 构建为空操作 (no-op)。 |
| `start_secondary_cpu` | AP 专用；声明在 `smp.h`，定义在 `smp.c`。 |
| `cpu_is_online` | `CPU_STATE==enable`；稀疏 APIC 安全；**IPI 用这个**。 |
| `cpu_id_is_online` | **不归本篇**——另要求 `cpu < NR_CPU` 且 `core_tm`；稀疏 APIC 会误判（见 `16`）。 |
| `NR_CPU` | x86=MADT 合法条目个数；aarch64 在 `arch_start_smp` 重置为 1 再串行 ++。 |
| `BSP_ID` | x86←APICID；aarch64 固定 0。 |

### 7.3 arch 唤醒

```c
void arch_start_smp(struct setup_info *arch_setup_info);
```

| arch | 行为要点 |
|------|----------|
| x86 | trampoline@`0x1000`；INIT all-ex-self → 对每个 `cpu_disable` SIPI×2（dest=APIC id）；擦跳板。 |
| aarch64 | DTB `cpu`+`psci`；`cpu_on(affinity=DTB reg, ap_entry_pa, context=稠密 id)`；失败仍可能 `NR_CPU++`（缺口）。 |

### 7.4 拓扑

`cpu_topology.h` 仅有空结构——**无** walker / 查询 API。身份以 MADT / DTB+PSCI 为准。

---


## 8. 多架构

上表。riscv / loongarch 非主线。

---

## 9. 测试

`smp_test` / 绑核测例。本篇未复测。

---

## 10. 限制与后续

- 拓扑头空壳。
- 两套 online 在稀疏 APIC 下冲突。
- MADT disabled 未过滤；aarch64 失败仍可能 `NR_CPU++`。
- BSP APIC≠0 的 GS 窗风险。

---

## 11. 变更记录

- 2026-09-27：中文措辞整理——PSCI `affinity` 句通顺化；弱化「钉 / 真源 / 契约」堆砌；空操作标 (no-op)。
- 2026-09-26：§7 全文审阅——纠正 `start_smp(setup_info*)`；补 `start_secondary_cpu` 声明与 Doxygen；划清 `cpu_is_online` vs `cpu_id_is_online`；拓扑头明确无 API。
- 2026-09-25：语言整理；§6.3/§6.4 扩写 INIT-SIPI / PSCI 与 ICR·跳板·context 的 OS 依赖；手册入口。
- 2026-08-29：整篇重做——双身份模型；NR_CPU/online 二分；INIT-SIPI/PSCI；AP 重跑表。
- 2026-08-27：初稿。
