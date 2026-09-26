# 平台中断：aarch64 GIC

v0.1 · 2026-09-25

本篇覆盖：`arch/aarch64/gic/gic_v2.c`、`include/arch/aarch64/gic/gic_v2.h`、`gic_v3.h`（占位）、以及与 `arch_start_platform` / `arch_start_core`、timer、IPI 的衔接。

DTB 节点见 `09-平台模块/36-DTB与设备树-aarch64.md`；trap id=`intid+64` 与 reserve 见 `25-IRQ向量分配与处理.md`；VBAR / `get_curr_el_trap_info` 见 Trap 篇；软 IPI 见 SMP 篇；PSCI 拉核见 `39-PSCI与处理器电源-aarch64.md`（本篇不展开）。

**官方手册：**

- **ARM Generic Interrupt Controller Architecture Specification（GICv2，文档号 IHI0048）** — Distributor / CPU Interface、INTID、IAR / EOIR、SGIR、优先级。
- **ARM Architecture Reference Manual（ARM ARM）** — VBAR_EL1、异常入口时 DAIF 置位、`eret`；CPU **不**内建「按 INTID 分槽的向量表」。

本篇用上述概念对照本仓库 **GICv2** 实现；`gic_v3.h` 几乎只是 include v2 + 注释——**不是**可用 v3 驱动。

---

## 1. 概述：没有 IDT，只有「IRQ 入口 + GIC 应答」

ARM 应用核（EL1）被中断时：

1. 硬件屏蔽 DAIF 中的 I（及通常更多位），保存 SPSR / ELR，跳到 **VBAR_EL1** 上对应的 **IRQ** 槽（Current EL 或 Lower EL）；
2. **不**携带「这是第几号外设」——要自己问 GIC；
3. 读 **GICC_IAR** 得到 **INTID**（及 SGI 时的 CPUID）；
4. 软件用 INTID 分发；处理完写 **GICC_EOIR**（通常写回 IAR 原值）。

core 把第 3 步的 INTID 映射成 trap id，塞进统一的 `irq_vector[]`：

```text
trap_id = intid + 64
```

前 64 个槽留给 ESR.EC（同步异常）。宏：`AARCH64_IRQ_TO_TRAP_ID` / `AARCH64_TRAP_ID_TO_IRQ`。

### 1.1 GIC 两大块（GICv2）

| 块 | 角色 | 本仓库 |
|----|------|--------|
| **GICD（Distributor）** | **全局一份**：SPI 配置、发 SGI、priority / target / enable、类型（边沿 / 电平） | `gic.gicd`，DTB `reg` 第一段 |
| **GICC（CPU Interface）** | **每核一份**：`IAR` 应答、`EOIR` 结束、`PMR` / `BPR` / `CTLR` | `gic.gicc`，DTB `reg` 第二段 |

（还有 GICH 等虚拟化扩展——头文件有结构、**无驱动**。）

### 1.2 INTID 分段（IHI0048）

| 范围 | 名称 | 含义 | OS 含义 |
|------|------|------|---------|
| 0–15 | **SGI** | Software Generated Interrupt | 核间门铃（本仓库 IPI = **SGI 0**） |
| 16–31 | **PPI** | Private Peripheral Interrupt | **每核私有**（本仓库 timer = **PPI 30**） |
| 32–1019 | **SPI** | Shared Peripheral Interrupt | 板级 / 外设共享线；可配 affinity |

**纠正：** Timer 是 **PPI 30**，不是 SPI。时间子系统若写「GIC SPI」是错的。

### 1.3 OS 对硬件的硬依赖

