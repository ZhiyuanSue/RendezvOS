# IRQ 向量分配与处理

v0.1 · 2026-09-27

本篇覆盖：`kernel/trap/trap.c`（reserve / pool / alloc / `register_irq_handler` / `trap_handler`）、`include/rendezvos/trap/trap.h`、`arch_init_irq_vector_state`（各 arch `trap.c`）、timer / IPI 注册衔接（点到为止）。

Trap class / fixed / IDT·VBAR 入口见 `23-Trap抽象与分类.md`；APIC / PIC、GIC 寄存器与 EOI 细节见平台两篇；timer 语义见 `07-时间与定时器`；软 IPI 协议见 `06-SMP与同步/30-软IPI机制.md`。

官方手册（本篇只写清「软件命名空间如何贴上硬件号」；寄存器细节归平台篇）：

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
| Timer | `rendezvos_time_init`：`arch_get_timer_irq_num` → `register_irq_handler(…, IRQ_NEED_EOI)`（x86=`0x20`；aarch64=DTB ns-phys） |
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

### 4.3 `arch_init_irq_vector_state`：向量怎么排、谁占坑

每个核在 `init_interrupt` 里都会调用一次。函数本身很短，只干两件事：

1. 对本核调用若干次 `irq_vector_reserve_range_for_cpu(me, …)`：在**本核**的 `irq_vector[]` 上把固定区间标成 `USED`，**不**挂 handler。
2. 调用一次 `irq_vector_set_alloc_pool(lo, hi)`：公布设备可分配的闭区间。这个窗口是**全局一份**的，不是 per-CPU；各核 boot 时会写同一对 lo/hi，重复写无妨。

boot 路径**不会**用 `irq_vector_reserve_range_for_all_cpus`。原因很简单：BSP 起来时 AP 往往还没起来，没法替别人标表；等每个核自己跑到 `init_interrupt`，自然会把自己那份固定槽占好。`reserve_for_all_cpus` 留给「所有核都已 online 之后」才需要一次性全局占坑的场景。

读布局时，把槽位分成三类就够了：

**① 每核都要有的固定槽（本函数 reserve）**  
异常 / EC、timer、IPI，以及 aarch64 上整段 SGI+PPI，都会打到「被中断的那颗核」。入口汇编查的是本核表，所以每个核都必须先把这些 id 标成 USED，否则该核上 `register_irq_handler` 会拒装，或者中断来了直接 unknown panic。  
注意：「每个 CPU 都需要」≠ boot 时调用 `reserve_for_all_cpus`。正确做法是：每核自己 reserve 同一组 id；之后某次 `register_irq_handler(ARCH_IRQ_VEC_*, …)` 再把**同一份** handler 写到所有核。

**② 全局设备池（`set_alloc_pool` + 日后 `alloc`）**  
外设 SPI / 将来 IOAPIC·MSI 可能被路由到任意核，所以软件 trap id 必须在所有核上同一套。`irq_vector_alloc` 要求「全 CPU 都未 USED」才算空闲，成功后再**全 CPU** 置 USED。

**③ 空洞（既没 reserve，也不在 pool 里）**  
例如 x86 上 timer / spurious / IPI 周围那些空隙。既不能 `alloc`，也不该手写进 LVT 或路由——留给以后固定用途，或故意留空。

「保留」在这里的含义是：reserve 只占坑（USED），handler 可以晚装（timer / IPI），也可以故意不装（x86 spurious：误入则 unknown，随后 panic）。

```text
x86_64 trap id（= IDT 向量）布局示意：

  [0 ………… 31]  [32…]     0x20   …  0x27  …  0x30  …  [0x40 …… 0xEF]  [0xF0…]
   └─ 每核 reserve ─┘  空洞   timer  空洞 spur  空洞 IPI 空洞 └─ 全局 alloc pool ─┘  空洞
   (TRAP_ARCH_USED)

aarch64 trap id 布局示意：

  [0 ……… 63]  [64 ……… 95]  [96 ………………… 1083]
   └每核 EC─┘  └每核 SGI+PPI─┘  └─ 全局 SPI alloc pool ─┘
               (intid 0–31)      (intid 32–1019)
```

