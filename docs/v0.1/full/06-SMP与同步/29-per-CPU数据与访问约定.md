# per-CPU 数据与访问约定

v0.1 · 2026-09-27

本篇覆盖：`kernel/smp/percpu.c`、`include/rendezvos/smp/percpu.h`、`arch/{x86_64,aarch64}/percpu.c`（`arch_enable_percpu` / `get_per_cpu_base`）。

SMP 身份 / BSP_ID（BSP = Bootstrap Processor，引导核；AP = Application Processor，非引导核）时序见拓扑篇；各变量「为何 per-CPU」见子系统篇（TM、kmalloc、EBR、IRQ…）。MCS（Mellor-Crummey & Scott 队列锁）的 `me` 参数事故亦见 `doc/ai/INVARIANTS.md`。

**官方手册：** Intel SDM — `IA32_GS_BASE`（及与 SWAPGS / `KERNEL_GS_BASE` 的分工）；ARM ARM — `TPIDR_EL1`（EL1 软件线程 ID，本仓库用作 per-CPU 基址）。

---

## 1. 概述

链接段 **`.percpu..data`** 放一份 **CPU0 模板**；映像后为 CPU1…MAX-1 **预留连续物理区并 memset 0**。

不是「每 CPU 从模板 memcpy」。AP 槽是**清零预留**——`DEFINE_PER_CPU(...)= { init }` **只对 CPU0 生效**；AP 的 GDT / TSS 等必须在 `arch_start_core` 重建。

| 宏 | 含义 |
|----|------|
| `DEFINE_PER_CPU(type, name)` | 放入 `.percpu..data` |
| `per_cpu(var, cpu)` | `__per_cpu_offset[cpu] + (&var - &_per_cpu_start)` |
| `percpu(var)` | `get_per_cpu_base() + 同上`（本核） |
| `arch_enable_percpu(cpu)` | x86：写 **`MSR_GS_BASE`**；aarch64：写 **`TPIDR_EL1`** |

### 1.1 硬件为何需要「基址寄存器」

每核要有一份自己的 `core_tm` / `irq_vector` / MCS node，又不能在热路径上「查询本核编号再乘以偏移量」走全局表（还要抗重入）。惯例做法是：

1. 算出本核 per-CPU 区的**虚拟基址**；
2. 写进 **只有本核能读的系统寄存器**；
3. `percpu(var)` 用「符号相对段起点的偏移 + 基址」得到本核副本。

x86 选 **GS_BASE**（内核态直接当基址；用户 TLS 另走 FS / KERNEL_GS，见任务篇 `switch_to`）。aarch64 选 **TPIDR_EL1**（EL1 私有；**不要**和用户 TLS 的 `TPIDR_EL0` 混）。

未 `arch_enable_percpu` 就调 `percpu()` → 读到错误基址 → 静默误访问错误槽位或崩溃。

#### 1.1.1 x86 GS 段机制要点

x86-64 的 per-CPU 基址靠 **`MSR_GS_BASE`**（`0xC0000101`）提供——它保存的是 **`%gs` 段寄存器**所选中 segment descriptor 的 **base 字段**，但 64 位下系统软件约定「`%gs:` 前缀访问 = `GS_BASE + disp`」，descriptor 的 limit / 权限在 64 位基本被忽略，等价于一条「带基址的线性地址」寻址。配套寄存器：

- **`MSR_GS_BASE`**：当前生效基址（内核态读 `%gs:` 走它）。
- **`MSR_KERNEL_GS_BASE`**（`0xC0000102`）：保存用户态的 GS 基址；`SWAPGS`（特权指令）在 syscall entry / exit 时与 `GS_BASE` 互换，让内核态用 per-CPU 基址、用户态用 TLS 基址，无需手动 wrmsr。
- **`MSR_FS_BASE`**（`0xC0000100`）：FS 段基址，本仓库用作用户 TLS，与 per-CPU **无关**。

`SWAPGS` 只在 ring 转换处用；`arch_enable_percpu` 在 AP 启动时直接 `wrmsrq(MSR_GS_BASE, offset)`，不动 `KERNEL_GS_BASE`。

#### 1.1.2 aarch64 线程 ID 寄存器

