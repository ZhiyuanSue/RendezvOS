# 软 IPI 机制

v0.1 · 2026-09-27

本篇覆盖：`kernel/smp/ipi.c`、`include/rendezvos/smp/ipi.h`、`arch/{x86_64,aarch64}/smp/arch_smp_ipi.c`。

向量 reserve 见 IRQ 篇；门铃硬件（ICR / SGI）见 APIC / GIC 篇；**x86 TLB shootdown 握手**见 `32-TLB_shootdown与跨核一致性.md`（及 MM 的 TLB 策略篇）。本篇不把 aarch64 TLB 写成「也走 IPI」。

**官方手册：** Intel SDM — ICR（Interrupt Command Register，中断命令寄存器）FIXED 投递、向量进 IDT（Interrupt Descriptor Table，中断描述符表）；ARM GICv2 IHI0048 — `GICD_SGIR`、SGI（Software Generated Interrupt，软件生成中断）INTID 0–15。

---

## 1. 概述

硬件只发送一次门铃信号；真正工作用 **slot 表 + 每核 pending 位图**。

| | x86 | aarch64 |
|--|-----|---------|
| 门铃 | LAPIC（Local APIC）ICR **FIXED**，向量 **`0x30`** | GICD **SGI 0** → trap id **64** |
| 注册 | `register_irq_handler(ARCH_IRQ_VEC_IPI, …, NEED_EOI)` | 同左（宏叠 +64） |

AP 启动拉核用的 INIT / SIPI **不是** soft IPI（delivery mode 不同，只是共用 `APIC_send_IPI` 一类原语）——见拓扑篇。

**唯一 in-tree 默认消费者：x86 TLB shootdown。** aarch64 TLB 用 `tlbi *is`，**不**注册 TLB IPI，也 **无** `arch_smp_flush_tlb_init`。现行主线各 ISA 仍都会 `smp_ipi_init`——机制保留，aarch64 几乎无第二消费者。

### 1.1 OS 为何拆「门铃」和「工作」

硬件一次中断只有一个向量 / INTID（Interrupt ID），无法承载「刷哪一页、哪个 ASID」这类参数。协议是：

1. 发送方先把参数写进 **目标核的 per-CPU 消息槽**（TLB 篇）；
2. 再在目标核 `smp_ipi_pending` 上 OR 对应 slot bit；
3. 触发门铃（ICR / SGIR）；
4. 目标核 IRQ handler **exchange** 清 pending，按 bit 调已注册的 `fn()`（无参——上下文在消息槽里）。

门铃可合并（同一 bit 已置则不必再 OR）；发送失败则 **清除该 bit**，避免悬空 pending。

#### 1.1.1 x86 LAPIC ICR 要点

x86 的 IPI 通过 Local APIC 的 **ICR（Interrupt Command Register）**发出，本仓库用 xAPIC / x2APIC 两种映射：

- **xAPIC（MMIO）**：ICR 是两个 32 位寄存器——`ICR0`（偏移 `0x300`，低 32 位，含 delivery mode / dest mode / vector）和 `ICR1`（偏移 `0x310`，高 32 位，含目标 CPU 位图）。写 ICR0 触发发送。
- **x2APIC（MSR）**：ICR 合并为单个 64 位 MSR `IA32_X2APIC_ICR`（`0x830`），高 32 位是目标 CPU 物理号，低 32 位是 vector / mode。一次 wrmsr 即发。
- **Delivery Mode**：本仓库只用 **Fixed（000b）**——按 vector 投递到目标 CPU 的 IDT。其他模式（SMI/NMI/INIT/SIPI/LowestPri/ExtINT）不用在 soft IPI 路径。INIT/SIPI 仅在 AP 启动拉核时用，delivery mode 不同，**不**走 `smp_ipi_send`。
- **Vector**：固定 `0x30`（IDT 项 48）。所有 soft IPI 共享这一个向量，区分靠 pending bit。
- **Dest Mode**：Physical（按 APIC id）；本仓库假设 `cpu_id == APIC id`。

#### 1.1.2 aarch64 GIC SGI 要点

aarch64 的 IPI 走 GIC Distributor 的 **SGI（Software Generated Interrupt）**：

- **`GICD_SGIR`（`0xF00`，GICv2）**：32 位寄存器，`[23:16]` 是 CPU target list（每位对应一个 CPU interface），`[3:0]` 是 SGI INTID（0–15）。写即触发。
- **SGI INTID**：0–15，本仓库用 **0**；trap 进入向量 `0x40`（trap id 64）。
- **CPU interface 编号**：GICv2 上限 **8** 个 CPU interface，故 `cpu < 8` 才能发。本仓库假设 GIC CPU interface 编号 == 软件 `cpu_id`（MPIDR Aff0 连续编号时成立）。
- **GICv3** 用 `ICC_SGI1R_EL1`（64 位系统寄存器），target 由 affinity tuple 指定，支持更多核；本仓库主线仍是 GICv2。

---

## 2. 目标与边界

**提供：** `smp_ipi_register` / `smp_ipi_send`；pending CAS；dispatch；arch 发送。

