# IRQ 向量分配与处理

v0.1 · 2026-09-25

本篇覆盖：`kernel/trap/trap.c`（reserve / pool / alloc / `register_irq_handler` / `trap_handler`）、`include/rendezvos/trap/trap.h`、`arch_init_irq_vector_state`（各 arch `trap.c`）、timer / IPI 注册衔接（点到为止）。

Trap class / fixed / IDT·VBAR 入口见 `23-Trap抽象与分类.md`；APIC / PIC、GIC 寄存器与 EOI 细节见平台两篇；timer 语义见 `07-时间与定时器`；软 IPI 协议见 `06-SMP与同步/30-软IPI机制.md`。

官方手册（本篇只钉「软件命名空间如何贴上硬件号」；寄存器细节归平台篇）：

- **Intel SDM Vol.3：** IDT、向量 0–255、Local APIC 投递的向量号。
- **ARM GIC Architecture Specification（GICv2，IHI0048）与 ARM ARM：** INTID、GICC_IAR / EOIR；CPU 侧只有 VBAR IRQ 入口。

---

## 1. 概述

硬件中断不会自己变成 C 函数调用。CPU 侧永远是：

1. 控制器（8259 / LAPIC / GIC）挑出一个**号**；
2. 架构入口把这个号写进 `trap_frame`；
3. `trap_handler` 用这个号当下标，查 per-CPU `irq_vector[]`，调 handler；
4. 若属性要求，再做 **EOI**（告诉硬件「这条中断处理完了」）。

core 的贡献是第 3 步的**软件命名空间**：先 **reserve** 架构占用，再划 **alloc 池** 给驱动；`register_irq_handler` 要求槽已 USED，并在 **所有 CPU** 装同一 handler。这不是 Linux `request_irq`，也不是「随便拿个数字写 handler」。

### 1.1 硬件「号」与软件 trap id

| | x86_64 | aarch64 |
|--|--------|---------|
| 硬件交付物 | **IDT 向量**（0–255） | **GIC INTID**（0–1019…），CPU 只看见 VBAR 的 IRQ 槽 |
| 软件 trap id | **= 向量号** | **`intid + 64`**（前 64 槽留给 ESR.EC） |
| 谁写出这个号 | PIC 基址 / LAPIC LVT·SVR·ICR /（将来）IOAPIC RTE | `GICC_IAR` 读出 INTID；SGI 由 `GICD_SGIR` 注入 |

OS 必须保证：**写进硬件的向量 / INTID，在软件表里已经 reserve 且挂了 handler**。否则要么 alloc 撞车，要么进 `unknown` panic。

### 1.2 为何 per-CPU 表、却装同一 handler

`DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ])`：每核一份槽位状态（USED 等），但 `register_irq_handler` 写的是**所有 online CPU 同一函数指针**。

硬件原因：SMP 上同一外设可能被路由到不同核（或 IPI 打到指定核）；每核都有自己的 IDT / VBAR / GICC，入口汇编跑在**被中断的那颗核**上，查的是**本核** `percpu(irq_vector)`。若只装 BSP 的表，AP 上同一向量会 panic。

v0.1 **没有**「每核不同 ISR」；绑核靠硬件路由（GIC ITARGETSR / 将来 IOAPIC），不是靠换软件表项。

### 1.3 EOI 与 schedule

ACK：`IRQ_NEED_EOI` → `arch_eoi_irq`（x86 写 LAPIC EOI 或 PIC OCW2；aarch64 写 `GICC_EOIR`）。漏 EOI 时，同优先级 / level 触发的线可能**再也不来**。

用户态被打断且 `core_tm` 就绪 → `trap_handler` 末尾 **`schedule`**（x86 `syscall` 旁路除外——见 syscall 篇）。ISR 应短、非阻塞；长活 IPC 到线程（无 threaded IRQ）。

---

## 2. 目标与边界

**提供：** SMP 一致的 trap id 命名空间；reserve / pool / alloc / free；register；EOI attr；用户来源 schedule 钩子。

**不做：** MSI / `request_irq` 框架；IOAPIC / GIC 路由编程全文（平台篇）；把 EC / fault 当「设备 IRQ」讲（Trap 篇）。

---

## 3. 分层与调用方

| 角色 | 做什么 |
|------|--------|
| arch boot | `arch_init_irq_vector_state`：reserve + `set_alloc_pool` |
| Timer | `rendezvos_time_init`：`register_irq_handler(timer_irq_num, …, IRQ_NEED_EOI)`（= `ARCH_IRQ_VEC_TIMER`） |
| IPI | `arch_smp_ipi_init`：register `ARCH_IRQ_VEC_IPI` |
| Fault / SVC | `register_fixed_trap` → 内部 `register_irq_handler` |
| 设备 | **`irq_vector_alloc` → `register_irq_handler(..., IRQ_NEED_EOI)` → 平台 unmask / 路由** |

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

### 4.2 NR_IRQ 与命名空间（硬件贴合）

| | x86_64 | aarch64 |
|--|--------|---------|
| `NR_IRQ` | **256**（IDT 全宽；SDM：向量 0–255） | **1084** = 64 EC + GIC INTID 0–1019 |
| 身份 | 向量号 = trap id | `[0,64)` = EC；`[64,…)` = intid+64 |
| IRQ↔id | 同一数字 | `trap_id = intid + 64`；`intid = trap_id - 64` |

