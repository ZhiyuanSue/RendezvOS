# 软 IPI 机制

v0.1 · 2026-09-27

本篇覆盖：`kernel/smp/ipi.c`、`include/rendezvos/smp/ipi.h`、`arch/{x86_64,aarch64}/smp/arch_smp_ipi.c`。

IPI 所用 `local_irq_id` 与 `register_irq_handler` 见 IRQ 篇（`25`）；门铃硬件（ICR / SGI）见 APIC / GIC 篇（`26` / `27`）；**x86 TLB shootdown 握手**见 `32`，策略与 mask 见 TLB 篇（`11`）。本篇不把 aarch64 TLB 写成「也走 IPI」。

**官方手册：** Intel SDM — ICR（Interrupt Command Register，中断命令寄存器）FIXED 投递、向量进 IDT（Interrupt Descriptor Table，中断描述符表）；ARM GICv2 IHI0048 — `GICD_SGIR`、SGI（Software Generated Interrupt，软件生成中断）INTID 0–15。

---

## 1. 概述

硬件只发送一次门铃信号；真正工作用 **slot 表 + 每核 pending 位图**。

| | x86 | aarch64 |
|--|-----|---------|
| 门铃 | LAPIC（Local APIC）ICR **FIXED**，向量 **`0x30`** | GICD **SGI 0**，对应 `local_irq_id` **64** |
| 注册 | `register_irq_handler(ARCH_IRQ_ID_IPI, this_cpu, …, NEED_EOI)` | 同左（aarch64 上该宏 = INTID + 64） |

AP 启动拉核用的 INIT / SIPI **不是** soft IPI（delivery mode 不同，只是共用 `APIC_send_IPI` 一类原语）——见 SMP 篇（`28`）。

**唯一 in-tree 默认消费者：x86 TLB shootdown。** aarch64 TLB 用 `tlbi *is`，**不**注册 TLB IPI，也 **无** `arch_smp_flush_tlb_init`。现行主线各 ISA 仍都会 `smp_ipi_init`——机制保留，aarch64 几乎无第二消费者。

### 1.1 OS 为何拆「门铃」和「工作」

硬件一次中断只有一个向量 / INTID（Interrupt ID），无法承载「刷哪一页、哪个 ASID」这类参数。协议是：

1. 发送方先把参数写进 **目标核的 per-CPU 消息槽**（`32`）；
2. 再在目标核 `smp_ipi_pending` 上 OR 对应 slot bit；
3. 触发门铃（ICR / SGIR）；
4. 目标核 IRQ handler **exchange** 清 pending，按 bit 调已注册的 `fn()`（无参——上下文在消息槽里）。

门铃可合并（同一 bit 已置则不必再 OR）；发送失败则 **清除该 bit**，避免悬空 pending。

#### 1.1.1 x86 LAPIC ICR 要点

x86 的 IPI 通过 Local APIC 的 **ICR（Interrupt Command Register）**发出，本仓库用 xAPIC / x2APIC 两种映射。代码入口 `APIC_send_IPI()`（`arch/x86_64/PIC/LocalAPIC.c`）**先在本地拼出一个完整 64 位值**（目的号置于 `[63:56]`，即 `dest_field << 56`），再按当前模式落盘：

