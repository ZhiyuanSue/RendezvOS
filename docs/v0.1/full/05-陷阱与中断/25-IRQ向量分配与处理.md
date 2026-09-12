# IRQ 向量分配与处理

v0.1 · 2026-08-29

本篇覆盖：`kernel/trap/trap.c`（reserve / pool / alloc / `register_irq_handler` / `trap_handler`）、`include/rendezvos/trap/trap.h`、`arch_init_irq_vector_state`（各 arch `trap.c`）、timer/IPI 注册衔接（点到为止）。

Trap class / fixed 见 `23-Trap抽象与分类.md`；APIC/PIC、GIC 寄存器与 EOI 细节见平台两篇；timer 语义见 `07-时间与定时器`；软 IPI 协议见 `06-SMP与同步/30-软IPI机制.md`。

---

## 1. 概述

硬件中断号（x86 向量 / GIC intid）与软件 handler 要在 **SMP 上同名**。core 用 per-CPU `irq_vector[NR_IRQ]`：先 **reserve** 架构占用，再划 **alloc 池** 给驱动；`register_irq_handler` 要求槽已 USED，并在 **所有 CPU** 装同一 handler。

这不是 Linux `request_irq`，也不是「随便拿个数字写 handler」。

ACK：`IRQ_NEED_EOI` → `arch_eoi_irq`。用户态被打断且 `core_tm` 就绪 → `trap_handler` 末尾 **`schedule`**（x86 `syscall` 旁路除外——见 syscall 篇）。

ISR 应短、非阻塞；长活 IPC 到线程（无 threaded IRQ）。

---

## 2. 目标与边界

**提供：** SMP 一致的 trap id 命名空间；reserve / pool / alloc / free；register；EOI attr；用户来源 schedule 钩子。

**不做：** MSI / `request_irq` 框架；IOAPIC/GIC 路由编程全文（平台篇）；把 EC/fault 当「设备 IRQ」讲（Trap 篇）。

---

## 3. 分层与调用方

| 角色 | 做什么 |
|------|--------|
| arch boot | `arch_init_irq_vector_state`：reserve + `set_alloc_pool` |
| Timer | `rendezvos_time_init`：`register_irq_handler(timer_irq_num, …, IRQ_NEED_EOI)`（= `ARCH_IRQ_VEC_TIMER`） |
| IPI | `arch_smp_ipi_init`：register `ARCH_IRQ_VEC_IPI` |
| Fault/SVC | `register_fixed_trap` → 内部 `register_irq_handler` |
| 设备 | **`irq_vector_alloc` → `register_irq_handler(..., IRQ_NEED_EOI)` → 平台 unmask/路由** |

---

## 4. 数据结构与不变量

### 4.1 表项

```c
struct irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;  /* bit: NEED_EOI, VEC_USED */
};
DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ]);
```

全局池：`irq_vector_pool_lo/hi`；默认 **lo=1, hi=0**（非法 → 未 `set_alloc_pool` 时 alloc 得 `-E_IN_PARAM`）。

### 4.2 NR_IRQ 与命名空间

| | x86_64 | aarch64 |
|--|--------|---------|
| `NR_IRQ` | **256**（IDT 全宽） | **1084**（64 EC + GIC 0–1019） |
| 身份 | 向量号 = trap id | `[0,64)` = EC；`[64,…)` = intid+64 |
| IRQ↔id | 同一数字 | `trap_id = intid + 64`；`intid = trap_id - 64` |

### 4.3 reserve 实表（`arch_init_irq_vector_state`，本 CPU `me`）

**x86：**

| 区间/槽 | 用途 |
|---------|------|
| `[0, 31]` | 架构异常（`TRAP_ARCH_USED`） |
| `0x20` | Timer |
| `0x27` | Spurious（只 reserve，**未见专用 handler**——误入可 panic） |
| `0x30` | IPI |
| pool `[0x40, 0xEF]` | 设备 alloc |