**不做：** 优先级 IPI；unregister；延迟队列（除 pending OR）；Linux generic IPI mux。

slot 上限 **`RENDEZVOS_SMP_IPI_MAX`（16）**；耗尽 `-E_REND_OVERFLOW`。

---

## 3. 分层与调用方

**TLB（x86）** — `arch_smp_flush_tlb_init` 全局一次 `smp_ipi_register`；remote 填 per-CPU msg 再 `smp_ipi_send`。

**compat** — 可注册短回调；handler 在 IRQ 上下文，末尾可能 `schedule`（用户态被打断时）——须极短、勿重入长逻辑。

发送前：`cpu_is_online`（看 `CPU_STATE`，适合稀疏 APIC）。

---

## 4. 数据结构与不变量

### 4.1 slot 与 per-CPU pending

```c
struct smp_ipi_slot { smp_ipi_fn_t fn; bool used; };
/* 全局 slots[RENDEZVOS_SMP_IPI_MAX]；无 unregister */
DEFINE_PER_CPU(atomic64_t, smp_ipi_pending);
```

不变量：

- `smp_ipi_send`：online → CAS OR pending bit → `arch_smp_ipi_send`；失败则 **清除该 bit**。
- dispatch：`atomic64_exchange(pending, 0)` 循环，对置位 slot 调 `fn()`（**无参**）。exchange 循环避免 handler 中途再 OR 丢 bit。

门铃怎么送到目标核（ICR / SGIR），见 §6.1。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/smp/ipi.c` | register / send / dispatch |
| `arch/*/smp/arch_smp_ipi.c` | ICR FIXED / SGIR |
| `arch_smp_tlb_flush.c` | x86_64 上目前唯一默认注册的软 IPI 消费者 |

---

## 6. 流程

### 6.1 门铃送到哪颗核（实现上的假设）

**x86_64：** `APIC_send_IPI(dest=cpu, FIXED, vector=0x30)`。这里的 `cpu` 直接当成 Local APIC id，也当成软件 `cpu_id`（编号不连续时须先用 `cpu_is_online` 排除空号）。若当前是 PIC 模式、无 LAPIC 可用，直接返回 `-E_RENDEZVOS`。

**aarch64：**

- 发给自己：`SGIR` 的 TARGET_SELF；
- 发给别人：TARGET_SPECIFIED，list = `(1 << cpu)`；
- 要求 `cpu < GIC_V2_NR_CPU_MAX`（**8**）；
- 假定 **GIC CPU interface 编号等于软件 `cpu_id`**。在 MPIDR Aff0 从 0 连续编号时恰好成立；若软件编号和 GIC 的 target 位对不齐，SGIR 会投递到错误的核。

### 6.2 注册与投递时间线

```text
每核 arch_start_core 路径：
  smp_ipi_init → arch_smp_ipi_init(dispatch)
    → register_irq_handler(IPI_VEC, …, NEED_EOI)  // 写全 CPU 槽

smp_ipi_register(&id, fn)   // 线性占 slot（通常 BSP 启一次）
smp_ipi_send(cpu, id)       // pending OR + 门铃触发

目标核：
  IRQ → trap_handler → dispatch → fn() → EOI（End Of Interrupt）→ 用户来源或 schedule
```

---

## 7. 公开 API

本篇涉及的接口分布在：软 IPI 协议——`smp_ipi_init` / `register` / `send`、`smp_ipi_fn_t` / `ipi_id_t`，以及 arch 门铃 `arch_smp_ipi_init` / `arch_smp_ipi_send`。说明改写自 `ipi.h` + `arch/*/smp.h` Doxygen（已与 `ipi.c`、`arch_smp_ipi.c` 核对）。

**本篇不涉及：** IRQ 向量 reserve / `trap_handler` → `25`；ICR / SGIR 字段细节 → `26`/`27`；x86 TLB 消息槽与握手 → `32`；`cpu_is_online` → `28`。

AP 启动拉核用的 INIT/SIPI **不是** soft IPI（共用发送原语而已）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 每核装门铃 | `init_interrupt` → 平台 IRQ iface → **`smp_ipi_init`**（→ `arch_smp_ipi_init` → `register_irq_handler(IPI_VEC, dispatch, NEED_EOI)`） |
| 注册消费者 | 通常全局一次：`smp_ipi_register(&id, fn)`（x86 TLB 在 `arch_smp_flush_tlb_init`） |
| 发送 | 填目标核 per-CPU 消息（若需要）→ **`smp_ipi_send(cpu, id)`** |
| 接收 | IRQ → dispatch：`exchange(pending,0)` 循环 → `fn()` → EOI |

`fn` 无参；上下文在发送方预先写好的槽里。无 unregister；槽上限 `RENDEZVOS_SMP_IPI_MAX`（16）。

### 7.2 协议 API

```c
typedef void (*smp_ipi_fn_t)(void);
error_t smp_ipi_register(ipi_id_t *ipi_id_out, smp_ipi_fn_t fn);
error_t smp_ipi_send(cpu_id_t cpu, ipi_id_t ipi_id);
void smp_ipi_init(void);
```

| 接口 | 说明 |
|------|------|
| `smp_ipi_init` | 每核；挂共享 `smp_ipi_dispatch`。 |
| `smp_ipi_register` | 线性占 slot。`-E_IN_PARAM` / 满则 `-E_REND_OVERFLOW`。 |
| `smp_ipi_send` | 要求 `cpu_is_online` + slot used；CAS OR pending bit → arch 门铃；**门铃失败则清除该 bit**。bit 已置仍会再触发门铃。 |

Dispatch：`atomic64_exchange(pending, 0)` 循环，避免 handler 中途再 OR 丢 bit。

#### 7.2.1 IPI 收发时序

```text
  发送方 CPU A                          目标核 CPU B
  ─────────────                         ─────────────
  ① 写 B 的 per-CPU 消息槽
     （TLB 篇：flush_va / flush_all）
  ② CAS OR B 的 smp_ipi_pending[bit]
  ③ arch_smp_ipi_send(B)
     │
     │  x86:  wrmsr ICR (dest=B, vec=0x30, Fixed)
     │  aarch64: write GICD_SGIR (target=1<<B, INTID=0)
     │
     └──────────────────────────────▶  ④ IRQ 触发（向量 0x30 / trap 64）
                                         trap_handler → smp_ipi_dispatch
                                         ⑤ exchange(pending, 0) 循环
                                            对置位 bit 调 fn()
                                            fn() 读 B 的消息槽执行
                                         ⑥ EOI（APIC / GIC）
                                         ⑦ 返回被打断的上下文
  ⑧ （仅 TLB shootdown）自旋等
     B 的 done_gen >= expect
