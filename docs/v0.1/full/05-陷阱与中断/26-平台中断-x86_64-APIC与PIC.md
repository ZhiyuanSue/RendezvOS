# 平台中断：x86_64 APIC 与 PIC

v0.1 · 2026-09-25

本篇覆盖：`arch/x86_64/PIC/PIC.c`、`LocalAPIC.c`、`PIT.c`、`IRQ.c`、`include/arch/x86_64/PIC/*.h`（含**空壳** `IOAPIC.h`）、以及与 `arch_start_core` / timer / IPI 的衔接。

向量 reserve / `register_irq_handler` 见 `25-IRQ向量分配与处理.md`；IDT / `trap_handler` 见 Trap 篇；软 IPI 协议见 `06-SMP与同步/30-软IPI机制.md`；MADT 枚举见 `09-平台模块/35-ACPI与MADT-x86_64.md`；timer 校准语义见时间子系统篇。

**官方手册：** Intel SDM Vol.3 — *Interrupt and Exception Handling*、*Advanced Programmable Interrupt Controller (APIC)*、*8259A* 兼容说明；PC 平台惯例见 ACPI MADT。本篇用手册概念对照本仓库**真实触碰**的寄存器与时序。

**纠正：** `IRQ.c` **不**「连接 HW IRQ 号与 trap id」——只做控制器选型。8254 PIT 实现在 `arch/x86_64/PIC/PIT.c`（校准 / 可选时钟源）；若另有 `modules/driver/timer/8254`，以链接实际为准，勿假定「无 PIT.c」。

---

## 1. 概述：硬件拓扑与 OS 依赖

PC 上外设不会直接敲内核入口。CPU 侧永远是：

```text
外设线 / 定时器 / IPI
  → 中断控制器（8259 或 Local APIC [+ 将来 I/O APIC]）
  → 选出一个 IDT 向量号
  → CPU 查 IDT → trap_vec → trap_handler(trap_info=向量)
```

本篇讲中间那一层：**向量怎么从硬件控制器长出来**，以及 OS 必须怎样编程控制器，才能和 IRQ 篇的 reserve 表对齐。

### 1.1 三选一（不是 PIC+APIC 同时路由）

| 模式 | 何时 | 设备怎么进 CPU |
|------|------|----------------|
| **8259 PIC** | 无 APIC（罕见现代机） | 主 / 从 8259 → CPU `INTR` 脚 → IDT（主片基址编程为 `0x20`） |
| **xAPIC** | CPUID 有 APIC、无 / 不用 x2APIC | 每核 Local APIC，MMIO **`0xFEE00000`**；timer / IPI / spurious 走 LVT / ICR / SVR |
| **x2APIC** | CPUID 支持且启用 | 同一语义，寄存器改走 **MSR**（基址 `0x800` + 寄存器索引） |

运行时 `arch_irq_type`（`PIC_IRQ` / `xAPIC_IRQ` / `x2APIC_IRQ`）决定 `arch_eoi_irq` 与 IPI 后端。选 APIC 时只是 **`disable_PIC`（全 mask）**，不是 ExtINT 与 PIC 并存。

### 1.2 Local APIC vs I/O APIC（OS 必须分清）

| 部件 | 角色 | 本仓库 |
|------|------|--------|
| **Local APIC（LAPIC）** | **每核一个**；接收投递给本核的中断；自带 timer、LINT、IPI、spurious | **已实现**（`LocalAPIC.c`） |
| **I/O APIC** | 板级 / 芯片组；把 PCI / ISA 线编成「发到某个 LAPIC 的某个向量」 | **`IOAPIC.h` 真·空壳**；MADT IOAPIC 条目 `break` 空操作 |

没有 IOAPIC，就没有规范的「外设 IRQ → 向量」编程路径——timer / IPI 仍可走 LAPIC 自有源；PCI 设备中断基本接不上。

### 1.3 OS 对硬件的硬依赖（读代码前先钉死）

1. **IDT 必须先装好**（`lidt`），向量 gate 指向 `trap_N`；否则开中断即飞。
2. **写进 LVT / SVR / ICR / PIC ICW2 的向量号**，必须已在 `irq_vector[]` **reserve**，且已 `register_irq_handler`（timer / IPI）或可进 unknown。
3. **处理完设备 / timer / IPI 类中断后要 EOI**（`IRQ_NEED_EOI` → `APIC_EOI` 写 0，或 `PIC_EOI`）；漏 EOI 会卡住同级 / level 线。
4. **用 APIC 就必须 mask 掉 8259**，否则双路径乱投。
5. **xAPIC 须把 `0xFEE00000` 映成 UNCACHED**（`map_LAPIC`）；缓存一致性错误会读到脏寄存器镜像。

---

## 2. 目标与边界

