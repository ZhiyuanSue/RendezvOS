# 软 IPI 机制

v0.1 · 2026-09-27

本篇覆盖：`kernel/smp/ipi.c`、`include/rendezvos/smp/ipi.h`、`arch/{x86_64,aarch64}/smp/arch_smp_ipi.c`。

向量 reserve 见 IRQ 篇；门铃硬件（ICR / SGI）见 APIC / GIC 篇；**x86 TLB shootdown 握手**见 `32-TLB_shootdown与跨核一致性.md`（及 MM 的 TLB 策略篇）。本篇不把 aarch64 TLB 写成「也走 IPI」。

**官方手册：** Intel SDM — ICR FIXED 投递、向量进 IDT；ARM GICv2 IHI0048 — `GICD_SGIR`、SGI INTID 0–15。

---

## 1. 概述

硬件只打一扇门铃；真正工作用 **slot 表 + 每核 pending 位图**。

| | x86 | aarch64 |
|--|-----|---------|
| 门铃 | LAPIC ICR **FIXED**，向量 **`0x30`** | GICD **SGI 0** → trap id **64** |
| 注册 | `register_irq_handler(ARCH_IRQ_VEC_IPI, …, NEED_EOI)` | 同左（宏叠 +64） |

AP 启动拉核用的 INIT / SIPI **不是** soft IPI（delivery mode 不同，只是共用 `APIC_send_IPI` 一类原语）——见拓扑篇。

**唯一 in-tree 默认消费者：x86 TLB shootdown。** aarch64 TLB 用 `tlbi *is`，**不**注册 TLB IPI，也 **无** `arch_smp_flush_tlb_init`。两端都会 `smp_ipi_init`——机制在，aarch64 几乎无第二消费者。

### 1.1 OS 为何拆「门铃」和「工作」

硬件一次中断只有一个向量 / INTID，装不下「刷哪一页、哪个 ASID」。协议是：

1. 发送方先把参数写进 **目标核的 per-CPU 消息槽**（TLB 篇）；
2. 再在目标核 `smp_ipi_pending` 上 OR 对应 slot bit；
3. 敲门（ICR / SGIR）；
4. 目标核 IRQ handler **exchange** 清 pending，按 bit 调已注册的 `fn()`（无参——上下文在消息槽里）。

门铃可合并（同一 bit 已置则不必再 OR）；发送失败则 **清回该 bit**，避免悬空 pending。

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

```c
struct smp_ipi_slot { smp_ipi_fn_t fn; bool used; };
/* 全局 slots[RENDEZVOS_SMP_IPI_MAX]；无 unregister */
DEFINE_PER_CPU(atomic64_t, smp_ipi_pending);
```

- `smp_ipi_send`：online → CAS OR pending bit → `arch_smp_ipi_send`；失败则 **清回该 bit**。
- dispatch：`atomic64_exchange(pending, 0)` 循环，对置位 slot 调 `fn()`（**无参**）。exchange 循环避免 handler 中途再 OR 丢 bit。

### 4.1 硬件发送假设

**x86：** `APIC_send_IPI(dest=cpu, FIXED, vector=0x30)`；`cpu` 即 APIC id ≡ 逻辑下标（稀疏拓扑下须 `cpu_is_online`）。PIC 模式直接 `-E_RENDEZVOS`（无 LAPIC 门铃）。

**aarch64：**

- self：`SGIR` TARGET_SELF；
- remote：TARGET_SPECIFIED，list = `(1<<cpu)`；
- 要求 `cpu < GIC_V2_NR_CPU_MAX`（**8**）；
- 假定 **GIC CPU IF 编号 == 逻辑 cpu_id**。在 Aff0 稠密编号时碰巧成立；若拓扑稀疏（逻辑 id 与 GIC target 位不对齐），SGIR 会打错核。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/smp/ipi.c` | register / send / dispatch |
| `arch/*/smp/arch_smp_ipi.c` | ICR FIXED / SGIR |
| `arch_smp_tlb_flush.c` | x86 唯一默认 registrant |

---

## 6. 流程

```text
每核 arch_start_core 路径：
  smp_ipi_init → arch_smp_ipi_init(dispatch)
    → register_irq_handler(IPI_VEC, …, NEED_EOI)  // 写全 CPU 槽

smp_ipi_register(&id, fn)   // 线性占 slot（通常 BSP 启一次）
smp_ipi_send(cpu, id)       // pending OR + 门铃

目标核：
  IRQ → trap_handler → dispatch → fn() → EOI → 用户来源或 schedule
```

---

## 7. 公开 API

本篇拥有：软 IPI 协议——`smp_ipi_init` / `register` / `send`、`smp_ipi_fn_t` / `ipi_id_t`，以及 arch 门铃 `arch_smp_ipi_init` / `arch_smp_ipi_send`。说明改写自 `ipi.h` + `arch/*/smp.h` Doxygen（已与 `ipi.c`、`arch_smp_ipi.c` 核对）。

**本篇不拥有：** IRQ 向量 reserve / `trap_handler` → `25`；ICR / SGIR 字段细节 → `26`/`27`；x86 TLB 消息槽与握手 → `32`；`cpu_is_online` → `28`。

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
| `smp_ipi_send` | 要求 `cpu_is_online` + slot used；CAS OR pending bit → arch 门铃；**门铃失败则清回该 bit**。bit 已置仍会再敲门。 |

Dispatch：`atomic64_exchange(pending, 0)` 循环，避免 handler 中途再 OR 丢 bit。

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

唯一 in-tree 默认 registrant：**x86** `arch_smp_flush_tlb_init`（见 `32`）。aarch64 TLB 走 `tlbi *is`，不注册 TLB IPI；两端仍 `smp_ipi_init`（机制在，几乎无第二消费者）。

---

## 8. 多架构

门铃不同；逻辑协议相同。TLB 是否走 IPI：**仅 x86**。与 APIC / GIC 篇交叉：本篇不重复 ICR / SGIR 字段表，只写清「soft IPI 用哪一种投递」。

---

## 9. 测试

间接：x86 SMP + 用户 map / unmap。本篇未复测。

---

## 10. 限制与后续

- 无 unregister；16 slot。
- aarch64 ≤8 核 target 假设。
- handler 短；可能 schedule。
- 与 TLB 篇分工：本篇门铃；TLB 篇 mask / 握手 / `*is`。

---

## 11. 变更记录

- 2026-09-27：中文措辞整理——Bring-up→AP 启动拉核；affinity/稀疏拓扑句通顺化；弱化「真源 / 钉死」堆砌。
- 2026-09-26：§7 全文审阅——`ipi.h` / arch 门铃 Doxygen；纠正 `ipi_id_t` 签名；写清 pending 失败回滚与 TLB 分工。
- 2026-09-25：语言整理；§1.1 门铃 vs 工作拆分；硬件发送假设对照 ICR / SGIR。
- 2026-08-29：整篇重做——门铃叙述；0x30 / SGI0→64；pending 协议；仅 x86 TLB 注册；纠正「aarch64 TLB via IPI」；GIC≤8。
- 2026-08-27：初稿。
