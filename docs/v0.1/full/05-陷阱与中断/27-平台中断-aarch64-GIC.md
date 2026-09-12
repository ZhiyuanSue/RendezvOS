# 平台中断：aarch64 GIC

v0.1 · 2026-08-29

本篇覆盖：`arch/aarch64/gic/gic_v2.c`、`include/arch/aarch64/gic/gic_v2.h`、`gic_v3.h`（占位）、以及与 `arch_start_platform` / `arch_start_core`、timer、IPI 的衔接。

DTB 节点见 `09-平台模块/36-DTB与设备树-aarch64.md`；trap id=`intid+64` 与 reserve 见 `25-IRQ向量分配与处理.md`；VBAR/`get_curr_el_trap_info` 见 Trap 篇；软 IPI 见 SMP 篇；PSCI 拉核见 `39-PSCI与处理器电源-aarch64.md`（本篇不展开）。

---

## 1. 概述

ARM 没有 IDT。CPU 只有 VBAR 上几条异常入口；设备中断经 **GIC**：

| 块 | 角色 |
|----|------|
| **GICD（Distributor）** | 全局：SPI 配置、发 SGI、priority/target/enable |
| **GICC（CPU Interface，GICv2）** | 每核：`IAR` 读当前中断、`EOIR` 结束 |

**INTID：** SGI 0–15、PPI 16–31、SPI 32–1019。

假统一、真映射。汇编进 IRQ 后读 `GICC_IAR`，再：

```text
trap_id = intid + 64
```

前 64 个 `irq_vector` 槽留给 ESR.EC（同步异常）。宏：`AARCH64_IRQ_TO_TRAP_ID` / `AARCH64_TRAP_ID_TO_IRQ`。

v0.1 主路径 **GICv2**（DTB `compatible` 现为 `arm,cortex-a15-gic`）。`gic_v3.h` 几乎只是 include v2 + 注释——**不是**可用 v3 驱动。

---

## 2. 目标与边界

**提供：** virt 可启动的 GICv2 最小驱动——probe/map、dist/CPU IF、mask/unmask、EOI、SGI。

**不做：** Linux irqchip 层级；ITS/LPI/MSI；GICv3/v4 真实现；GICH 虚拟化扩展（头文件有结构、无驱动）。

---

## 3. 分层与调用方

**平台一次** — DTB → `gic.probe` → `gic.init_distributor`（只配 SPI）。同阶段有 `psci_init`——边界：电源归 PSCI 篇。

**每核** — `init_interrupt`（VBAR + **立刻 enable IRQ**）→ `gic.init_cpu_interface` → IPI/timer register。

**驱动** — alloc 得 trap id 后，对 SPI：`intid = trap_id - 64`，再 `gicd_v2_unmask_irq` / affinity；勿把手写 EC 当设备号。

---

## 4. 数据结构与不变量

### 4.1 INTID ↔ trap（必记）

| 用途 | INTID | trap id |
|------|-------|---------|
| IPI | SGI **0** | **64** |
| Timer | PPI **30** | **94** |
| 设备 pool | SPI 32–1019 | 96–1083 |

**纠正：** Timer 是 **PPI 30**，不是 SPI。时间子系统若写「GIC SPI」是错的。

### 4.2 全局 `gic`

`gicd` / `gicc` 虚拟指针 + ops（probe、init、eoi、send_sgi、mask/unmask…）。映射属性：**DEVICE**（`map_gic_mem`）。

### 4.3 EOI 契约

`arch_eoi_irq`：从 `trap_info` 取回 intid → `gic.eoi` → **`GICC_EOIR = IAR 原值`**（注释明确不用分离 `GICC_DIR`）。

**缺口：** `get_curr_el_trap_info` 把 IAR 收成 intid+64 时 **丢掉 IAR.CPUID**；SGI EOI 规范需要 CPUID——当前可能不完整。

### 4.4 PPI/SGI vs SPI

