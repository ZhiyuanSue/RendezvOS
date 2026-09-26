# per-CPU 数据与访问约定

v0.1 · 2026-09-25

本篇覆盖：`kernel/smp/percpu.c`、`include/rendezvos/smp/percpu.h`、`arch/{x86_64,aarch64}/percpu.c`（`arch_enable_percpu` / `get_per_cpu_base`）。

SMP 身份 / BSP_ID 时序见拓扑篇；各变量「为何 per-CPU」见子系统篇（TM、kmalloc、EBR、IRQ…）。MCS `me` 事故亦见 `doc/ai/INVARIANTS.md`。

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

每核要有一份自己的 `core_tm` / `irq_vector` / MCS node，又不能在热路径上查「我是几号核再乘偏移」的全局表（还要抗重入）。惯例是：

1. 算出本核 per-CPU 区的**虚拟基址**；
2. 写进 **只有本核能读的系统寄存器**；
3. `percpu(var)` 用「符号相对段起点的偏移 + 基址」得到本核副本。

x86 选 **GS_BASE**（内核态直接当基址；用户 TLS 另走 FS / KERNEL_GS，见任务篇 `switch_to`）。aarch64 选 **TPIDR_EL1**（EL1 私有；**不要**和用户 TLS 的 `TPIDR_EL0` 混）。

未 `arch_enable_percpu` 就调 `percpu()` → 读到错误基址 → 静默踩错槽或崩。

---

## 2. 目标与边界

**提供：** 布局、宏、GS / TPIDR、跨核读纪律、MCS `me` 规则。

**不做：** Linux `this_cpu_ptr` 动态分配 API；动态对象（如 `core_tm`）仍是运行时 kmalloc 再 `per_cpu(ptr,cpu)=…`。

---

## 3. 分层与调用方

典型住户：`core_tm`、`kallocator`、`current_vspace`、`Map_Handler`、`cpu_number`、`boot_stack_bottom`、`irq_vector[]`、`ebr_*`、`smp_ipi_pending`、`CPU_STATE`、各类 `*_spin_lock` / `*_mcs_node`、时间 tick、x86 `cpu_tss` / GDT、x86 `smp_tlb_flush_message`（aarch64 **无**此槽）、x86 `user_rsp_scratch`。

跨核读他核 per-CPU：仅约定路径（TLB msg、EBR scan、调试）；并发改须锁或 IPI。

---

## 4. 数据结构与不变量

### 4.1 offset 布局

`reserve_per_cpu_region`（在 `phy_mm_init` 路径）：

- `offset[0] = &_per_cpu_start`（链接段内 CPU0 模板）；
- 把 `phy_kernel_end` 64B 对齐后，`offset[1] = virt(phy_end)`；
- 再为 CPU1…MAX-1 预留 `(MAX-1) * per_cpu_size` 物理空间；
- `calculate_per_cpu_offset` 线性填 `offset[2…]`。

注释写明：**只用 MAX-1 份额外区**（index 0 在链接脚本里）。随后 `clean_per_cpu_region` **memset 0** 额外区——不是 memcpy 模板。

### 4.2 MCS `me`

`lock_mcs(lock, me)` 的 `me` 必须是 **本 CPU 队列节点**（`&percpu(port_table_spin_lock)`、`&percpu(asid_mcs_node)`，或嵌在 `Map_Handler` 里的 node）。用他核 slot → 链表损坏（INVARIANTS）。

### 4.3 启用时机

| 核 | 何时 `arch_enable_percpu` |
|----|---------------------------|
| BSP | 启动早期（常在知道 `BSP_ID` 前后；拓扑篇警告 APIC≠0 时 GS 窗） |
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

```c
DEFINE_PER_CPU(type, name);
percpu(var);
per_cpu(var, cpu);
void arch_enable_percpu(cpu_id_t cpu);
vaddr get_per_cpu_base(void);
/* reserve/clean：mm/boot 内部 */
```

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

间接覆盖。本篇未复测。

---

## 10. 限制与后续

- AP 零页 vs CPU0 初值。
- 无动态 percpu allocator。
- BSP_ID≠0 与 GS 窗（拓扑篇）。
- 稀疏拓扑下勿用 `NR_CPU` 当合法下标上界扫全表。

---

## 11. 变更记录

- 2026-09-25：语言整理；§1.1 补 GS_BASE / TPIDR_EL1 硬件动机与用户 TLS 分界。
- 2026-08-29：整篇重做——纠正「复制」→清零预留；MCS me；GS/TPIDR；住户表；与拓扑篇时序交叉。
- 2026-08-27：初稿。