ARM 上没有「段基址」概念，per-CPU 借用 **线程 ID 寄存器**——它们只是 64 位通用系统寄存器，无权限 / limit 检查，软件约定存什么由 OS 决定：

- **`TPIDR_EL1`**：EL1（内核）私有；本仓库存放 per-CPU 基址。EL2/EL3 各有对应 `TPIDR_EL2/EL3`。
- **`TPIDR_EL0`**：EL0（用户）可读写，用作用户 TLS；`switch_to` 切线程时换。
- **`TPIDRRO_EL0`**：EL0 只读视图，Linux 用作当前 CPU 编号；本仓库未用。

EL1 代码读 `mrs x0, TPIDR_EL1` 取得基址，再加偏移即得本核副本地址。

---

## 2. 目标与边界

**提供：** 布局、宏、GS / TPIDR、跨核读纪律、MCS `me` 规则。

**不做：** Linux `this_cpu_ptr` 动态分配 API；动态对象（如 `core_tm`）仍是运行时 kmalloc 再 `per_cpu(ptr,cpu)=…`。

---

## 3. 分层与调用方

典型住户：`core_tm`、`kallocator`、`current_vspace`、`Map_Handler`、`cpu_number`、`boot_stack_bottom`、`irq_vector[]`、`ebr_*`、`smp_ipi_pending`、`CPU_STATE`、各类 `*_spin_lock` / `*_mcs_node`、时间 tick、x86 `cpu_tss` / GDT、x86 `smp_tlb_flush_message`（aarch64 **无**此槽）、x86 `user_rsp_scratch`。

跨核读取其他核的 per-CPU：仅约定路径（TLB msg、EBR scan、调试）；并发修改须持锁或发 IPI。

---

## 4. 数据结构与不变量

### 4.1 offset 布局

`reserve_per_cpu_region`（在 `phy_mm_init` 路径）：

- `offset[0] = &_per_cpu_start`（链接段内 CPU0 模板）；
- 把 `phy_kernel_end` 64B 对齐后，`offset[1] = virt(phy_end)`；
- 再为 CPU1…MAX-1 预留 `(MAX-1) * per_cpu_size` 物理空间；
- `calculate_per_cpu_offset` 线性填 `offset[2…]`。

注释写明：**只用 MAX-1 份额外区**（index 0 在链接脚本里）。随后 `clean_per_cpu_region` **memset 0** 额外区——不是 memcpy 模板。

#### 4.1.1 布局示意

```text
                  .percpu..data (链接段，CPU0 模板)
                  ┌──────────────────────────────────────┐
   &_per_cpu_start │ DEFINE_PER_CPU(core_tm) ...          │ ◀ offset[0]
                  │ DEFINE_PER_CPU(irq_vector) ...       │
                  │ DEFINE_PER_CPU(smp_ipi_pending) ...  │
                  │ ...（所有 per-CPU 变量一份模板）       │
   &_per_cpu_end  └──────────────────────────────────────┘
                            │ 64B 对齐
                            ▼
                  额外预留区（AP 槽，memset 0）
                  ┌──────────────────────────────────────┐
   offset[1]  ──▶ │ CPU1 副本（per_cpu_size 字节）        │
                  ├──────────────────────────────────────┤
   offset[2]  ──▶ │ CPU2 副本                             │
                  ├──────────────────────────────────────┤
                  │ ...                                   │
                  ├──────────────────────────────────────┤
   offset[N-1]──▶ │ CPU(N-1) 副本                         │
                  └──────────────────────────────────────┘
                            ▲
                            │ phy_kernel_end 推进到这里
```

同一变量 `var` 在不同核的副本地址：`__per_cpu_offset[cpu] + (&var - &_per_cpu_start)`。`percpu(var)` 只是把 `__per_cpu_offset[cpu]` 换成 `get_per_cpu_base()`（即从 GS_BASE / TPIDR_EL1 读），省一次显式 `cpu` 查询。

### 4.2 MCS `me`

`lock_mcs(lock, me)` 的 `me` 必须是 **本 CPU 队列节点**（`&percpu(port_table_spin_lock)`、`&percpu(asid_mcs_node)`，或嵌在 `Map_Handler` 里的 node）。使用其他核的 slot → 链表损坏（INVARIANTS）。

### 4.3 启用时机