中间洞（如 0x21–0x26）**不在 pool、也未 reserve**——不可 alloc。

**aarch64：**

| 区间 | 用途 |
|------|------|
| `[0, 63]` | EC / sync |
| trap **64–95** | SGI+PPI（intid 0–31；含 IPI0、timer PPI30）——**整段** reserve，不是单独两行 |
| pool trap **96–1083** | SPI intid 32–1019 |

### 4.4 注册不变量

- `register_irq_handler`：要求 **当前 CPU** 该 id 已 USED；写 **全 CPU** 同一 handler；`attr | USED`。  
- 允许 `handler == NULL`（注释）清函数指针，USED 可仍在。  
- `irq_vector_free`：仅池内；清 USED，**不**清 handler 指针——再 alloc 同 id 可能看到旧指针直到重新 register。  
- `register_irq_handler` **不是** per-CPU 不同 ISR。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/trap/trap.c` | 池 API、`trap_handler` |
| `arch/*/trap/trap.c` | `arch_init_irq_vector_state`、`arch_eoi_irq` |
| `kernel/time/time.c` | timer register |
| `arch/*/smp` | IPI register |
| 平台 APIC/GIC | 硬件路由与 EOI 实现 |

---

## 6. 流程

### 6.1 Boot

```text
init_interrupt
  → arch_init_irq_vector_state   // reserve + set_alloc_pool
  → arch_init_interrupt          // lidt / VBAR(+aarch64 enable)
→ 平台 IRQ 接口（x86 init_irq / aarch64 gic.init_cpu_interface）
→ smp_ipi_init                   // register IPI
→ … → rendezvos_time_init        // register timer
```

### 6.2 `trap_handler`（设备/IRQ 路径）

```text
trap_id = TRAP_ID(tf->trap_info)
→ handler 或 unknown+panic
→ NEED_EOI → arch_eoi_irq
→ !from_kernel && core_tm → schedule
```

### 6.3 驱动 checklist

1. `u32 id; irq_vector_alloc(&id);` — **勿**手写 timer/IPI/EC。  
2. `register_irq_handler(id, isr, IRQ_NEED_EOI);`  
3. 硬件路由：  
   - x86：向量 = `id`（IOAPIC/MSI 见 APIC 篇；v0.1 可能空壳）。  
   - aarch64：`intid = AARCH64_TRAP_ID_TO_IRQ(id)`，再 GIC unmask/target。  
4. 未 USED 就 register → error log + return。

---

## 7. 公开 API

```c
error_t irq_vector_reserve_range_for_cpu(cpu_id_t cpu, u32 lo, u32 hi);
error_t irq_vector_reserve_range_for_all_cpus(u32 lo, u32 hi);
void irq_vector_set_alloc_pool(u32 lo, u32 hi);
error_t irq_vector_alloc(u32 *trap_id_out);
error_t irq_vector_free(u32 trap_id);
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *),
                          u64 irq_attr);
void trap_handler(struct trap_frame *tf);
void arch_eoi_irq(u64 trap_info);
```

---

## 8. 多架构

上表 NR_IRQ / reserve / intid 映射。控制器细节 → APIC / GIC 篇。

---

## 9. 测试

间接：timer、IPI、SMP。无单独「池耗尽」测例。本篇未复测。

---

## 10. 限制与后续

- 池耗尽 → `-E_RENDEZVOS`；未 set pool → `-E_IN_PARAM`。  
- free 不清 handler。  
- x86 IOAPIC 路由可能未齐。  
- Spurious 无专用 handler。  
- 池是全局 lo/hi，非 per-CPU 独立池。

---

## 11. 变更记录

- 2026-08-29：整篇重做——两侧 reserve 实表；NR_IRQ/EC+offset；驱动 checklist；free/USED 陷阱；与 Trap/APIC/GIC/Timer/IPI 边界；纠正「aarch64 单独 reserve TIMER 行」叙事。
- 2026-08-27：初稿。