x86 上 Intel 保留 **0–31** 给架构异常（`TRAP_ARCH_USED = 32` 是上界，不是「已用个数」）。设备 / timer / IPI 必须落在 ≥32 的向量上，并避开我们 reserve 的固定槽。

aarch64 上 CPU **没有** 256 项向量表；GIC 用 INTID 标识中断源。为了复用同一套 `irq_vector[]` 分发，把 EC 塞进前 64 槽、IRQ 整体平移 +64——Trap 篇说的「假统一、真映射」。

### 4.3 reserve 实表（`arch_init_irq_vector_state`，本 CPU `me`）

**x86（贴合 IDT / LAPIC 固定编程）：**

| 区间 / 槽 | 用途 | 硬件谁写这个向量 |
|-----------|------|------------------|
| `[0, 31]` | 架构异常（`TRAP_ARCH_USED`） | IDT gate；不是 LAPIC LVT |
| `0x20` | Timer | PIC 路径 = master IRQ0 基址；APIC = **LVT Timer** 向量字段 |
| `0x27` | Spurious | **SVR** 的 spurious 字段；**无专用 handler** → 误入可 panic |
| `0x30` | IPI | **ICR** FIXED 投递 |
| pool `[0x40, 0xEF]` | 设备 alloc | 将来 IOAPIC / MSI 编程到这些向量 |

中间洞（如 0x21–0x26）**不在 pool、也未 reserve**——不可 alloc，也勿手写进 LVT。

**aarch64（贴合 GIC INTID 分段）：**

| 区间 | 用途 | 硬件 |
|------|------|------|
| `[0, 63]` | EC / sync | ESR.EC，不是 GIC |
| trap **64–95** | SGI+PPI（intid 0–31；含 IPI0、timer PPI30） | **整段** reserve——SGI/PPI 的 INTID 固定，不能当设备池 |
| pool trap **96–1083** | SPI intid 32–1019 | Distributor 可配的外设线 |

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
| 平台 APIC / GIC | 硬件路由与 EOI 实现 |

---

## 6. 流程

### 6.1 Boot（软件表必须先于「能收到中断」）

```text
init_interrupt
  → arch_init_irq_vector_state   // reserve + set_alloc_pool
  → arch_init_interrupt          // lidt / VBAR(+aarch64 立刻 enable IRQ)
→ 平台 IRQ 接口（x86 init_irq / aarch64 gic.init_cpu_interface）
→ smp_ipi_init                   // register IPI
→ … → rendezvos_time_init        // register timer；x86 路径上才 software_enable_APIC
```

x86：先 lidt、再选 PIC/APIC、再 `sti`（且常在 `init_syscall` 之后）。aarch64：**先开 IRQ 再配 GICC**——窗口可疑，见 GIC 篇。

### 6.2 `trap_handler`（设备 / IRQ 路径）

```text
trap_id = TRAP_ID(tf->trap_info)
→ handler 或 unknown+panic
→ NEED_EOI → arch_eoi_irq      // 硬件 ACK
→ !from_kernel && core_tm → schedule
```

### 6.3 驱动 checklist（硬件依赖顺序）

1. `u32 id; irq_vector_alloc(&id);` — **勿**手写 timer / IPI / EC。
2. `register_irq_handler(id, isr, IRQ_NEED_EOI);` — 软件表就绪。
3. **再**硬件路由 / unmask：
   - x86：向量 = `id`（IOAPIC / MSI 见 APIC 篇；v0.1 IOAPIC 空壳 → 外设线基本接不上）。
   - aarch64：`intid = AARCH64_TRAP_ID_TO_IRQ(id)`，再 `gicd_v2_unmask_irq` / `set_affinity`。
4. 未 USED 就 register → error log + return。
5. ISR 返回后由 `trap_handler` 做 EOI——handler 内**不要**自己乱写 EOI，除非明确绕过 `IRQ_NEED_EOI`。

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

上表 NR_IRQ / reserve / intid 映射。控制器细节 → APIC / GIC 篇。riscv / loongarch 非 v0.1 主线。

---

## 9. 测试

间接：timer、IPI、SMP。无单独「池耗尽」测例。本篇未复测。

---

## 10. 限制与后续

- 池耗尽 → `-E_RENDEZVOS`；未 set pool → `-E_IN_PARAM`。
- free 不清 handler。
- x86 IOAPIC 路由未齐 → alloc 出的向量无处可编程。
- Spurious 无专用 handler。
- 池是全局 lo/hi，非 per-CPU 独立池。
- 无 portable `irq_set_affinity`（old 笔记 backlog）；GIC 有内部 `set_affinity`，x86 无。

---

## 11. 变更记录

- 2026-09-25：扩写硬件「号」与软件 trap id 贴合；per-CPU 表为何、EOI 硬件含义；驱动 checklist 强调先软件后路由；手册入口。
- 2026-08-29：整篇重做——两侧 reserve 实表；NR_IRQ / EC+offset；free / USED 陷阱。
- 2026-08-27：初稿。
