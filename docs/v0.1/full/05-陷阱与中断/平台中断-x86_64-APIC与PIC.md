# 平台中断：x86_64 APIC 与 PIC

v0.1 · 2026-08-29

本篇覆盖：`arch/x86_64/PIC/PIC.c`、`LocalAPIC.c`、`IRQ.c`、`include/arch/x86_64/PIC/*.h`（含**空壳** `IOAPIC.h`）、以及与 `arch_start_core` / timer / IPI 的衔接。

向量 reserve / `register_irq_handler` 见 `IRQ向量分配与处理.md`；IDT/`trap_handler` 见 Trap 篇；软 IPI 协议见 `06-SMP与同步/软IPI机制.md`；MADT 枚举见 `09-平台模块/ACPI与MADT-x86_64.md`；timer 校准语义见时间子系统篇。

**纠正：** 仓库**无** `PIC/PIT.c`；8254 在 `modules/driver/timer/8254`（及 `arch/.../time/time.c` 调用）。`IRQ.c` **不**「连接 HW IRQ 号与 trap id」——只做控制器选型。

---

## 1. 概述

PC 上外设不会直接敲内核。CPU 侧永远是：**向量号 → IDT → `trap_info`=向量 → `trap_handler`**。本篇讲向量**怎么从硬件控制器长出来**。

三选一，不是「PIC+APIC 同时路由」。

| 模式 | 何时 | 设备怎么进 CPU |
|------|------|----------------|
| **8259 PIC** | 无 APIC | 主/从 8259 → `INTR` → IDT（主片基址编程为 `0x20`） |
| **xAPIC** | 有 APIC、无/不用 x2APIC | LAPIC MMIO `0xFEE00000`；timer/IPI/spurious 走 LVT/ICR/SVR |
| **x2APIC** | CPUID 支持且启用 | 同一语义，寄存器改走 **MSR**（`0x800+index`） |

**Local APIC**：每核一个；定时器、LINT、IPI、spurious → IDT 向量。  
**I/O APIC**：本应编 PCI/ISA 线；本仓库 **`IOAPIC.h` 真·空壳**，MADT IOAPIC 条目 `break` 空操作。

运行时 `arch_irq_type` 决定 `arch_eoi_irq` 走 PIC EOI 还是 APIC EOI。选 APIC 时只是 **`disable_PIC`（全 mask）**，不是 ExtINT 并存。

---

## 2. 目标与边界

**提供：** QEMU/常见 PC 可启动的最小中断树——8259 或 LAPIC（含 x2APIC 模式切换）、LVT timer、ICR IPI、EOI。

**不做：** 完整 IOAPIC/MSI/HPET 校准源；Linux IRQ domain；把 MADT 当 IOAPIC 编程入口。

---

## 3. 分层与调用方

**BSP 平台一次** — ACPI → `parser_apic()` 枚举 Local APIC → `NR_CPU`；**不**在此 `init_irq`。

**每核** — 见 §6.1。Timer / IPI 注册在 IRQ 篇与软 IPI 篇；本篇提供 LVT/ICR/SVR 原语。

**设备** — v0.1 勿假定 IOAPIC 自动路由；无 IOAPIC 则 PCI IRQ 基本接不上。

---

## 4. 数据结构与不变量

### 4.1 向量与硬件绑定

| 向量 | 用途 | 硬件 |
|------|------|------|
| `0x20` | Timer | PIC 路径 = master IRQ0 基址；APIC = LVT Timer 向量字段 |
| `0x27` | Spurious | 写入 SVR；**无 handler** → 误入 panic |
| `0x30` | IPI | ICR FIXED → 软 IPI / bring-up |

向量语义与 pool 见 IRQ 篇；本篇只钉「谁写出这个号」。

### 4.2 关键寄存器（代码真实触碰）