| 核 | 何时 `arch_enable_percpu` |
|----|---------------------------|
| BSP | `cmain`：`arch_cpu_info` 之后、`virt_mm_init` 之前（`arch_enable_percpu(BSP_ID)`） |
| AP | `start_secondary_cpu` **尽早**（在 `virt_mm_init` / `arch_start_core` 之前） |

`percpu()` 早于 enable → 基址错。`get_per_cpu_base()`：x86 `rdmsr GS_BASE`；aarch64 `mrs TPIDR_EL1`。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `percpu.h` / `kernel/smp/percpu.c` | 宏、offset、reserve / clean、`cpu_number` |
| `arch/x86_64/percpu.c` | `wrmsrq(MSR_GS_BASE, …)` / `rdmsrq` |
| `arch/aarch64/percpu.c` | `msr/mrs TPIDR_EL1` |

---

## 6. 流程

1. 链接：CPU0 模板在 `.percpu..data`。
2. `phy_mm_init`：`reserve_per_cpu_region` + `clean_per_cpu_region`。
3. BSP / AP：`arch_enable_percpu(cpu_id)` → 之后才安全 `percpu()`。
4. 运行时：各子系统往本核槽填指针 / 状态；跨核用 `per_cpu(var, cpu)` **显式**指名。

---

## 7. 公开 API

本篇涉及的接口分布在：per-CPU 布局与访问——`DEFINE_PER_CPU` / `percpu` / `per_cpu`、`arch_enable_percpu` / `get_per_cpu_base`、`__per_cpu_offset` / `cpu_number`、以及 boot 侧 `reserve_per_cpu_region` / `clean_per_cpu_region` / `calculate_per_cpu_offset`。说明改写自 `percpu.h` Doxygen（已与 `percpu.c`、`arch/*/percpu.c` 核对）。

**本篇不涉及：** 各住户变量语义（`core_tm`、`irq_vector`、MCS node…）→ 各子系统篇；BSP_ID / 上线时序 → `28`；MCS 锁原语本身 → `31`。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 物理布局 | `phy_mm_init` → **`reserve_per_cpu_region`** → map → **`clean_per_cpu_region`**（额外区 **memset 0**） |
| BSP | `cmain`：`arch_cpu_info` 之后 **`arch_enable_percpu(BSP_ID)`** → 之后才 `percpu()`；`arch_start_core` 写 `cpu_number` |
| AP | `start_secondary_cpu` **最先** `arch_enable_percpu(cpu_id)` → 再 `virt_mm_init` / `arch_start_core` |
| MCS | `lock_mcs(lock, me)` 的 `me` = **本核** node（`&percpu(...)`），禁止他核 slot |

未 enable 就 `percpu()` → 基址错。AP 槽无模板初值——勿假定 `DEFINE_PER_CPU(...)= {x}` 在 AP 上成立。

### 7.2 宏与本核访问

```c
DEFINE_PER_CPU(type, name);   /* → .percpu..data（CPU0 模板） */
per_cpu(var, cpu);            /* 显式 CPU；可跨核读 */
percpu(var);                  /* 本核 = get_per_cpu_base() + 偏移 */
```

| 宏 | 说明 |
|----|------|
| `DEFINE_PER_CPU` | 链接段一份模板；初值只服务 CPU0。 |
| `per_cpu` | `__per_cpu_offset[cpu] + (&var - &_per_cpu_start)`。跨核改须有协议。 |
| `percpu` | 依赖 GS_BASE / TPIDR_EL1 已正确初始化。 |

#### 7.2.1 访问路径示意

```text
  C 代码：percpu(core_tm)
        │
        ├─ 编译期：算 &core_tm - &_per_cpu_start  ── 常量偏移 OFF
        │
        └─ 运行期：get_per_cpu_base() + OFF
                          │
                          ▼
        ┌─────────────────────────────────────────────┐
        │ x86_64                                       │
        │   rdmsr MSR_GS_BASE  ──▶ 本核 per-CPU 区基址  │
        │   （SWAPGS 已在 syscall entry 切到内核态）   │
        ├─────────────────────────────────────────────┤
        │ aarch64                                      │
        │   mrs TPIDR_EL1     ──▶ 本核 per-CPU 区基址  │
        │   （EL1 私有，无 ring 切换开销）             │
        └─────────────────────────────────────────────┘
                          │
                          ▼
        基址 + OFF  ──▶ 本核 core_tm 副本
```