**提供：** QEMU / 常见 PC 可启动的最小中断树——8259 或 LAPIC（含 x2APIC 模式切换）、LVT timer、ICR IPI、EOI、PIT 校准源。

**不做：** 完整 IOAPIC / MSI / HPET 校准源；Linux IRQ domain；把 MADT 当 IOAPIC 编程入口。

---

## 3. 分层与调用方

**BSP 平台一次** — ACPI → `parser_apic()` 枚举 Local APIC → `NR_CPU`；**不**在此 `init_irq`。

**每核** — 见 §6.1。Timer / IPI 注册在 IRQ 篇与软 IPI 篇；本篇提供 LVT / ICR / SVR 原语。

**设备** — v0.1 勿假定 IOAPIC 自动路由。

---

## 4. 硬件深度：8259 PIC

### 4.1 芯片角色（Intel 8259A 兼容）

PC-AT 级联两片：

- **主片**：IRQ0–7；IRQ2 接从片 cascade。
- **从片**：IRQ8–15。

CPU 只有一根 INTR；8259 用中断向量号告诉 CPU「查 IDT 哪一项」。我们在 ICW2 把主片基址设为 **`0x20`**、从片 **`0x28`**，于是：

| 8259 线 | 向量 | 典型用途（PC 惯例） |
|---------|------|---------------------|
| IRQ0 | `0x20` | PIT 定时器 |
| IRQ1 | `0x21` | 键盘 |
| IRQ2 | — | cascade，不单独作设备 |
| … | … | … |
| IRQ8–15 | `0x28`–`0x2F` | RTC、鼠标、IDE… |

这与 IRQ 篇 reserve 的 timer=`0x20` **故意对齐**：PIC 模式下 IRQ0 就是系统 tick。

### 4.2 初始化序列（`init_PIC` ↔ ICW1–4）

端口：主片 `0x20/0x21`，从片 `0xA0/0xA1`（`io_port.h`）。

| ICW | 本实现要点 |
|-----|------------|
| ICW1 | 需要 ICW4、边沿触发、级联（非单片） |
| ICW2 | 向量基址高 5 位：主 `0x20`、从 `0x28` |
| ICW3 | 主：bit2=1（从片挂 IRQ2）；从：写 index=2 |
| ICW4 | 8086 模式（`uPM`）；**手动 EOI**（非 AEOI） |

随后 OCW1（IMR）：主片初值 **`0xFB`**（放开 cascade IRQ2，其余 mask）、从片 **`0xFF`** 全 mask。设备要用时再 `enable_PIC_IRQ`。

### 4.3 EOI（OCW2）

`PIC_EOI(irq_num)`：向对应片写 `_8259A_OCW_2_EOI_`。

**已知缺陷（文档写死）：** 从片 EOI **不**连带 EOI 主片。经典 8259 对从片中断通常需要「先从后主」两次 EOI；当前实现可能令 cascade 路径异常——PIC 路径本就不是 SMP 主路径，但仍是缺口。

### 4.4 OS 依赖小结（PIC）

- 必须先 `lidt`，IDT[`0x20`…] 有效。
- 向量基址改了就必须同步改 `ARCH_IRQ_VEC_*` / reserve——现在写死 `0x20`/`0x28`。
- 手动 EOI：handler 经 `arch_eoi_irq` → `PIC_EOI`。

---

## 5. 硬件深度：Local APIC

### 5.1 启用与寻址（SDM：IA32_APIC_BASE）

MSR **`IA32_APIC_BASE`**：

- 置 **APIC Global Enable** → `enable_xAPIC`；
- 再置 **x2APIC enable** → `enable_x2APIC`（同时保留 enable）。

| 模式 | 访问 | 基址 |
|------|------|------|
| xAPIC | MMIO，寄存器间距 0x10 | 物理 **`0xFEE00000`**（本仓库要求与 MADT `Local_int_ctrl_address` **精确相等**，否则直接 return） |
| x2APIC | MSR `0x800 + index` | 无独立 MMIO 映射 |

`map_LAPIC`：仅 xAPIC；映成 **UNCACHED | GLOBAL | RW**。LAPIC 是 MMIO 设备寄存器，缓存会破坏读-改-写与 EOI 语义。

### 5.2 SVR — 软使能与 Spurious（SDM：Spurious-Interrupt Vector Register）

**SVR**（寄存器索引 `0xF`）：

- **bit8 `SW_ENABLE`**：Local APIC 软件使能。关着时大量投递被吞或行为异常。
- **低 8 位**：spurious 向量——硬件无法交付「真」中断时可能投这个向量。

本仓库：`software_enable_APIC()` 置 enable，并把 spurious 写成 **`ARCH_IRQ_VEC_SPURIOUS`（0x27）**。

**时序陷阱：** SVR 软使能 **不在** `init_irq` 末尾，而在 **`arch_init_timer` 路径**上才调用。开中断（`sti`）与真正「APIC 开始干活」之间可能有窗口——读启动日志时别假设 `init_irq` 完就能收 timer。