#### 本核 `me` 实际 reserve 的区间

**x86（对齐 IDT / LAPIC 固定编程）：**

| 区间 / 槽 | 用途 | 硬件谁写出这个向量 |
|-----------|------|------------------|
| `[0, 31]` | 架构异常（`TRAP_ARCH_USED` 是上界） | IDT gate；不是 LAPIC LVT |
| `0x20` | Timer | PIC：master IRQ0 基址；APIC：**LVT Timer** 的向量字段 |
| `0x27` | Spurious | **SVR** 的 spurious 字段；当前无专用 handler，误入可 panic |
| `0x30` | IPI | **ICR** FIXED 投递 |
| pool `[0x40, 0xEF]` | 设备 alloc | 将来 IOAPIC / MSI 编到这些向量 |

中间空洞（如 `0x21–0x26`）既不在 pool、也未 reserve——不可 alloc，也勿写进 LVT。

**aarch64（对齐 GIC INTID 分段）：**

| 区间 | 用途 | 硬件 |
|------|------|------|
| `[0, 63]` | EC / sync | ESR.EC，不是 GIC |
| trap **64–95** | SGI+PPI（intid 0–31；含 IPI0、timer PPI） | **整段** reserve：SGI/PPI 的 INTID 固定，不能当地设备池。timer 的具体 trap id 仍由 `arch_get_timer_irq_num` 解析，但槽位已落在本段内 |
| pool trap **96–1083** | SPI（intid 32–1019） | Distributor 上可配的外设线 |

和 §1.2 一起看：表是 per-CPU 的，所以固定槽必须**每核各自** reserve；handler 却是全局一份——`register_irq_handler` 负责后半句，本函数只负责前半句的「占坑」。

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
→ … → rendezvos_time_init        // register timer；x86 路径上才 software_enable_APIC（SVR 软件使能）
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

本篇拥有：软件 trap id 命名空间与分发——`struct irq` / `irq_vector[]`、`irq_vector_*`、`register_irq_handler`、`trap_handler`、`init_interrupt`、`arch_init_irq_vector_state` / `arch_init_interrupt`、`arch_eoi_irq`、`arch_unknown_trap_handler`。说明改写自 `trap.h` / `arch/*/trap/trap.h` Doxygen（已与 `kernel/trap/trap.c`、`arch/*/trap/trap.c` 核对）。

**本篇不拥有：** `register_fixed_trap` / `trap_class` → `23`；weak `syscall` → `24`；APIC / PIC / GIC 寄存器与路由 → `26` / `27`；timer / IPI 业务语义 → 时间篇 / `30`。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 每核装表 | `arch_start_core` → **`init_interrupt`** = `arch_init_irq_vector_state` → `arch_init_interrupt` → 平台 IRQ iface → `smp_ipi_init` → … → `rendezvos_time_init` |
| 设备驱动 | **`irq_vector_alloc(&id)`** → **`register_irq_handler(id, isr, IRQ_NEED_EOI)`** → **再**硬件 unmask / 路由 |
| Fault / SVC | `register_fixed_trap`（内部再 `register_irq_handler`；须已 reserve） |
| Timer / IPI | 槽已由 `arch_init_irq_vector_state` reserve → 直接 `register_irq_handler(ARCH_IRQ_VEC_*, …)` |

未 USED 就 register → error log + return。ISR 内勿自写 EOI（除非故意不设 `IRQ_NEED_EOI`）。

### 7.2 表项与属性

```c
struct irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;  /* IRQ_NEED_EOI | IRQ_VEC_USED */
};
DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ]);
```

