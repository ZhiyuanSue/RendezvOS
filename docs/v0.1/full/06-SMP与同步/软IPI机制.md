# 软 IPI 机制

v0.1 · 2026-08-29

本篇覆盖：`kernel/smp/ipi.c`、`include/rendezvos/smp/ipi.h`、`arch/{x86_64,aarch64}/smp/arch_smp_ipi.c`。

向量 reserve 见 IRQ 篇；门铃硬件（ICR / SGI）见 APIC / GIC 篇；**x86 TLB shootdown 握手**见 `TLB_shootdown与跨核一致性.md`（及 MM 的 TLB 策略篇）。本篇不把 aarch64 TLB 写成「也走 IPI」。

---

## 1. 概述

硬件只打一扇门铃；真正工作用 slot 表 + 每核 pending 位图。

| | x86 | aarch64 |
|--|-----|---------|
| 门铃 | LAPIC ICR FIXED，向量 **`0x30`** | GICD SGI **0** → trap id **64** |
| 注册 | `register_irq_handler(ARCH_IRQ_VEC_IPI, …)` | 同左（宏叠 offset） |

Bring-up 的 INIT/SIPI **不是** soft IPI（delivery mode 不同，只是共用 `APIC_send_IPI` 一类原语）。

**唯一 in-tree 默认消费者：x86 TLB shootdown。** aarch64 TLB 用 `tlbi *is`，**不**注册 TLB IPI，也 **无** `arch_smp_flush_tlb_init`。两端都会 `smp_ipi_init`——机制在，aarch64 几乎无第二消费者。

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
- dispatch：`atomic64_exchange(pending, 0)` 循环，对置位 slot 调 `fn()`（**无参**；上下文在 per-CPU 消息槽）。exchange 循环避免 handler 中途再 OR 丢 bit。

**aarch64 发送假设：** GIC target list 用 **`(1<<cpu)`**，且 `cpu < GIC_V2_NR_CPU_MAX`（**8**）；假定 **GIC CPU IF 编号 == 逻辑 cpu_id**。稠密 Aff0 时碰巧成立；affinity 稀疏会打错核。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/smp/ipi.c` | register / send / dispatch |
| `arch_smp_ipi.c` | ICR / SGI |
| `arch_smp_tlb_flush.c` | x86 唯一默认 registrant |

---

## 6. 流程

```text
smp_ipi_init → arch_smp_ipi_init(dispatch)
  → register_irq_handler(IPI_VEC, …, NEED_EOI)  // 写全 CPU 槽

smp_ipi_register(&id, fn)   // 线性占 slot
smp_ipi_send(cpu, id)       // pending OR + 门铃

IRQ → trap_handler → dispatch → fn() → EOI → 或 schedule
```

x86 dest = APIC id ≡ 逻辑下标。aarch64 self 用 TARGET_SELF；remote 用 target list。

---

## 7. 公开 API

```c
void smp_ipi_init(void);
error_t smp_ipi_register(u32 *id_out, smp_ipi_fn_t fn);
error_t smp_ipi_send(cpu_id_t cpu, u32 id);
```

---

## 8. 多架构

门铃不同；逻辑协议相同。TLB 是否走 IPI：**仅 x86**。

---

## 9. 测试

间接：x86 SMP + 用户 map/unmap。本篇未复测。

---

## 10. 限制与后续

- 无 unregister；16 slot。  
- aarch64 ≤8 核 target 假设。  
- handler 短；可能 schedule。  
- 与 TLB 篇分工：本篇门铃；TLB 篇 mask/握手/`*is`。

---

## 11. 变更记录

- 2026-08-29：整篇重做——门铃叙述；0x30/SGI0→64；pending 协议；仅 x86 TLB 注册；纠正「aarch64 TLB via IPI」；GIC≤8。
- 2026-08-27：初稿。