**Spurious 无 handler**：IRQ 篇只 reserve `0x27`；若误入 → `trap_handler` unknown → panic。正常不应频繁 spurious。

### 5.3 LVT — 本地向量表（SDM：Local Vector Table）

每项描述「本核本地源 → 哪个向量、什么投递模式、是否 mask」：

| LVT | 本实现 | 含义 |
|-----|--------|------|
| **Timer** | 校准后写向量=`timer_irq_num`（0x20）、模式 periodic / one-shot / TSC-deadline | 系统 tick 源 |
| **LINT0** | `reset_APIC` 里 **MASKED** | 常接 ExtINT / 8259；我们用 APIC 时 mask 掉 |
| **LINT1** | **漏写**（像复制粘贴错误，LINT0 写了两次） | 常接 NMI；当前未显式 mask |
| **PERF** | 投递模式 **NMI** | 性能计数溢出 → NMI（进 IRQ class，见 Trap 篇） |
| Thermal / Error / CMCI | 未重点用 | — |

LVT 字段（本代码用到的）：

- **Vector[7:0]**：必须 ∈ 已 reserve 的软件槽。
- **Mask**：1 = 不投递。
- **Timer mode**：one-shot / periodic / TSC-deadline（需 CPUID TSC_Deadline）。
- **Delivery mode**：Fixed / NMI / ExtINT / SMI / INIT…

**OS 依赖：** 改 LVT 向量前，软件 `register_irq_handler` 必须已挂好；否则第一声 tick 就 panic。

### 5.4 定时器计数器（与 LVT Timer 配套）

| 寄存器 | 作用 |
|--------|------|
| **DCR** | 分频（本实现校准 / 运行多用 `DIV_16`） |
| **INIT_CNT** | 写入初始计数；开始倒数 |
| **CURR_CNT** | 当前剩余（只读） |

校准：`APIC_timer_calibration` 用 **8254 PIT** 作时间基准（`PIT_mdelay`），测 APIC 计数频率；TSC-deadline 路径另用 `TSC_timer_calibration`。HPET 未接。

TSC-deadline 模式：不依赖 INIT_CNT 倒数，而写 MSR **`IA32_TSC_DEADLINE`**；到点仍经 LVT Timer 向量进 IDT。

### 5.5 TPR / 优先级

`reset_APIC` 把 **TPR=0**：不抬高任务优先级门槛，允许接收较低优先级中断。PPR / ISR 位图存在，调试 API（`lapic_get_vec`）可读 ISR / IRR / TMR；对 RO 寄存器的 set/clear 可能无效——已知缺口。

### 5.6 IRR / ISR / EOI（SDM：中断接收）

硬件大致流程：

1. 中断到达 → 置 **IRR**（Interrupt Request）对应向量位；
2. 被接受 → 进 **ISR**（In-Service），CPU 开始跑 IDT handler；
3. 软件写 **EOI 寄存器 = 0** → 清最高优先级 ISR 位，允许同级后续中断。

本仓库：`APIC_EOI()` → `APIC_WR_REG(EOI, …, 0)`；由 `arch_eoi_irq` 在 `IRQ_NEED_EOI` 时调用。**不要**在 ISR 里提前 EOI 再做长活（除非明确理解 level 重入风险）。

### 5.7 ICR — 核间中断（SDM：Interrupt Command Register）

IPI / AP bring-up（INIT-SIPI）都走 ICR：

| 字段 | 本代码宏 | 用途 |
|------|----------|------|
| Vector | 低 8 位 | 软 IPI 用 **`0x30`** |
| Delivery mode | FIXED / INIT / STARTUP / NMI / … | bring-up 用 INIT + STARTUP；运行时软 IPI 用 FIXED |
| Dest mode | physical / logical | |
| Level / Trigger | assert + edge 等 | INIT 序列需要 |
| Destination shorthand | self / all / all-ex-self / no shorthand | |
| Dest field | xAPIC 在 ICR_HIGH[63:56]；x2APIC 在 64-bit ICR | 目标 APIC ID |

**xAPIC 发送顺序（硬件要求，代码已遵守）：**

1. 等 ICR **Delivery Status** 清零（上一次送完）；
2. **先写 ICR_HIGH**（目的地），**再写 ICR_LOW**（触发发送）。

**x2APIC：** 单次 `wrmsr` 64-bit ICR；无 Delivery Status 轮询。注释提醒：部分路径 `edx` 强制 0 时定向目的地可疑——已知缺口。

**OS 依赖：** `0x30` 必须已 register；软 IPI 协议（generation / pending）见 SMP 篇——本篇只提供「把向量砸到目标核」的门铃。

### 5.8 DFR / LDR（仅 xAPIC 逻辑目的地）