- PPI/SGI：**不能**靠 `ITARGETSR` 绑核（RO）；`set_affinity` 只接受 SPI。不是「AP 启动绑 PPI/SGI affinity」。  
- Dist init：SPI 全 mask；CPU IF init：**SGI+PPI 全 unmask** → timer/IPI 能通；外设 SPI 须显式 unmask。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `gic_v2.c` / `.h` | probe、map、dist/CPU IF、mask、EOI、SGI |
| `gic_v3.h` | 占位 |
| `arch/.../trap/trap.c` | IAR→trap_id；`arch_eoi_irq` |
| `generic_time.c` | arch timer（CNTP_*）+ 消费 PPI 线 |
| `arch_smp_ipi` | SGI0 |

---

## 6. 流程

### 6.1 Probe / map

1. DTB `compatible`（现硬编码 **`arm,cortex-a15-gic`**——写 `arm,gic-400` 会对不上，probe 失败则静默 return）。  
2. `reg` 四元组：`[gicd_phys, len, gicc_phys, len]` → DEVICE 映射 → `KERNEL_PHY_TO_VIRT`。

### 6.2 Distributor（SPI only）

`GICD_CTLR=0` → 读 `TYPER` → SPI：edge+1-N、`set_affinity(irq,0)`、priority、全 mask → `GICD_CTLR` 开 Group0。

注意：`set_affinity(..., 0)` / `set_priority` 现实现偏 `|=`——写 0 可能是空操作；「全部打到 core0」别写死为已验证正确。

### 6.3 每核时序（与 x86 相反，必写）

```text
init_interrupt()
  └ set_vbar + arch_enable_irq()   // 已开 IRQ！
gic.init_cpu_interface()           // 然后才配 GICC / unmask SGI+PPI
smp_ipi_init()                     // SGI0 → trap 64
rendezvos_time_init()              // PPI30 → trap 94
```

x86 是先配控制器再 `sti`；aarch64 **先开 IRQ 再配 GICC**——启动窗口可疑，文档诚实写出。

CPU IF：`GICC_BPR=3`、`PMR=0xff`、`CTLR` enable；SGI edge、PPI level。

### 6.4 IRQ 处理链

```text
VBAR IRQ → get_curr_el_trap_info
  IAR → trap_info = (intid+64) | …
→ trap_handler → ISR
→ NEED_EOI → arch_eoi_irq → GICC_EOIR
```

FIQ 分支解码为空（Trap 篇）。

### 6.5 Group0 / Group1 命名

GICD 开 Group0、GICC 开 “Group1”——在无 Security 扩展的 GICv2 上 bit 语义常塌成单一 Enable。**virt 上可能仍工作**；不是「已正确区分 Secure Group」。

---

## 7. 公开 API（arch 内部）

经全局 `gic` ops：`probe`、`init_distributor`、`init_cpu_interface`、`eoi`、`send_sgi`、mask/unmask。上层优先 IRQ 篇 alloc + 本篇 unmask SPI。

---

## 8. 多架构

仅 aarch64。x86 → APIC/PIC 篇。

---

## 9. 测试

间接：timer、SMP IPI、virt 启动。本篇未复测。

---

## 10. 限制与后续 / 已知缺口

- GICv3/ITS/MSI 无。  
- compatible 写死；与 DTB 举例可能不一致。  
- 开中断早于 CPU IF。  
- IAR CPUID 丢失；SGI EOI 可能不完整。  
- **`AARCH64_TRAP_ID_MASK = 0x2FF`** 与 pool 上界 1083 不一致——高 SPI 的 `TRAP_ID()` 可能截断。  
- Timer 编程 **CNTP（物理）**，向量绑 **INTID 30**（惯例上 virtual 常为 30、物理常为 29）——可能靠 QEMU 巧合，**不是架构正确性**；与 Timer 篇并读时点破。  
- SPI 默认全 mask。

---

## 11. 变更记录

- 2026-08-29：整篇重做——GICD/GICC 叙述；intid+64 实表；PPI30≠SPI；开中断时序；EOI/CPUID；compatible/v3 占位；affinity/priority/`TRAP_ID_MASK`/CNTP vs PPI 缺口。
- 2026-08-27：初稿（偏浅）。