- **xAPIC（MMIO）**：APIC 寄存器窗口在物理 `0xFEE00000`，代码用「寄存器索引 × 0x10」寻址，ICR 索引 `0x30` → `ICR0` 在偏移 `0x300`（低 32 位：vector `[7:0]`、delivery mode `[10:8]`、dest mode / trigger / level / **Delivery Status = bit 12**），`ICR1` 在 `0x310`（高 32 位，目的 APIC id 在其 `[31:24]`）。发送前**自旋等 `ICR0` 的 bit 12（Delivery Status）清零**——代码注释：DS 仍置位说明上一条 IPI 还压在目标核，此时写 ICR 会被忽略；随后**先写 `ICR1`（高），再写 `ICR0`（低），对低半的写才真正触发发送**。
- **x2APIC（MSR）**：MSR 地址 = `0x800 + 寄存器索引`（`x2APIC_MSR_BASE` 宏），ICR → **`0x830`**，与 SDM 的 `IA32_X2APIC_ICR` 一致。目的号就在 64 位值的 `[63:56]`，一次 `wrmsr` 即发；代码注释明确 x2APIC 没有 ICR 高寄存器，**无需回读等 DS**。头文件还定义了 x2APIC 专属的 `SELF_IPI`（索引 `0x3F`，MSR `0x83F`），但本仓库发送路径不用它（给自己发也走 ICR）。
- **Delivery Mode**：本仓库只用 **Fixed（000b）**——按 vector 投递到目标 CPU 的 IDT。其他模式（SMI/NMI/INIT/SIPI/LowestPri/ExtINT）不用在 soft IPI 路径。INIT/SIPI 仅在 AP 启动拉核时用，delivery mode 不同，**不**走 `smp_ipi_send`。
- **Vector**：固定 `0x30`（IDT 项 48）。所有 soft IPI 共享这一个向量，区分靠 pending bit。
- **其余字段**（`arch_smp_ipi_send` 显式传入）：dest mode **Physical**（按 APIC id，本仓库假设 `cpu_id == APIC id`）、trigger **EDGE**、level **ASSERT**、destination shorthand **NO**（不用 all-inclusive 等广播简写，逐核单发）。

#### 1.1.2 aarch64 GIC SGI 要点

aarch64 的 IPI 走 GIC Distributor 的 **SGI（Software Generated Interrupt）**：

- **`GICD_SGIR`（偏移 `0xF00`，GICv2，只写）**：32 位寄存器，`[3:0]` 是 SGI INTID（0–15），`[23:16]` 是 CPU target list（每位对应一个 CPU interface），`[25:24]` 是 target filter。写即触发。
- **target filter 编码**（`gic_v2.h` 的 `GIC_V2_GICD_SGIR_TARGET_*`）：`00` = **TARGET_SPECIFIED**（按 target list 逐位投递）、`01` = **TARGET_OTHER**（除写者外所有核，本仓库未用）、`10` = **TARGET_SELF**（只发自己，忽略 target list）。代码里 `arch_smp_ipi_send` **给自己发时走 TARGET_SELF**，给别人发时 `target_list = (1 << cpu_id) << 16` 配 TARGET_SPECIFIED。
- **SGI INTID**：0–15，本仓库用 **0**（`GIC_SGI_START`）；软件侧 `local_irq_id` = INTID + 64（`AARCH64_IRQ_OFFSET`），故 SGI0 → **64**，落在 IRQ 篇 `[64,79]` 的 SGI 段。
- **CPU interface 编号**：GICv2 上限 **8** 个 CPU interface（`GIC_V2_NR_CPU_MAX`），故 `cpu < 8` 才能发，否则 `-E_IN_PARAM`。本仓库假设 GIC CPU interface 编号 == 软件 `cpu_id`（MPIDR Aff0 连续编号时成立）。
- **前置检查**：发送还要 `gic.send_sgi` 已绑定（GIC 驱动 ops，签名 `void (*)(u32 irq_num, u32 target_mode, u32 target_list_bit)`），未绑定返回 `-E_RENDEZVOS`。
- **GICv3** 用 `ICC_SGI1R_EL1`（64 位系统寄存器），target 由 affinity tuple 指定，支持更多核；本仓库主线仍是 GICv2。

---

## 2. 目标与边界

**提供：** `smp_ipi_register` / `smp_ipi_send`；pending CAS；dispatch；arch 发送。

**不做：** 优先级 IPI；unregister；延迟队列（除 pending OR）；Linux generic IPI mux。

slot 上限 **`RENDEZVOS_SMP_IPI_MAX`（16）**；耗尽 `-E_REND_OVERFLOW`。

---

## 3. 分层与调用方

**TLB（x86）** — `arch_smp_flush_tlb_init` 全局一次 `smp_ipi_register`；remote 填 per-CPU msg 再 `smp_ipi_send`。