`reset_xAPIC_LDR`：DFR=全 1（flat model），LDR 低位设 bit0。x2APIC 下 DFR 不用、LDR 只读。逻辑目的地 IPI 依赖这些；物理 dest 路径更常用。

---

## 6. 数据结构与不变量（软件侧）

### 6.1 向量与硬件绑定

| 向量 | 用途 | 硬件写出者 |
|------|------|------------|
| `0x20` | Timer | PIC：IRQ0 基址；APIC：LVT Timer |
| `0x27` | Spurious | SVR |
| `0x30` | IPI | ICR FIXED |

### 6.2 `arch_irq_type`

决定 EOI 与 IPI 后端；`init_irq` 一次选型，之后只读。

---

## 7. 代码对应

| 路径 | 职责 |
|------|------|
| `PIC/IRQ.c` | **选型** PIC vs xAPIC vs x2APIC；`init_irq` |
| `PIC/APIC.h` | 伞头：include `IOAPIC.h` + `LocalAPIC.h` |
| `PIC/PIC.c` | 8259 init / mask / EOI |
| `PIC/LocalAPIC.c` | MMIO / MSR、reset、SVR、LVT、ICR、EOI、校准 |
| `PIC/PIT.c` | 8254 编程 / `PIT_mdelay` |
| `PIC/IOAPIC.h` | **空壳** |
| `arch/.../trap/trap.c` `arch_eoi_irq` | PIC：`PIC_EOI(向量)`；APIC：`APIC_EOI()` 写 0（**不**带向量） |
| `arch/.../time/time.c` | 校准 + 调 LVT；`software_enable_APIC` |
| `arch/.../time/rtc.c` | **墙钟 CMOS 读**——不是 IRQ 路径 |

---

## 8. 流程

### 8.1 每核时序

```text
init_interrupt()          // reserve + lidt
init_irq()                // 三选一；APIC 路径 disable_PIC + enable + reset_APIC
smp_ipi_init()            // register 0x30
… init_syscall …
arch_enable_irq()         // sti
rendezvos_time_init()
  └ arch_init_timer()     // 校准 + LVT timer + software_enable_APIC()
```

### 8.2 `init_irq` 决策

| 条件 | 动作 |
|------|------|
| CPUID APIC + x2APIC | `x2APIC_IRQ`：disable_PIC → enable_x2APIC → `reset_APIC` |
| 仅 APIC | `xAPIC_IRQ`：disable_PIC → MADT 基址须 **`== 0xFEE00000`** → map LAPIC → enable → LDR → `reset_APIC` |
| 无 APIC | `PIC_IRQ`：`init_PIC()` |

MADT 地址若非 FEE00000，xAPIC 路径直接 return——**不**跟 override 重映射。

### 8.3 一条 timer 中断的端到端

```text
LAPIC 计数到 0（或 TSC deadline）
  → LVT Timer 向量 0x20
  → IDT[0x20] → trap_entry → trap_handler
  → timer ISR（IRQ_NEED_EOI）
  → arch_eoi_irq → APIC_EOI
  → 若用户来源 → schedule
```

EOI 分支差异（实现细节）：PIC 路径 `PIC_EOI(向量号)` 要区分主/从片；APIC 路径只向 EOI 寄存器写 0，硬件自己清最高 ISR 位。从片 PIC EOI **不**连带主片（缺口）。
---

## 9. 公开 API（arch 内部为主）

选型与 EOI 经 `init_irq` / `arch_eoi_irq`；LAPIC 读写宏、`software_enable_APIC`、`APIC_send_IPI` 供 timer / IPI 使用。上层驱动应走 IRQ 篇 alloc，勿直接碰未实现的 IOAPIC。

---

## 10. 多架构

仅 x86_64。aarch64 → GIC 篇。

---

## 11. 测试

间接：timer tick、SMP bring-up、软 IPI。本篇未复测。

---

## 12. 限制与后续 / 已知缺口

- **IOAPIC 空壳**；MADT IOAPIC 未消费 → 无规范 PCI IRQ。
- **x2APIC ICR** 定向目的地可疑。
- **`reset_APIC` 不写 LINT1**；`disable_APIC` 清 bit 用了 `&` 组合（逻辑可疑）。
- Spurious 无 handler。
- PIC 从片 EOI 不连带主片。
- ISR / IRR 调试 API 可能对 RO 寄存器无效。
- HPET 校准未接。

---

## 13. 变更记录

- 2026-09-25：对照源码补遗漏——`APIC.h` 伞头、`rtc.c` 非 IRQ、`arch_eoi_irq` PIC/APIC 差异；大幅扩写 8259/LAPIC 硬件与 OS 依赖。
- 2026-08-29：整篇重做——三选一；init 时序；IOAPIC 空壳；缺口表。
- 2026-08-27：初稿（偏浅）。