1. **先 `set_vbar_el1`**，IRQ 槽指向 `el*_trap_entry`；否则开中断即飞。
2. **进 IRQ 入口后必须读 IAR**（`gic.read_irq_num`）才能知道 INTID——只靠 VBAR 偏移不够。
3. **写进软件表的 trap id** 必须 = `intid+64`，且已 reserve / register。
4. **处理完必须 EOI**（`GICC_EOIR`）；漏 EOI 则同 INTID 可能卡在 active。
5. **GIC 寄存器必须 DEVICE 属性映射**（`map_gic_mem`）——普通缓存页会导致 MMIO 乱序 / 丢失。
6. **SPI 默认全 mask**；SGI+PPI 在 CPU IF init 时全 unmask——timer / IPI 才能响，外设须显式 `unmask`。

---

## 2. 目标与边界

**提供：** virt 可启动的 GICv2 最小驱动——probe / map、dist / CPU IF、mask / unmask、EOI、SGI。

**不做：** Linux irqchip 层级；ITS / LPI / MSI；GICv3 / v4 真实现；GICH 虚拟化。

---

## 3. 分层与调用方

**平台一次** — DTB → `gic.probe` → `gic.init_distributor`（只配 SPI）。同阶段有 `psci_init`——电源归 PSCI 篇。

**每核** — `init_interrupt`（VBAR + **立刻 enable IRQ**）→ `gic.init_cpu_interface` → IPI / timer register。

**驱动** — alloc 得 trap id 后，对 SPI：`intid = trap_id - 64`，再 `gicd_v2_unmask_irq` / affinity；勿把手写 EC 当设备号。

---

## 4. 硬件深度：Distributor（GICD）

### 4.1 关键寄存器（本代码触碰）

| 寄存器 | 作用 | OS 用法 |
|--------|------|---------|
| **GICD_CTLR** | 全局开关；Group0 / Group1 enable | init 先写 0，末尾开 Group0 |
| **GICD_TYPER** | ITLinesNumber、CPU 数等 | 推算 SPI 上界 |
| **GICD_ISENABLER / ICENABLER** | 置位 enable / clear enable | `unmask_irq` / `mask_irq` |
| **GICD_IPRIORITYR** | 每 INTID 一字节优先级 | init 写 `irq/8`；**现实现用 `|=`**，写 0 可能空操作 |
| **GICD_ITARGETSR** | SPI 目标 CPU 位图（每 INTID 一字节） | `set_affinity`；**仅 SPI**；SGI/PPI 的 target 只读 |
| **GICD_ICFGR** | 边沿 / 电平（每 INTID 2 bit） | SPI 现设 edge+1-N；SGI edge；PPI level |
| **GICD_SGIR** | 写即发 SGI | `send_sgi`：target list / other / self |
| **GICD_ICPENDR** | 清 pending | `pending_clr` |

### 4.2 `init_distributor` 在做什么

```text
GICD_CTLR = 0
读 TYPER → irq_num / cpu_num
对每个 SPI:
  set_type(edge | 1-N)
  set_affinity(…, 0)     // 意图：打到 core0；|= 实现可能未真正清旧位
  set_priority(irq/8)
  mask_irq
GICD_CTLR = Group0 enable
```

**不**在 dist init 里动 SGI/PPI——那些在每核 `init_cpu_interface`。

### 4.3 SGIR 与 IPI

写 **GICD_SGIR**：

- 低位 = SGI 号（0–15）；
- filter：指定 list / 除自己外所有 / 仅自己；
- list 位图在对应字段。

本仓库软 IPI / SMP 门铃用 **SGI 0** → trap id **64**。协议（pending 位、generation）见软 IPI 篇；本篇只提供「硬件把 SGI 砸到目标 CPU interface」。

### 4.4 Affinity 限制（硬件）

- **SPI**：可写 ITARGETSR，决定哪些 CPU 能收到。
- **SGI / PPI**：**不能**靠 ITARGETSR 绑核（对 PPI/SGI 那些字节是 RO）。PPI 天然「只打到本核」；SGI 的目标由 SGIR 的 filter/list 决定。

因此：「给 timer 设 affinity」在 GICv2 上对 PPI **无意义**——每核自己的 PPI30 只进本核 GICC。

---

## 5. 硬件深度：CPU Interface（GICC）

