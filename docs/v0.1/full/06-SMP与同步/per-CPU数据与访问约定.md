# per-CPU 数据与访问约定

v0.1 · 2026-08-29

本篇覆盖：`kernel/smp/percpu.c`、`include/rendezvos/smp/percpu.h`、`arch/{x86_64,aarch64}/percpu.c`（`arch_enable_percpu`）。

SMP 身份 / BSP_ID 时序见拓扑篇；各变量「为何 per-CPU」见子系统篇（TM、kmalloc、EBR、IRQ…）。

---

## 1. 概述

链接段 **`.percpu..data`** 放一份 **CPU0 模板**；映像后为 CPU1…MAX-1 **预留连续物理区并 memset 0**。

不是「每 CPU 从模板 memcpy」。AP 槽是**清零预留**——`DEFINE_PER_CPU(...)= { init }` **只对 CPU0 生效**；AP 的 GDT/TSS 等必须在 `arch_start_core` 重建。

| 宏 | 含义 |
|----|------|
| `DEFINE_PER_CPU(type, name)` | 放入 `.percpu..data` |
| `per_cpu(var, cpu)` | `__per_cpu_offset[cpu] + (&var - &_per_cpu_start)` |
| `percpu(var)` | `get_per_cpu_base() + 同上`（本核） |
| `arch_enable_percpu(cpu)` | x86：`MSR_GS_BASE`；aarch64：`TPIDR_EL1` |

---

## 2. 目标与边界

**提供：** 布局、宏、GS/TPIDR、跨核读纪律、MCS `me` 规则。

**不做：** Linux `this_cpu_ptr` 动态分配 API；动态对象（如 `core_tm`）仍是运行时 kmalloc 再 `per_cpu(ptr,cpu)=…`。

---

## 3. 分层与调用方

典型住户：`core_tm`、`kallocator`、`current_vspace`、`Map_Handler`、`cpu_number`、`boot_stack_bottom`、`irq_vector[]`、`ebr_*`、`smp_ipi_pending`、`CPU_STATE`、各类 `*_spin_lock` / `*_mcs_node`、时间 tick、x86 `cpu_tss`/GDT、x86 `smp_tlb_flush_message`（aarch64 **无**此槽）。

跨核读他核 per-CPU：仅约定路径（TLB msg、EBR scan、调试）；并发改须锁或 IPI。

---

## 4. 数据结构与不变量

### 4.1 offset 布局

`reserve_per_cpu_region`：`offset[0]=_per_cpu_start`；`offset[1]=virt(phy_end)`；再线性填 2…MAX-1。注释：**只用 MAX-1 份额外区**（index 0 在链接脚本里）。随后 `clean_per_cpu_region` 清零额外区。

### 4.2 MCS `me`

`lock_mcs(lock, me)` 的 `me` 必须是 **本 CPU 队列节点**（`&percpu(port_table_spin_lock)`、`&percpu(asid_mcs_node)`，或嵌在 `Map_Handler` 里的 node）。用他核 slot → 链表损坏（INVARIANTS）。

### 4.3 启用时机

`percpu()` 早于 `arch_enable_percpu` → 基址错。AP：secondary 入口尽早 enable。BSP：见拓扑篇 BSP_ID/GS 时序注意。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `percpu.h` / `percpu.c` | 宏、offset、reserve/clean |
| `arch/*/percpu.c` | GS / TPIDR |

---

## 6. 流程

1. 链接：CPU0 模板在段内。  
2. `phy_mm_init`：`reserve_per_cpu_region` + clean。  
3. BSP/AP：`arch_enable_percpu` → 之后才安全 `percpu()`。  
4. 运行时：各子系统往本核槽填指针/状态。

---

## 7. 公开 API

```c
DEFINE_PER_CPU(type, name);
percpu(var);
per_cpu(var, cpu);
void arch_enable_percpu(cpu_id_t cpu);
/* reserve/clean：mm/boot 内部 */
```

---

## 8. 多架构

基址寄存器不同；布局与宏相同。勿假定 aarch64 有 TLB flush message 槽。

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

- 2026-08-29：整篇重做——纠正「复制」→清零预留；MCS me；GS/TPIDR；住户表；与拓扑篇时序交叉。
- 2026-08-27：初稿。