热路径上没有「查 cpu_id → 查 offset 表」的全局访问，只有一次 MSR / 系统寄存器读 + 一次加法。这正是 per-CPU 数据存在的核心原因。

### 7.3 基址寄存器

```c
void arch_enable_percpu(cpu_id_t cpu_id);
vaddr get_per_cpu_base(void);
```

| | x86_64 | aarch64 |
|--|--------|---------|
| 写入 | `MSR_GS_BASE = __per_cpu_offset[cpu]` | `TPIDR_EL1 = …` |
| 读取 | `rdmsr GS_BASE` | `mrs TPIDR_EL1` |
| 勿混 | 用户 TLS：FS / `KERNEL_GS` | 用户 TLS：`TPIDR_EL0` |

### 7.4 布局辅助（mm/boot 内部）

```c
void reserve_per_cpu_region(paddr *phy_kernel_end);
void calculate_per_cpu_offset(void);
void clean_per_cpu_region(paddr per_cpu_phy_addr);
```

| 接口 | 说明 |
|------|------|
| `reserve` | `offset[0]`=链接段；`offset[1…]` 从对齐后的 `phy_kernel_end` 起预留 `(MAX-1)*size`；推进 end。 |
| `calculate` | 线性填 `offset[2…]`。 |
| `clean` | 额外区清零——**不是** memcpy 模板。 |

`cpu_number`：`DEFINE_PER_CPU`；每核在 `arch_start_core` 赋值为本 `cpu_id`。

### 7.5 MCS `me`（硬规则）

`me` 必须指向**当前 CPU** 的队列节点。用 `per_cpu(node, other)` 当 `me` → 链表损坏。细节亦见 INVARIANTS。

---


## 8. 多架构

| | x86_64 | aarch64 |
|--|--------|---------|
| 基址寄存器 | `IA32_GS_BASE` | `TPIDR_EL1` |
| 用户 TLS | FS / KERNEL_GS（`switch_to`） | `TPIDR_EL0` |
| TLB flush msg 槽 | 有 | **无** |

布局与宏相同。勿把用户 TLS 寄存器当 per-CPU 基址。

---

## 9. 测试

间接覆盖。

---

## 10. 限制与后续

- AP 零页 vs CPU0 初值。
- 无动态 percpu allocator。
- BSP_ID≠0 与 GS 窗（拓扑篇）。
- 稀疏拓扑下勿用 `NR_CPU` 当合法下标上界遍历全表。

---

## 11. 变更记录

- 2026-10-04：§1.1.1/1.1.2 补 x86 GS 段机制（GS_BASE / KERNEL_GS_BASE / SWAPGS / FS_BASE 分工）与 aarch64 TPIDR_EL1/EL0/RO_EL0 线程 ID 寄存器说明；§4.1.1 加 per-CPU 布局示意（链接段模板 + AP 清零预留区）；§7.3.1 加 percpu() 访问路径图（MSR/系统寄存器读 + 偏移）。
- 2026-10-03：§4.3 / §7.1 BSP enable 时点改为 `arch_cpu_info` 之后。
- 2026-09-27：中文措辞整理——弱化「真源 / 钉死」堆砌。
- 2026-09-26：§7 全文审阅——`percpu.h` 全套 Doxygen；写清「清零预留≠memcpy」「先 enable 再 percpu」与 MCS `me`；划清 vs `28`/`31`。
- 2026-09-25：语言整理；§1.1 补 GS_BASE / TPIDR_EL1 硬件动机与用户 TLS 分界。
- 2026-08-29：整篇重做——纠正「复制」→清零预留；MCS me；GS/TPIDR；住户表；与拓扑篇时序交叉。
- 2026-08-27：初稿。
- 2026-10-05：任务 1/3/5 精读——补全 BSP/AP/MCS 缩写首现释义；「踩错槽/拿基址/装 per-CPU 基址/他核 slot/已装好/扫全表」等口语改正式动词；修正 §7.3.1 编号错位为 §7.2.1。
- 2026-10-05：最终词句顺畅。