### 5.1 关键寄存器

| 寄存器 | 作用 | OS 用法 |
|--------|------|---------|
| **GICC_CTLR** | 本核 CPU IF 使能 | 写 Group1 enable（见 §5.4 命名塌缩） |
| **GICC_PMR** | Priority Mask：只接收优先级 **严格优于** 该阈值的中断 | 设 `0xff` = 尽量全收 |
| **GICC_BPR** | Binary Point：优先级分组 | 设 `0x3` |
| **GICC_IAR** | **读即应答**：返回 INTID[+CPUID]；1023=spurious | `read_irq_num` |
| **GICC_EOIR** | **写即结束**：应写回与 IAR 相同的值 | `eoi` |
| **GICC_DIR** | 分离 deactivate（EOImode 时） | **本实现不用**——注释写明只走 EOIR |

### 5.2 IAR / EOIR 协议（OS 必须配对）

硬件约定（IHI0048）：

1. CPU 取中断时读 **IAR** → 该 INTID 进入 active；
2. ISR 跑完写 **EOIR**（同一 INTID，SGI 还要同一 CPUID）→ 结束 active；
3. 若配置了拆分 EOI 模式，还要再写 DIR——我们关掉这条路径。

本仓库链路：

```text
VBAR IRQ → get_curr_el_trap_info
  source = gic.read_irq_num()          // 读 IAR
  tf->trap_info = intid + 64           // ⚠ 丢掉 IAR.CPUID
→ trap_handler → ISR
→ NEED_EOI → arch_eoi_irq
  从 trap_info 还原 intid → gic.eoi(source)  // 写 EOIR
```

**缺口：** `get_curr_el_trap_info` 只保留 `irq_id`，**丢掉 IAR 的 CPUID 字段**。对 SPI/PPI 通常够用；对 **SGI**，规范要求 EOIR 带上源 CPU ID——当前可能不完整。多核 IPI 压力下若出现异常，优先怀疑这里。

**Spurious：** IAR 返回 INTID **1023** 表示无有效中断。软件应识别并避免当设备号分发（当前映射会变成巨大 trap id，易进 unknown）。

### 5.3 `init_cpu_interface` 在做什么

```text
SGI/PPI: set_priority, unmask, set_type (SGI edge / PPI level)
GICC_BPR = 3
GICC_PMR = 0xff
GICC_CTLR = ENABLE_GROUP1
```

于是 **timer PPI30、IPI SGI0 在硬件上已能到达本核**；软件还须 `register_irq_handler` 才不会 panic。

### 5.4 Group0 / Group1 与 Security

GICD 开 **Group0**、GICC 开 **「Group1」** 宏位——在**无 Security 扩展**的 GICv2（QEMU virt 常见）上，这些 bit 的语义常塌成单一 Enable。**virt 上可能仍工作**；不要解读成「已正确区分 Secure / Non-secure Group」。真 Secure 世界要用另一套编程，本仓库未做。

---

## 6. 数据结构与不变量（软件侧）

### 6.1 INTID ↔ trap（必记）

| 用途 | INTID | trap id |
|------|-------|---------|
| IPI | SGI **0** | **64** |
| Timer | PPI **30** | **94** |
| 设备 pool | SPI 32–1019 | 96–1083 |

### 6.2 全局 `gic`

`gicd` / `gicc` 虚拟指针 + ops。映射属性：**DEVICE**。

### 6.3 `compatible`

现硬编码 **`arm,cortex-a15-gic`**。DTB 若写 `arm,gic-400` 等会对不上 → probe 失败则静默 return（后续空指针风险取决于调用方检查）。

---

## 7. 代码对应

| 路径 | 职责 |
|------|------|
| `gic_v2.c` / `.h` | probe、map、dist / CPU IF、mask、EOI、SGI |
| `gic_v3.h` | 占位 |
| `arch/.../trap/trap.c` | IAR→trap_id；`arch_eoi_irq` |
| `generic_time.c` | arch timer（CNTP_*）+ 消费 PPI 线 |
| `arch_smp_ipi` | SGI0 |