| 概念 | 作用 |
|------|------|
| `IA32_APIC_BASE` | 硬件开 APIC / x2APIC |
| SVR | bit8 软使能 + spurious=`0x27` |
| LVT Timer / DCR / INIT_CNT / CURR_CNT | 周期 / one-shot / TSC-deadline |
| LVT PERF / LINT0 | `reset_APIC`：PERF→NMI；LINT0 mask（**LINT1 漏写**——像复制粘贴错误） |
| TPR=0 | 不挡优先级 |
| DFR/LDR | 仅 xAPIC 逻辑目的地 |
| EOI | `arch_eoi_irq` 分支 |
| ICR | INIT/SIPI + 软 IPI `0x30` |
| 8259 ICW1–4 / OCW1 | 基址 `0x20`/`0x28`；IMR 初值 master `0xFB`（保留 cascade IRQ2） |

### 4.3 `arch_irq_type`

`PIC_IRQ` / `xAPIC_IRQ` / `x2APIC_IRQ`——决定 EOI 与 IPI 发送后端。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `PIC/IRQ.c` | **选型** PIC vs xAPIC vs x2APIC；`init_irq` / `arch_eoi_irq` |
| `PIC/PIC.c` | 8259 init / mask / EOI |
| `PIC/LocalAPIC.c` | MMIO/MSR、reset、SVR、LVT、ICR、EOI |
| `PIC/IOAPIC.h` | **空壳** |
| `arch/.../time/time.c` | 校准 + 调 LVT；`software_enable_APIC` 在此路径 |
| `modules/driver/timer/8254` | PIT 校准源（非 `PIC/PIT.c`） |

---

## 6. 流程

### 6.1 每核时序

```text
init_interrupt()          // reserve + lidt
init_irq()                // 三选一；APIC 路径 disable_PIC
smp_ipi_init()            // register 0x30
… init_syscall … 
arch_enable_irq()         // sti
rendezvos_time_init()
  └ arch_init_timer()     // 校准 + LVT timer + software_enable_APIC()
```

**陷阱：** SVR 软使能 **不在** `init_irq` 末尾，而在 **`arch_init_timer`**。

### 6.2 `init_irq` 决策

| 条件 | 动作 |
|------|------|
| CPUID APIC + x2APIC | `x2APIC_IRQ`：disable_PIC → enable_x2APIC → `reset_APIC` |
| 仅 APIC | `xAPIC_IRQ`：disable_PIC → MADT 基址须 **`== 0xFEE00000`** → map LAPIC → enable → LDR → `reset_APIC` |
| 无 APIC | `PIC_IRQ`：`init_PIC()` |

MADT `Local_int_ctrl_address` 若非 FEE00000，xAPIC 路径直接 return——**不**跟 override 重映射。

### 6.3 EOI

`arch_eoi_irq`：按 `arch_irq_type` 调 `PIC_EOI` 或 `APIC_EOI`。  
PIC slave EOI **不**连带 EOI master（经典缺陷，文档写死）。

### 6.4 IPI / Timer（边界）

- IPI：`APIC_send_IPI` → ICR；协议见软 IPI 篇。  
- Timer：LVT + PIT 校准见时间篇；向量 `0x20`。

---

## 7. 公开 API（arch 内部为主）

选型与 EOI 经 `init_irq` / `arch_eoi_irq`；LAPIC 读写宏、`software_enable_APIC`、`APIC_send_IPI` 供 timer/IPI 使用。上层驱动应走 IRQ 篇 alloc，勿直接碰未实现的 IOAPIC。

---

## 8. 多架构

仅 x86_64。aarch64 → GIC 篇。

---

## 9. 测试

间接：timer tick、SMP bring-up、软 IPI。本篇未复测。

---

## 10. 限制与后续 / 已知缺口

- **IOAPIC 空壳**；MADT IOAPIC 未消费。  
- **x2APIC ICR** 经 `wrmsr`（edx 强制 0）时定向目的地可疑。  
- **`reset_APIC` 不写 LINT1**；`disable_APIC` 清 bit 逻辑可疑。  
- Spurious 无 handler。  
- ISR/IRR 调试 API 可能对 RO 寄存器无效。  
- HPET 校准未接。

---

## 11. 变更记录

- 2026-08-29：整篇重做——三选一叙述；init 时序；寄存器/向量实表；IOAPIC 空壳；纠正 IRQ.c/PIT 路径；x2APIC/spurious/PIC EOI/LINT1 缺口。
- 2026-08-27：初稿（偏浅）。