```

要点：①②③ 在 A 上是有序的（先写参数再 OR bit 再触发门铃），但 B 看到的顺序靠 **pending bit 的 CAS** 保证——B 的 handler 看到 bit 置位时，消息槽已写好。门铃本身只是「通知有事件」，不携带参数。

### 7.3 arch 门铃

```c
void arch_smp_ipi_init(void (*handler)(struct trap_frame *));
error_t arch_smp_ipi_send(cpu_id_t cpu);
```

| | x86_64 | aarch64 |
|--|--------|---------|
| 向量 / INTID | IDT **`0x30`** | SGI **0** → trap **64** |
| send | ICR FIXED；非 APIC → `-E_RENDEZVOS` | SGIR self / specified；`cpu≥8` → `-E_IN_PARAM` |
| 假设 | `cpu` = APIC id | GIC CPU IF 编号 == 逻辑 id |

### 7.4 与 TLB 的分工

唯一 in-tree 默认注册者：**x86** `arch_smp_flush_tlb_init`（见 `32`）。aarch64 TLB 走 `tlbi *is`，不注册 TLB IPI；各 ISA 仍 `smp_ipi_init`（机制保留，几乎无第二消费者）。

---

## 8. 多架构

门铃不同；逻辑协议相同。TLB 是否走 IPI：**仅 x86**。与 APIC / GIC 篇交叉：本篇不重复 ICR / SGIR 字段表，只写清「soft IPI 用哪一种投递」。

---

## 9. 测试

间接：x86 SMP + 用户 map / unmap。

---

## 10. 限制与后续

- 无 unregister；16 slot。
- aarch64 ≤8 核 target 假设。
- handler 须简短；可能触发 schedule。
- 与 TLB 篇分工：本篇门铃；TLB 篇 mask / 握手 / `*is`。

---

## 11. 变更记录

- 2026-10-04：§1.1.1/1.1.2 补 x86 LAPIC ICR（xAPIC ICR0/ICR1、x2APIC IA32_X2APIC_ICR、Delivery Mode、vector 0x30）与 aarch64 GIC SGI（GICD_SGIR 字段、INTID 0–15、GICv2 ≤8、GICv3 ICC_SGI1R_EL1）硬件说明；§7.4.1 加 IPI 收发时序图（写消息槽→OR pending→敲门→handler exchange→EOI）。
- 2026-10-04：§4 只留 slot / pending 不变量；门铃送到哪颗核迁入 §6.1。
- 2026-09-27：中文措辞整理——Bring-up→AP 启动拉核；affinity/稀疏拓扑句通顺化；弱化「真源 / 钉死」堆砌。
- 2026-09-26：§7 全文审阅——`ipi.h` / arch 门铃 Doxygen；纠正 `ipi_id_t` 签名；写清 pending 失败回滚与 TLB 分工。
- 2026-09-25：语言整理；§1.1 门铃 vs 工作拆分；硬件发送假设对照 ICR / SGIR。
- 2026-08-29：整篇重做——门铃叙述；0x30 / SGI0→64；pending 协议；仅 x86 TLB 注册；纠正「aarch64 TLB via IPI」；GIC≤8。
- 2026-08-27：初稿。
- 2026-10-05：任务 1/3/5 精读——补全 ICR/IDT/SGI/INTID/LAPIC/EOI 缩写首现释义；「打一扇门铃/敲门/清回/装不下/打到错误的核/registrant/handler 短」等口语与英文混用改正式中文；修正 §7.4.1 编号错位为 §7.2.1。
- 2026-10-05：最终词句顺畅。