| 符号 | 说明 |
|------|------|
| `IRQ_VEC_USED` | 槽已占用（reserve / alloc）；register 前置条件（查**本 CPU**） |
| `IRQ_NEED_EOI` | handler 返回后 `trap_handler` 调 `arch_eoi_irq` |
| `irq_attr_*` | 内联 get/set USED、need_eoi |

### 7.3 池与 reserve

```c
error_t irq_vector_reserve_range_for_cpu(cpu_id_t cpu, u32 lo, u32 hi);
error_t irq_vector_reserve_range_for_all_cpus(u32 lo, u32 hi);
void irq_vector_set_alloc_pool(u32 lo, u32 hi);
error_t irq_vector_alloc(u32 *trap_id_out);
error_t irq_vector_free(u32 trap_id);
```

| 接口 | 说明 |
|------|------|
| `reserve_*` | 只标 USED，不装 handler。boot 里用 **`for_cpu(me)`** 按 §4.3 占本核固定槽；`for_all_cpus` 不走 boot 路径。 |
| `set_alloc_pool` | 设置**全局**可分配闭区间 [lo, hi]；非法范围会被忽略。默认 lo=1、hi=0，此时 alloc 返回 `-E_IN_PARAM`。各核 boot 会重复写成同一窗口。 |
| `irq_vector_alloc` | 池内找「**全 CPU** 均未 USED」的 id，再全 CPU 置 USED。耗尽 → `-E_RENDEZVOS`。 |
| `irq_vector_free` | 仅池内；要求全 CPU USED，否则 `-E_RENDEZVOS`。**不清** `irq_handler`。 |

### 7.4 注册与分发

```c
void init_interrupt(void);
void register_irq_handler(int irq_num, void (*handler)(struct trap_frame *),
                          u64 irq_attr);
void trap_handler(struct trap_frame *tf);  /* 汇编入口；声明在 trap.h */
void arch_eoi_irq(u64 trap_info);
void arch_unknown_trap_handler(struct trap_frame *tf);
```

| 接口 | 说明 |
|------|------|
| `init_interrupt` | 固定序：`arch_init_irq_vector_state` → `arch_init_interrupt`。不清零已有全 CPU 表项。 |
| `register_irq_handler` | 本 CPU 须已 USED；写**全 CPU**同一 handler；`attr \| USED`。`handler==NULL` 可清指针。 |
| `trap_handler` | `TRAP_ID` → handler 或 unknown+panic → NEED_EOI → EOI → 用户来源且 `core_tm` 则 `schedule`。 |
| `arch_eoi_irq` | 平台 ACK（细节 → `26`/`27`）。 |
| `arch_unknown_trap_handler` | 打印帧；随后 `trap_handler` panic。 |

驱动 checklist 见 §6.3。x86 `syscall` **不**经本路径。

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
- 无 portable `irq_set_affinity`（旧笔记 backlog）；GIC 有内部 `set_affinity`，x86 无。绑核语义见 GIC 篇。

---

## 11. 变更记录

- 2026-10-01：§4.3 按中文阅读习惯重写（保留布局示意）；同步扩写代码侧 Doxygen / 注释。
- 2026-10-01：§4.3 扩写——`arch_init_irq_vector_state` 的每核 reserve / 全局 pool / 空洞三类槽位与布局示意；澄清为何不用 boot 期 `reserve_for_all_cpus`。
- 2026-09-27：中文措辞整理——弱化「钉 / 真源」堆砌；补 `software_enable_APIC` 的「软件使能」对照。
- 2026-09-26：§7 全文审阅——补 `trap_handler` 声明与 Doxygen；强化 alloc/free/register 全 CPU 语义与「先软件后路由」编排序；划清 vs `23`/`24`/`26`/`27`。
- 2026-09-25：扩写硬件「号」与软件 trap id 贴合；per-CPU 表为何、EOI 硬件含义；驱动 checklist 强调先软件后路由；手册入口。
- 2026-08-29：整篇重做——两侧 reserve 实表；NR_IRQ / EC+offset；free / USED 陷阱。
- 2026-08-27：初稿。
