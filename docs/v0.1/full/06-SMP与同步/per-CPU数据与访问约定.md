# per-CPU 数据与访问约定

v0.1 · 2026-08-27

本篇覆盖：`kernel/smp/percpu.c`、`include/rendezvos/smp/percpu.h`、`arch/x86_64/percpu.c`、`arch/aarch64/percpu.c`。

SMP 启动见 `SMP启动与处理器拓扑.md`；`core_tm`、`kallocator` 等 per-CPU 变量见各子系统篇。

---

## 1. 概述

RendezvOS 使用链接段 **`.percpu..data`** 存放 per-CPU 变量：`DEFINE_PER_CPU(type, name)` 在 BSP 镜像中占 **一份模板**，boot 时 **`reserve_per_cpu_region`** 在物理内存为每 CPU 复制/分配区域，**`calculate_per_cpu_offset`** 填 **`__per_cpu_offset[cpu]`**。

访问宏：**`percpu(var)`**（当前 CPU）、**`per_cpu(var, cpu)`**（指定 CPU，须确保合法）。**`arch_enable_percpu(cpu)`** 在 AP 启动早期切换 **`get_per_cpu_base()`** 使用的 offset。

---

## 2. 目标与边界

core 不提供 Linux 式 **this_cpu_ptr** 动态 per-CPU 分配 API；动态 per-CPU 结构（如 `core_tm`）在运行时 kmalloc 并 **`per_cpu(ptr, cpu) = ...`** 赋值。

**纪律：** MCS 锁的 **`me`** 参数、 **`port_table_spin_lock` 的 me 槽** 必须是 **本 CPU** 的 per-CPU slot，禁止用他核指针（INVARIANTS）。

---

## 3. 分层与调用方

典型 per-CPU 变量：`core_tm`、`kallocator`、`current_vspace`、`cpu_number`、`boot_stack_bottom`、`irq_vector[]`、`ebr_*`、`smp_ipi_pending`。

跨核读他核 per-CPU：仅允许在 **已知 CPU 在线且数据稳定** 时（如 TLB shootdown、EBR scan）；并发修改须另用锁或 IPI 同步。

---

## 4. 数据结构与不变量

```c
#define percpu(var)  /* get_per_cpu_base() + offset */
#define per_cpu(var, cpu)  /* __per_cpu_offset[cpu] + offset */
extern u64 __per_cpu_offset[RENDEZVOS_MAX_CPU_NUMBER];
extern cpu_id_t cpu_number;
```

- 链接脚本须保留 `_per_cpu_start` / `_per_cpu_end` 符号。
- AP 的 per-CPU 区在 **`arch_enable_percpu`** 之后方可 `percpu()` 访问模板变量。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `percpu.c` | offset 计算、region reserve/clean |
| `arch/*/percpu.c` | `get_per_cpu_base`、GS/TPIDR 等 |

---

## 6. 流程

BSP boot：reserve → copy template → calculate offset → 设 BSP offset。  
AP：`arch_enable_percpu(id)` → 使用该 CPU 的 per-CPU 窗口。

---

## 7. 公开 API

`DEFINE_PER_CPU`、`percpu`、`per_cpu`、`arch_enable_percpu`、`calculate_per_cpu_offset`、`reserve_per_cpu_region`。

---

## 8. 多架构

x86 可能用 GS base 指向 per-CPU 区；aarch64 用 TPIDR_EL1 或 offset 表（见 arch percpu.c）。

---

## 9. 测试

SMP 测例间接验证每核独立 `core_tm`、`cpu_number`。

与当前源码一致，尚未复测。

---

## 10. 限制与后续

- **RENDEZVOS_MAX_CPU_NUMBER** 编译上限
- 动态 hotplug CPU 无 per-CPU 区扩展 API

---

## 11. 变更记录

| 2026-08-27 | 初稿 |
