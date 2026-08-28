# 平台中断：x86_64 APIC 与 PIC

v0.1 · 2026-08-27

本篇覆盖：`arch/x86_64/PIC/PIC.c`、`arch/x86_64/PIC/LocalAPIC.c`、`arch/x86_64/PIC/IRQ.c`、`arch/x86_64/PIC/PIT.c`、`arch/x86_64/time/time.c`、`arch/x86_64/time/rtc.c`、`include/arch/x86_64/PIC/*.h`、`include/arch/x86_64/time.h`、`include/arch/x86_64/io.h`、`include/arch/x86_64/io_port.h`、`include/arch/x86_64/msr.h`。

IRQ 向量抽象见 `IRQ向量分配与处理.md`；SMP IPI 见 `06-SMP与同步/软IPI机制.md`。

---

## 1. 概述

x86_64 bring-up 在 v0.1 使用 **8259 PIC 遗留路径** 与 **Local APIC/x2APIC** 组件的混合：`PIC.c` 初始化级联 8259；`LocalAPIC.c` 配置 per-CPU LAPIC timer 与 IPI；`IRQ.c` 连接 HW IRQ 号与内核 trap id；**IOAPIC 路由在 v0.1  largely 空壳或未完整接入**（README 已注明现状）。

PIT/HPET 相关：`PIT.c`、time 子系统可选时钟源；RTC 读 wall clock 在 `rtc.c`。定时器中断最终仍落到 **`register_irq_handler(ARCH_IRQ_VEC_TIMER, ...)`** 框架。

---

## 2. 目标与边界

core arch 模块提供 **QEMU/常见 PC 可启动** 的最小中断树，不是完整 ACPI/PCI MSI 中断子系统。兼容层或驱动扩展 PCI 设备 IRQ 时，须补 IOAPIC 编程或改用已知静态路由。

**不在本篇：** Linux IRQ domain、/proc/interrupts、irqbalance。

---

## 3. 分层与调用方

**启动** — `start_arch.c` / PIC 初始化在 BSP 上执行；AP 启动后各自 LAPIC init（SMP 篇）。

**timer** — arch time 后端注册 timer vector handler，编程 LAPIC timer 或 PIT 周期。

**IPI** — `arch_smp_ipi.c` 经 LAPIC ICR 发送，目标向量在 reserve 的 IPI 槽。

**设备驱动** — v0.1 样例驱动少；新设备须确认 HW IRQ→trap id 映射是否已实现，勿假定 IOAPIC 自动配置。

---

## 4. 数据结构与不变量

- **I/O port 访问** — `io_port.h` / `inb/outb` 用于 8259、PIT。
- **MMIO LAPIC** — MSR 或 mapped APIC base（`LocalAPIC.c`）。
- **arch_irq_type** — 枚举当前使用的 IRQ 路由模式（PIC vs APIC），影响 `arch_eoi_irq` 行为。

具体寄存器宏见各 `include/arch/x86_64/PIC/*.h`。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `PIC.c` | 8259 初始化、mask/unmask |
| `LocalAPIC.c` | LAPIC timer、IPI、EOI |
| `IRQ.c` | IRQ 号与 trap 向量衔接 |
| `PIT.c` | legacy 定时器 |
| `arch/x86_64/time/time.c` | 平台 tick 源 hook |
| `rtc.c` | 实时时钟读取 |

---

## 6. 流程

### 6.1 典型 BSP 中断链

```mermaid
flowchart LR
  A[Device/PIC/LAPIC] --> B[IDT vector]
  B --> C[trap_handler]
  C --> D[registered ISR]
  D --> E[arch_eoi_irq LAPIC/PIC]
```

### 6.2 IOAPIC 现状（v0.1）

- 文档与 README 一致：**完整 IOAPIC 表编程未作为 v0.1 冻结能力**。
- 在 QEMU default PC 上，部分设备 IRQ 仍经 8259 固定线；集成新硬件前须阅读 `IRQ.c` 实际分支。

---

## 7. 公开 API

平台层 mostly **内部**；对 rendezvos 公开的是 trap 层 `register_irq_handler` 与 time 抽象（`07-时间与定时器/时间与定时器子系统.md`）。PIC 模块函数由 arch init 调用，非 stable cross-arch API。

---

## 8. 多架构

本篇 **仅 x86_64**。aarch64 见 `平台中断-aarch64-GIC.md`。

---

## 9. 测试

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

验证：timer tick 打印、SMP IPI 测例、无 unhandled IRQ storm。

与当前源码一致，尚未复测。

---

## 10. 限制与后续

- **IOAPIC/MSI** — 见 `v0.1/evolution/TODO.md`（E5）；PCI 热插拔 IRQ 不完整。
- **x2APIC 模式** — 视 `LocalAPIC.c` 实现程度而定。
- **HPET 优先** — 配置依赖 `config_*.json` 与 time 模块。

---

## 11. 变更记录

| 日期 | 摘要 |
|------|------|
| 2026-08-27 | 初稿：PIC/LAPIC/PIT 与 IOAPIC 空壳说明 |