---

## 8. 流程

### 8.1 Probe / map

1. DTB `compatible` 匹配 → 找节点。
2. `reg` 四元组：`[gicd_phys, len, gicc_phys, len]` → DEVICE 映射 → `KERNEL_PHY_TO_VIRT`。

### 8.2 每核时序（与 x86 相反，必写）

```text
init_interrupt()
  └ set_vbar + arch_enable_irq()   // 已开 IRQ！
gic.init_cpu_interface()           // 然后才配 GICC / unmask SGI+PPI
smp_ipi_init()                     // SGI0 → trap 64
rendezvos_time_init()              // PPI30 → trap 94
```

x86 是先配控制器再 `sti`；aarch64 **先开 IRQ 再配 GICC**——启动窗口可疑，文档诚实写出。窗口内若有 pending，可能在 CPU IF 未就绪时进入 IRQ 入口。

### 8.3 一条 IRQ 的端到端

```text
外设/PPI/SGI 到达 GIC → 目标 CPU 的 IRQ 线拉高
  → VBAR IRQ 入口（DAIF.I 已屏蔽）
  → 读 GICC_IAR → INTID
  → trap_info = INTID+64
  → trap_handler → ISR
  → arch_eoi_irq → GICC_EOIR
  → 用户来源则 schedule → eret
```

FIQ 分支解码为空（Trap 篇）——勿指望 FIQ 路径。

### 8.4 与 arch timer 的接线

Generic timer 编程 **CNTP_***（物理定时器）在 EL1；到期拉高约定的 PPI。本仓库绑 **INTID 30**。

惯例上：virtual timer 常为 PPI 27、physical 常为 30（具体以 DTB `interrupts` 与平台为准）。注释 / 旧笔记提醒：CNTP + INTID30 在 QEMU virt 上能工作，**不要默认成「所有板级都正确」**——换平台应读 DTB 的 timer 中断属性。

---

## 9. 公开 API（arch 内部）

经全局 `gic` ops：`probe`、`init_distributor`、`init_cpu_interface`、`eoi`、`send_sgi`、mask / unmask、`set_affinity`（SPI only）。上层优先 IRQ 篇 alloc + 本篇 unmask SPI。

---

## 10. 多架构

仅 aarch64。x86 → APIC / PIC 篇。

---

## 11. 测试

间接：timer、SMP IPI、virt 启动。本篇未复测。

---

## 12. 限制与后续 / 已知缺口

- GICv3 / ITS / MSI 无。
- compatible 写死；与 DTB 举例可能不一致。
- 开中断早于 CPU IF。
- **IAR CPUID 丢失**；SGI EOI 可能不完整。
- **`AARCH64_TRAP_ID_MASK = 0x2FF`（767）** 与 pool 上界 trap **1083** 不一致：`TRAP_ID(tf->trap_info)` 会截断高 SPI（intid ≳ 703）——高编号外设向量在软件分发上不可靠，属实现缺口。
- Timer：**CNTP** + **INTID 30** 依赖平台约定。
- SPI 默认全 mask；affinity / priority 的 `|=` 实现可能写不进「全 0」。
- Group0 / Group1 命名在无 Security 时塌缩。
- `get_curr_el_trap_info` 未把 IAR.CPUID 打进 `trap_info`（虽有 `AARCH64_TRAP_CPU_*` 宏位），SGI EOI 路径不完整。

---

## 13. 变更记录

- 2026-09-25：大幅扩写硬件——GICD/GICC 寄存器、IAR/EOIR 协议、INTID 分段、SGIR、affinity 限制、OS 依赖清单与端到端链；手册入口 IHI0048 / ARM ARM。
- 2026-08-29：整篇重做——intid+64；PPI30≠SPI；开中断时序；EOI/CPUID；缺口表。
- 2026-08-27：初稿（偏浅）。