**兼容层** — 可注册短回调；handler 在 IRQ 上下文，末尾可能 `schedule`（用户态被打断时）——须极短、勿重入长逻辑。

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

- `smp_ipi_send`：先校验 slot 在范围内且 `used`，再 `cpu_is_online`（两者违例都是 `-E_IN_PARAM`）；然后 CAS-OR 置 pending bit，再 `arch_smp_ipi_send`；门铃失败则 **清除该 bit** 回滚。bit 已置位时 CAS 循环空转退出，但门铃**仍会再敲一次**（多发一次中断无害，漏发才有害）。
- dispatch：`atomic64_exchange(pending, 0)` 外层循环，每轮对置位 slot 调 `fn()`（**无参**）。exchange 一把取走全部快照再处理，handler 执行中途新 OR 进来的 bit 留到下一轮，不会丢。`trap_frame *tf` 参数被显式弃用（`(void)tf`），pending 直接以 `(volatile u64 *)&percpu(smp_ipi_pending).counter` 裸指针参与原子运算。

原子原语的底层形态（`arch/{x86_64,aarch64}/atomic.c`）——这是「先写消息槽、再置 bit」能被目标核看见的依据：

| 原语 | x86_64 | aarch64 |
|------|--------|---------|
| `atomic64_exchange` | `lock xchgq`（单指令，自带全屏障） | `ldaxr/stlxr` 重试环，前后各一条 `dmb(ISH)`（未用 LSE `swp`） |
| `atomic64_cas` | `lock; cmpxchgq` | `ldaxr` 比较 + `stlxr` 提交，前后 `dmb(ISH)` |

两者都是**全序屏障**（且带 `"memory"` clobber），因此发送方「① 写参数 → ② CAS 置 bit」天然有序；目标核经 exchange 看到 bit 置位后读消息槽，必能看到 ① 的写入。

两条已核实的并发边界条件（现网无害，扩展时注意）：

1. **`smp_ipi_register` 无锁**：`smp_ipi_next++` 与填槽不加锁，依赖「bring-up 期单点调用」约定（x86 TLB 用 `smp_tlb_ipi_ready` 位保证全局只占一个槽）。启动完成后若两个核并发注册新消费者，需要调用方自行串行化。
2. **回滚只清自己的 bit**：若 bit 是**另一个发送方**先置的且门铃已在途，本发送方 arch 失败后的回滚可能把对方刚置的 bit 一并抹掉，令对方的工作丢失。当前唯一消费者不可达此路径（x86 TLB 发送前先查 `arch_irq_type`，aarch64 仅在 GIC 未绑定时失败），暂无实害。

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

**x86_64：** `arch_smp_ipi_send` 入口先检查 `arch_irq_type` 必须是 xAPIC / x2APIC，否则（PIC 遗留模式，无 LAPIC）直接返回 `-E_RENDEZVOS`；再调 `APIC_send_IPI(dest=cpu, SH_NO, EDGE, ASSERT, PHYSICAL, FIXED, vector=0x30)`。这里的 `cpu` 直接当成 Local APIC id，也当成软件 `cpu_id`（编号不连续时须先用 `cpu_is_online` 排除空号）。`APIC_send_IPI` 内部对非法 irq type 还有一道静默 return——两处检查冗余但一致（§1.1.1 的 DS 等待与先高后低写序见彼处）。

**aarch64：** 检查顺序为 **先越界后绑定**：

- `cpu >= GIC_V2_NR_CPU_MAX`（**8**）→ `-E_IN_PARAM`；
- `gic.send_sgi` 未绑定 → `-E_RENDEZVOS`；
- 发给自己：TARGET_SELF（不用 target list）；
- 发给别人：TARGET_SPECIFIED，list = `(1 << cpu)`；
- 假定 **GIC CPU interface 编号等于软件 `cpu_id`**。在 MPIDR Aff0 从 0 连续编号时恰好成立；若软件编号和 GIC 的 target 位对不齐，SGIR 会投递到错误的核。

### 6.2 注册与投递时间线

```text
每核 arch_start_core 路径：
  smp_ipi_init → arch_smp_ipi_init(dispatch)
    → register_irq_handler(ARCH_IRQ_ID_IPI, this_cpu, …, NEED_EOI)

smp_ipi_register(&id, fn)   // 线性占 slot（通常 BSP 启一次）
smp_ipi_send(cpu, id)       // pending OR + 门铃触发

目标核：
  IRQ → trap_handler → dispatch → fn() → EOI（End Of Interrupt）→ 用户来源或 schedule
```

---

## 7. 公开 API

接口分布在：软 IPI 协议——`smp_ipi_init` / `register` / `send`、`smp_ipi_fn_t` / `ipi_id_t`，以及 arch 门铃 `arch_smp_ipi_init` / `arch_smp_ipi_send`。说明改写自 `ipi.h` + `arch/*/smp.h` Doxygen（已与 `ipi.c`、`arch_smp_ipi.c` 核对）。

**本篇不涉及：** IRQ 两 id / `trap_handler`（见 `25`）；ICR / SGIR 字段细节（见 `26`/`27`）；x86 TLB 消息槽与握手（见 `32`）；`cpu_is_online`（见 `28`）。

AP 启动拉核用的 INIT/SIPI **不是** soft IPI（共用发送原语而已）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 每核装门铃 | `init_interrupt` 与平台 IRQ iface 之后 **`smp_ipi_init`**（内部 `arch_smp_ipi_init`，再 `register_irq_handler(ARCH_IRQ_ID_IPI, percpu(cpu_number), dispatch, IRQ_NEED_EOI)`）。两架构都在 `arch_start_core` 内调用：x86 在 `init_interrupt`/`init_irq` 之后，aarch64 在 `gic.init_cpu_interface()` 之后。注册用的本核号取自 per-CPU `cpu_number`，故其必须先于本步写入（`arch_start_core` 开头即 `per_cpu(cpu_number, cpu_id) = cpu_id`，见 `29` §7.4） |
| 注册消费者 | 通常全局一次：`smp_ipi_register(&id, fn)`（x86 TLB 在 `arch_smp_flush_tlb_init`） |
| 发送 | 填好目标核 per-CPU 消息（若需要）后调 **`smp_ipi_send(cpu, id)`** |
| 接收 | IRQ 进 dispatch：`exchange(pending,0)` 循环调 `fn()`，最后 EOI |

`fn` 无参；上下文在发送方预先写好的槽里。无 unregister；槽上限 `RENDEZVOS_SMP_IPI_MAX`（16）。

### 7.2 协议 API

```c
typedef u32 ipi_id_t;
#define IPI_ID_INVALID ((ipi_id_t)U32_MAX) /* 哨兵：供调用方表示「未拿到 slot」，core 内暂无使用者 */
typedef void (*smp_ipi_fn_t)(void);
error_t smp_ipi_register(ipi_id_t *ipi_id_out, smp_ipi_fn_t fn);
error_t smp_ipi_send(cpu_id_t cpu, ipi_id_t ipi_id);
void smp_ipi_init(void);
```

| 接口 | 说明 |
|------|------|
| `smp_ipi_init` | 每核；挂共享 `smp_ipi_dispatch`。 |
| `smp_ipi_register` | 线性占 slot（无锁，见 §4.1 边界条件）。出参 id ∈ `[0, 16)`；参数为 NULL → `-E_IN_PARAM`；满 → `-E_REND_OVERFLOW`。 |
| `smp_ipi_send` | 依序校验：slot 在范围内且 `used` → `cpu_is_online`（违例分别 `-E_IN_PARAM`；arch 层还可能回传 `-E_RENDEZVOS` / `-E_IN_PARAM`，见 §7.3）。CAS OR pending bit 后敲 arch 门铃；**门铃失败则清除该 bit**。bit 已置仍会再触发门铃。 |

Dispatch：`atomic64_exchange(pending, 0)` 循环，避免 handler 中途再 OR 丢 bit。

#### 7.2.1 IPI 收发时序

```text
  发送方 CPU A                          目标核 CPU B
  ─────────────                         ─────────────
  ① 写 B 的 per-CPU 消息槽
     （`32`：flush_va / flush_all）
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

要点：①②③ 在 A 上并非「天然」有序——跨核可见的顺序靠 **pending bit 原子操作的屏障语义**保证：x86 的 `lock cmpxchg` 与 aarch64 的 `ldaxr/stlxr + dmb(ISH)` 都是全屏障（§4.1 表），① 的参数写不会重排到 ② 的置位之后；B 侧 exchange 取到 bit 后（同为全屏障）再读消息槽，必看到 ① 的结果。门铃本身只是「通知有事件」，不携带参数，其硬件投递本身也不保证先于数据到达——协议里数据的「到达」始终以 bit 可见为准。若第 ③ 步失败，A 回滚 ② 的 bit；对 TLB 这类自带握手的消费者，发送失败即放弃本轮、由发起方处理（`32` 篇）。

### 7.3 arch 门铃

```c
void arch_smp_ipi_init(void (*handler)(struct trap_frame *));
error_t arch_smp_ipi_send(cpu_id_t cpu);
```

| | x86_64 | aarch64 |
|--|--------|---------|
| 向量 / INTID | IDT **`0x30`** | SGI **0**，对应 trap **64** |
| send | ICR FIXED（含 DS 等待与先高后低写序，§1.1.1）；非 APIC 模式返回 `-E_RENDEZVOS` | SGIR self / specified；`cpu≥8` 返回 `-E_IN_PARAM`，`gic.send_sgi` 未绑定返回 `-E_RENDEZVOS` |
| 假设 | `cpu` = APIC id | GIC CPU IF 编号 == 逻辑 id |

### 7.4 与 TLB 的分工

唯一 in-tree 默认注册者：**x86** `arch_smp_flush_tlb_init`（见 `32`）。aarch64 TLB 走 `tlbi *is`，不注册 TLB IPI；各 ISA 仍 `smp_ipi_init`（机制保留，几乎无第二消费者）。

---

## 8. 多架构

门铃不同；逻辑协议相同。TLB 是否走 IPI：**仅 x86**。与 APIC / GIC 篇交叉：本篇不重复 ICR / SGIR 字段表，只写清「soft IPI 用哪一种投递」。

---

## 9. 测试

间接：x86 SMP + 用户 map / unmap 走 TLB shootdown（`32` 篇），顺带压满「写槽 → OR bit → 门铃 → dispatch」全链。全仓（core 与 `linux_layer`/`servers`/`tests`）grep `smp_ipi_register`/`smp_ipi_send`，消费者仅此一处，故 soft IPI 机制的正确性目前完全由该路径背书。aarch64 无 in-tree 消费者，IPI 路径只有「每核注册成功、启动不 panic」这一层间接覆盖；若兼容层将来引入第二消费者（如跨核事件通知），应补多核并发注册与发送的专项测例（另见 §4.1 无锁注册的约定）。

---

## 10. 限制与后续

- 无 unregister；16 slot（slot 只增不减，`smp_ipi_next` 单调）。
- aarch64 ≤8 核 target 假设（GICv2 CPU interface 上限）。
- `smp_ipi_register` 无锁、失败回滚可能抹他人 bit——两条边界条件与「现网为何无害」见 §4.1，新增消费者时须复核。
- handler 须简短；可能触发 schedule。
- 分工：本篇门铃；shootdown 握手在 `32`，mask / `*is` 在 TLB 篇（`11`）。

---

## 11. 变更记录
- 2026-11-19：底子首评批次——§7 opener。事实沿用 11-14/15。
- 2026-11-15：Maintainer 复审前可读性通读。跨篇引用统一既定昵称与裸编号：门铃硬件句补 `26`/`27`，`32-TLB_shootdown与跨核一致性.md` 全称→`32`，「MM 的 TLB 策略篇」→「TLB 篇（`11`）」；「拓扑篇」→「SMP 篇（`28`）」（沿 `16`/`29` 先例）；篇内三处裸用「TLB 篇」原实指 `32`，与全仓既定昵称（`11`）撞名，改为显式 `32` / `11` 并按实际归属拆开分工句；§9 消费者句语病修正（「grep…仅此一处消费者」→「grep…，消费者仅此一处」）。`RENDEZVOS_SMP_IPI_MAX`=16u（`include/rendezvos/limits.h`）、`IPI_ID_INVALID` 无 .c 使用者、唯一消费者 `arch_smp_tlb_flush.c` 复核属实；事实层沿用 11-14 核验，未再改动。

- 2026-11-14：全文对照 `ipi.c`/`ipi.h`/两 arch `arch_smp_ipi.c`/`LocalAPIC.c`/`gic_v2.h`/两 arch `atomic.c` 精读——§1.1.1 补 `APIC_send_IPI` 真实路径（目的号 `[63:56]` 拼装、xAPIC 等 Delivery Status bit12 清零、**先写 ICR1 高半再写 ICR0 低半触发**、x2APIC MSR=`0x800+索引`→`0x830` 免回读、`SELF_IPI` 定义未用、SH_NO/EDGE/ASSERT/PHYSICAL 实参）；§1.1.2 补 SGIR target filter 三值编码（SELF/SPECIFIED/OTHER）、发己走 TARGET_SELF、`gic.send_sgi` ops 签名与未绑定 `-E_RENDEZVOS`；§4.1 新增原子原语底层表（`lock xchgq/cmpxchg` vs `ldaxr/stlxr+dmb(ISH)`，非 LSE）作为跨核可见性依据，并记录两条并发边界条件（register 无锁靠 bring-up 约定；回滚可能抹他人 bit，现网不可达）；§6.1 补两 arch 检查顺序与双重保险；§7.1 明确门铃注册取号依赖 `cpu_number` 先写入；§7.2 补 `ipi_id_t`/`IPI_ID_INVALID` 与错误码顺序；§9 记录全仓消费者核实；§10 增补约定条目；变更记录 2026-10-05 两条归位。
- 2026-10-22：与两 id 模型对齐——「向量 reserve / trap id」改为 `local_irq_id` 与 `25` 交叉引用。
- 2026-10-05：任务 1/3/5 精读——补全 ICR/IDT/SGI/INTID/LAPIC/EOI 缩写首现释义；「打一扇门铃/敲门/清回/装不下/打到错误的核/registrant/handler 短」等口语与英文混用改正式中文；修正 §7.4.1 编号错位为 §7.2.1。
- 2026-10-05：最终词句顺畅。
- 2026-10-04：§1.1.1/1.1.2 补 x86 LAPIC ICR（xAPIC ICR0/ICR1、x2APIC IA32_X2APIC_ICR、Delivery Mode、vector 0x30）与 aarch64 GIC SGI（GICD_SGIR 字段、INTID 0–15、GICv2 ≤8、GICv3 ICC_SGI1R_EL1）硬件说明；§7.4.1 加 IPI 收发时序图（写消息槽→OR pending→敲门→handler exchange→EOI）。
- 2026-10-04：§4 只留 slot / pending 不变量；门铃送到哪颗核迁入 §6.1。
- 2026-09-27：中文措辞整理——Bring-up→AP 启动拉核；affinity/稀疏拓扑句通顺化；弱化「真源」等生硬堆砌用语。
- 2026-09-26：§7 全文审阅——`ipi.h` / arch 门铃 Doxygen；纠正 `ipi_id_t` 签名；写清 pending 失败回滚与 TLB 分工。
- 2026-09-25：语言整理；§1.1 门铃 vs 工作拆分；硬件发送假设对照 ICR / SGIR。
- 2026-08-29：整篇重做——门铃叙述；0x30 / SGI0→64；pending 协议；仅 x86 TLB 注册；纠正「aarch64 TLB via IPI」；GIC≤8。
- 2026-08-27：初稿。
