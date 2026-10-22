# IRQ 向量分配与处理

v0.1 · 2026-10-22

本篇覆盖：`kernel/trap/trap.c`（`local_irq_vector` / `global_irq_vector`、`irq_global_*`、`register_irq_handler`、`trap_handler`）、`include/rendezvos/trap/trap.h`、各架构 map 钩子（`arch/*/trap/trap.h` / `trap.c`）、timer / IPI 注册衔接（点到为止）。

Trap class / fixed / IDT·VBAR 入口见 `23-Trap抽象与分类.md`；APIC / PIC、GIC 寄存器与 EOI 细节见平台两篇；timer 语义见 `07-时间与定时器`；软 IPI 协议见 `06-SMP与同步/30-软IPI机制.md`。

官方手册（本篇只写清「软件两套 id 如何映射到硬件号」；寄存器细节归平台篇）：

- **Intel SDM Vol.3：** IDT、向量 0–255、Local APIC 投递的向量号。
- **ARM GIC Architecture Specification（GICv2，IHI0048）与 ARM ARM：** INTID、GICC_IAR / EOIR；CPU 侧只有 VBAR IRQ 入口。

---

## 1. 概述

硬件中断不会自动变成 C 函数调用。CPU 侧永远是：

1. 控制器（8259 / LAPIC / GIC）选出一个**号**；
2. 架构入口把这个号写进 `trap_frame`——软件侧叫 **`local_irq_id`**；
3. `trap_handler` 看这个号：小于 `NR_ARCH_LOCAL_IRQ`（即 `ARCH_IRQ_ID_ALLOC_LO`，下文 **LO**）时，查**本核** `local_irq_vector[]`；大于等于 LO 时，本核表里没有这一格，要先经架构 map 找到共享的 `global_irq_vector[]`，再调 handler；
4. 若属性要求，再做 **EOI**（告诉硬件「这条中断处理完了」）。

core 的贡献是第 3 步的**两套软件 id**：

- **`local_irq_id`**：每核同一套数字。小于 LO 的，本核 `local_irq_vector[]` 里有对应格子，编号就是下标；大于等于 LO 的没有格子，必须 map 到全局表。
- **`global_irq_id`**：`global_irq_vector[]` 的下标。x86 上是 GSI；aarch64 上是 SPI 下标（`INTID - SPI_START`）。

全局表每一项在软件里只绑一对 `(local_irq_id, cpu_id)`。要换核，先 `irq_global_free` 再 `irq_global_alloc`。`register_irq_handler` 必须带具体 `cpu_id`：本核表内直接写该核的槽；表外须已 `irq_global_alloc`。这不是 Linux `request_irq`，也不是「随意取一个数字写 handler」。

### 1.1 硬件「号」与软件 id

| | x86_64 | aarch64 |
|--|--------|---------|
| 硬件交付物 | **IDT 向量**（0–255） | **GIC INTID**（0–1019…），CPU 只看见 VBAR 的 IRQ 槽 |
| `local_irq_id` | **= 向量号** | **`intid + 64`**（前 64 槽留给 ESR.EC）；LO 以下含 EC、SGI、PPI |
| `global_irq_id` | GSI 下标（长度暂由启动时申请决定） | SPI 下标 = `INTID - SPI_START` |
| 谁写出这个号 | PIC 基址 / LAPIC LVT·SVR·ICR /（将来）IOAPIC RTE | `GICC_IAR` 读出 INTID；SGI 由 `GICD_SGIR` 注入 |
| 表外编号怎么进全局表 | 每核 `local_irq_map[]`（CAS；**同号在不同核可映不同 GSI**） | 公式一一对应（无每核 map；**同号各核必映同一 SPI**） |

OS 必须保证：**写进硬件的向量 / INTID，在软件侧已经有表项并挂了 handler**（本核表范围内可直接 `register_irq_handler`；表外须先 `irq_global_alloc`）。否则进 `unknown` panic。

### 1.2 为何本核表是 per-CPU 的

`DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ])`：每核一份本核表。表外编号再经本核 map（x86）或公式（aarch64）落到共享的 `global_irq_vector[]`。

硬件原因：SMP 上同一外设可能被路由到不同核（或 IPI 投递到指定核）；每核都有自己的 IDT / VBAR / GICC，入口汇编跑在**被中断的那颗核**上，查的是**本核**表（及本核 map）。若只在 BSP 上挂 handler、AP 上同一编号空着，AP 收到中断就会 panic。

`register_irq_handler(local_irq_id, cpu_id, …)` 只写**那一个** `cpu_id` 上的槽（或已 alloc 到该核的全局项）。v0.1 **没有**「同一全局中断在每核挂不同 ISR」；绑核靠软件记的 `cpu_id` + 硬件路由（GIC ITARGETSR / 将来 IOAPIC），不是靠在每核换一套函数指针。

两套数组的关系（与 `arch/*/trap/trap.h` 注释一致）：

**x86_64**（同表外编号，不同核可指向不同 `g[i]`）：

```
CPU0  local_irq_vector[]（本核私有，有格子）   ≥LO（本核表无此格）
      +-----+-----+-----+-----+                +-----+-------+-----+
      |  0  |  1  | ... |LO-1 |                | LO  | LO+1  | ... |
      +-----+-----+-----+-----+                +--+--+--+----+-----+
                                                  |     |
                                                  |     +-----> （未映射）
                                                  v
CPU1  local_irq_vector[]（本核私有，有格子）   ≥LO
      +-----+-----+-----+-----+                +-----+-------+-----+
      |  0  |  1  | ... |LO-1 |                | LO  | LO+1  | ... |
      +-----+-----+-----+-----+                +--+--+--+----+-----+
                                                  |
                                                  v
                 global_irq_vector[]（全机共享）
                 +------+------+-----+------+-----+-------+
                 | g[0] | g[1] | ... | g[j] | ... | g[k]  | ...
                 +------+------+-----+--+---+-----+--+----+
                                       ^             ^
                                       |             |
                            CPU0 的 LO +             |
                            CPU1 的 LO --------------+
```

**aarch64**（同表外编号，各核一律映到同一 `g[i]`）：

```
CPU0  local_irq_vector[]（本核私有，有格子）   ≥LO（本核表无此格）
      +-----+-----+-----+-----+                +-----+-------+-----+
      |  0  |  1  | ... |LO-1 |                | LO  | LO+1  | ... |
      +-----+-----+-----+-----+                +--+--+--+----+-----+
                                                  |     |
                                                  v     v
CPU1  local_irq_vector[]（本核私有，有格子） +-----+-------+-----+
      +-----+-----+-----+-----+                | LO  | LO+1  | ... |
      |  0  |  1  | ... |LO-1 |                +--+--+--+----+-----+
      +-----+-----+-----+-----+                   |     |
                                                  v     v
                 global_irq_vector[]（全机共享）
                 +------+------+------+-----+
                 | g[0] | g[1] | g[2] | ... |
                 +--+---+--+---+------+-----+
                    ^      ^
                    |      |
           各核的 LO       各核的 LO+1     （公式一一对应）
```

`LO = ARCH_IRQ_ID_ALLOC_LO`；`g[i] = global_irq_vector[i]`。

### 1.3 EOI 与 schedule

ACK：带 `IRQ_NEED_EOI` 时调 `arch_eoi_irq`（x86 写 LAPIC EOI 或 PIC OCW2；aarch64 写 `GICC_EOIR`）。漏 EOI 时，同优先级 / level 触发的线可能**再也收不到中断**（时序与共享线约束见 §6.2）。

用户态被打断且 `core_tm` 就绪时，`trap_handler` 末尾会 **`schedule`**（x86 `syscall` 旁路除外——见 syscall 篇）。ISR 应短、非阻塞；长任务应通过 IPC 交给线程（无 threaded IRQ）。

### 1.4 软件占表 ≠ 硬件打开外设线

`irq_global_alloc` / `register_irq_handler` **只**动软件表。外设线还要在控制器里打开：

- aarch64 SPI：`gic.unmask_irq(INTID)`（可选再 `set_affinity`）；
- x86 外设：IOAPIC 路由（v0.1 尚未实现）。

本核本地中断（异常、IPI、timer 的 LVT / PPI 等）一般在每核启动时已经使能，不走这条外设 unmask。

---

## 2. 目标与边界

**提供：** 两套 id（`local_irq_id` / `global_irq_id`）；`irq_global_vector_alloc` / `irq_global_alloc` / `irq_global_free` / `irq_global_find`；按 `(local_irq_id, cpu_id)` 注册；EOI attr；用户来源 schedule 钩子。

**不做：** MSI / `request_irq` 框架；IOAPIC / GIC 路由编程全文（平台篇）；把 EC / fault 当「设备 IRQ」讲（Trap 篇）。

---

## 3. 分层与调用方

| 角色 | 做什么 |
|------|--------|
| `arch_start_platform` | BSP 上 `irq_global_vector_alloc`（须已有 `kallocator`；长度由平台决定） |
| `init_interrupt` | 要求全局数组已就绪；再 `arch_init_interrupt`（装 IDT / 设 VBAR） |
| Timer | `rendezvos_time_init`：取 `arch_get_timer_irq_num` 后 `register_irq_handler(…, this_cpu, IRQ_NEED_EOI)`（x86=`0x20`；aarch64=DTB ns-phys） |
| IPI | `arch_smp_ipi_init`：`register_irq_handler(ARCH_IRQ_ID_IPI, this_cpu, …)` |
| Fault / SVC | `arch_register_fixed_trap` 内部再调 `register_irq_handler`（带每个匹配 id 与每个 `cpu_id`） |
| 设备 | **先 `irq_global_alloc`，再 `register_irq_handler(..., IRQ_NEED_EOI)`，然后平台 unmask / 路由** |

---

## 4. 数据结构与不变量

### 4.1 表项

```c
struct local_irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;              /* 普通写；每核一份 */
};
DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

struct global_irq {                /* 定义在各 arch 头；字段可多可少 */
        void (*irq_handler)(struct trap_frame *tf);
        atomic64_t irq_attr;       /* 申请时 CAS 置 IRQ_ATTR_USED */
        size_t local_irq_id;
        cpu_id_t cpu_id;
        /* x86：ioapic_id、pin（占位） */
};
extern struct global_irq *global_irq_vector;
extern size_t nr_global_irq;
```

属性位：`IRQ_NEED_EOI`、`IRQ_ATTR_USED`（见 `trap_common.h`）。`irq_attr_*` / `irq_atomic_attr_*` 见 `trap.h`。

### 4.2 编号空间（硬件贴合）

| | x86_64 | aarch64 |
|--|--------|---------|
| 本核表长度 | `NR_ARCH_LOCAL_IRQ` = LO（`0x40`） | 同宏（EC+SGI+PPI 上界） |
| IDT / 硬件全宽 | IDT **256**（SDM：向量 0–255）；表外号仍可作 IDT 向量，但不进 `local_irq_vector[]` | CPU **没有** 256 项向量表；GIC 用 INTID |
| 身份 | 向量号 = `local_irq_id` | `[0,64)` = EC；`[64,…)` = intid+64 |
| 表外窗口 | `[ALLOC_LO, ALLOC_HI]` = `[0x40, 0xEF]` | 同；固定表外号 = INTID + 偏移 |
| 全局数组长度 | 启动时申请（暂定 `NR_ARCH_GLOBAL_IRQ`，待 MADT） | `gic.nr_spi()` |

x86 上 Intel 保留 **0–31** 给架构异常。设备 / timer / IPI 必须落在 ≥32 的向量上；timer / spurious / IPI 用固定宏，设备走表外窗口再 map 进全局表。

aarch64 上为了复用同一套「按编号分发」外形，把 EC 放入前 64 槽、IRQ 整体平移 +64——Trap 篇说的「假统一、真映射」。SGI+PPI 落在本核表内；SPI 是表外号，经公式映到 `global_irq_vector[]`。

### 4.3 本核固定槽、表外窗口与空洞

读布局时，把编号分成三类即可：

**① 本核表内的固定槽（结构性，无需旧式 reserve）**

异常 / EC、timer、IPI，以及 aarch64 上整段 SGI+PPI，都会打到「被中断的那颗核」。入口汇编查的是本核的 `local_irq_vector[]`，所以这些编号落在 `[0, LO)`，直接 `register_irq_handler(id, this_cpu, …)` 即可。handler 可以晚装（timer / IPI），也可以故意不装（x86 spurious：误入则 unknown，随后 panic）。

**② 表外窗口 → `global_irq_vector[]`**

外设 SPI / 将来 IOAPIC·MSI 用大于等于 LO 的 `local_irq_id`。软件上先 `irq_global_alloc(global_irq_id, cpu_id, &local)`（CAS 占全局项，并在目标核上建立 map），再 `register_irq_handler`。全局表全机一份；亲和写在表项的 `(local_irq_id, cpu_id)` 上。

**③ 空洞（本核表长度内、又未被固定宏占用的空隙）**

例如 x86 上 timer / spurious / IPI 周围那些空隙（如 `0x21–0x26`）。仍落在本核表长度内，但勿手写进 LVT 或路由——留给以后固定用途，或故意留空。

```text
x86_64 local_irq_id（= IDT 向量）布局示意：

  0          31   32       0x20 0x27 0x30        0x40              0xEF  0xF0      0xFF
  ├─架构异常─┤  空洞    ──┤timer│spur│IPI│  空洞   ├─ 表外 → global_irq_vector ─┤ 空洞 │
  └─本核表内─┘            │      │    │          └─ LO..HI（设备）──────┘        │
                          └─本核表内固定槽───────┘                  └─未使用────┘

  256 项 IDT 全宽；向量 0–15 不可作 APIC 投递目标（APIC 合法范围 16–255）。

aarch64 local_irq_id 布局示意：

  0 ……… 63   64 ……… 95   96 ………………… （SPI 上界）
  └─本核 EC─┘  └本核 SGI+PPI─┘  └─ 表外 → global_irq_vector ─┘
              (intid 0–31)      (intid 32+；公式映 SPI 下标)
```

#### 各 ISA 实际占用的区间

**x86（对齐 IDT / LAPIC 固定编程）：**

| 区间 / 槽 | 用途 | 硬件谁写出这个向量 |
|-----------|------|------------------|
| `[0, 31]` | 架构异常 | IDT gate；不是 LAPIC LVT |
| `0x20`（`ARCH_IRQ_ID_TIMER`） | Timer | PIC：master IRQ0 基址；APIC：**LVT Timer** 的向量字段 |
| `0x27`（`ARCH_IRQ_ID_SPURIOUS`） | Spurious | **SVR** 的 spurious 字段；当前无专用 handler，误入可 panic |
| `0x30`（`ARCH_IRQ_ID_IPI`） | IPI | **ICR** FIXED 投递 |
| `[0x40, 0xEF]`（`ALLOC_LO..HI`） | 表外设备号 | 将来 IOAPIC / MSI；经 per-CPU map 进 `global_irq_vector` |

中间空洞（如 `0x21–0x26`）既不在表外窗口、也未被固定宏占用——勿写进 LVT。

**aarch64（对齐 GIC INTID 分段）：**

| 区间 | 用途 | 硬件 |
|------|------|------|
| `[0, 63]` | EC / sync | ESR.EC，不是 GIC |
| **64–95** | SGI+PPI（intid 0–31；含 IPI0、timer PPI） | INTID 固定，落在本核表内。timer 的具体 id 仍由 `arch_get_timer_irq_num` 解析，但槽位已经落在这一段里 |
| **≥96**（`ALLOC_LO` 起） | SPI（intid 32+） | Distributor 上可配的外设线；公式映 `global_irq_id` |

和 §1.2 一起看：本核表是 per-CPU 的，固定槽在每核各自 register；设备走全局表 + 亲和，不是旧式「全 CPU 同 handler」。

### 4.4 注册不变量

- `register_irq_handler` / `irq_global_alloc` / `irq_global_free`：**必须给具体 `cpu_id`**（不能用 `CPU_ID_INVALID`）。
- map / find：`CPU_ID_INVALID` 表示当前 CPU。
- 本核表内：写该 `cpu_id` 上的 `local_irq_vector[local_irq_id]`（实现会 `attr | USED`）。
- 全局：须已 `irq_global_alloc`，且表项里的 `cpu_id` / `local_irq_id` 对得上，否则 find 失败；register 打 error log 后 return。
- 允许 `handler == NULL` 清函数指针；`irq_global_free` 会清 handler，并**最后**清 `IRQ_ATTR_USED`。
- `register_irq_handler` **不是**「同一全局中断在每核挂不同 ISR」的框架——全局项只有一对亲和。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/trap/trap.c` | `irq_global_*`、`register_irq_handler`、`trap_handler`、`init_interrupt` |
| `include/rendezvos/trap/trap.h` | 公共接口与属性辅助 |
| `arch/*/trap/trap.h` | `struct global_irq`、区间宏、id 图 |
| `arch/*/trap/trap.c` | `arch_map_*`、`arch_irq_global_entry_reset`、`arch_register_fixed_trap`、`arch_eoi_irq` |
| `arch/*/boot/start_arch.c` | 调 `irq_global_vector_alloc` |
| `kernel/time/time.c` | timer register |
| `arch/*/smp` | IPI register |
| 平台 APIC / GIC | 硬件路由与 EOI 实现 |

---

## 6. 流程

### 6.1 Boot（软件表必须先于「能收到中断」）

1. BSP 的 `arch_start_platform`：申请 `global_irq_vector`（此时须已有内核分配器；长度 x86 暂定常量、aarch64 跟 SPI 条数走）。
2. 每核 `init_interrupt`：确认全局数组已就绪（否则 panic）→ `arch_init_interrupt`（lidt / VBAR；aarch64 路径上可能立刻 enable IRQ）。
3. 平台 IRQ 接口（x86 `init_irq` / aarch64 `gic.init_cpu_interface`）→ `smp_ipi_init` → …… → `rendezvos_time_init`（register timer；x86 路径上才 `software_enable_APIC`——SVR 软件使能）。

```text
arch_start_platform          // BSP：irq_global_vector_alloc
…
init_interrupt
  → 检查 global_irq_vector
  → arch_init_interrupt      // lidt / VBAR（+aarch64 可能立刻 enable IRQ）
→ 平台 IRQ 接口（x86 init_irq / aarch64 gic.init_cpu_interface）
→ smp_ipi_init               // register IPI
→ … → rendezvos_time_init    // register timer；x86 路径上才 software_enable_APIC
```

x86：先 `lidt`、再选 PIC/APIC、再 `sti`（且常在 `init_syscall` 之后）。aarch64：**先开 IRQ 再配 GIC CPU interface**——中间有一小段窗口，见 GIC 篇。

### 6.2 `trap_handler`（设备 / IRQ 路径）与 EOI

```text
local_irq_id = TRAP_ID(tf->trap_info)
→ 本核表内：读 percpu(local_irq_vector[id])
  表外：irq_global_find(id, CPU_ID_INVALID)
→ handler 或 unknown+panic
→ NEED_EOI → arch_eoi_irq      // 硬件 ACK
→ !from_kernel && core_tm → schedule
```

#### EOI 时序（漏 EOI 会卡住后续中断）

```text
  正常路径（IRQ_NEED_EOI=1）：

   IRQ 到达 ─► IRR 置位 ─► CPU accept ─► ISR 置位 ─► ISR 执行
                                              │
                                              └─ trap_handler 末尾
                                                 arch_eoi_irq ─► EOI 寄存器写 0
                                                 └─► ISR 位清零
                                                     └─► 同级/同向量可再来

  漏 EOI 路径（IRQ_NEED_EOI=0 但硬件需要 EOI）：

   IRQ 到达 ─► ISR 置位 ─► ISR 执行 ─► 返回（未 EOI）
                                    │
                                    └─ ISR 位仍置位
                                       └─► 同优先级中断被屏蔽
                                       └─► 该线不再触发（level）
                                               或同向量不重投（edge 亦可能）
```

#### IRQ 共享与 EOI 时序（同向量多设备）

x86 上多个设备可共享同一 IDT 向量（典型 PCI legacy 共享线）。共享时 ISR 必须轮询每个挂在该向量的设备：

```text
  共享向量 V 上挂了设备 A、B、C（都 register 到同一 local_irq_id）：

   IRQ 到达 ─► IDT[V] ─► trap_handler ─► 调共享 handler
                                       │
                                       ├─ 查设备 A 状态寄存器
                                       │   └─ active？处理 A，清 A 中断源
                                       ├─ 查设备 B 状态寄存器
                                       │   └─ active？处理 B，清 B 中断源
                                       └─ 查设备 C 状态寄存器
                                           └─ active？处理 C，清 C 中断源
                                       │
                                       └─ 全部查完 ─► EOI（一次，对控制器）
```

**关键：** level 触发时，EOI 之前必须先清除所有挂在该线的设备中断源，否则 EOI 后线仍有效，会立刻再进同一向量（风暴）。本仓库 v0.1 **没有** IRQ 共享框架（`register_irq_handler` 同 id 后写覆盖，不挂链），所以共享场景属于限制项——见 §10。

### 6.3 驱动 checklist（硬件依赖顺序）

1. `irq_global_alloc(global_irq_id, cpu_id, &local_irq_id);` — **勿**手写 timer / IPI / EC 当设备号。
2. `register_irq_handler(local_irq_id, cpu_id, isr, IRQ_NEED_EOI);` — 软件表就绪。
3. **再**硬件路由 / unmask：
   - x86：向量 = `local_irq_id`（IOAPIC / MSI 见 APIC 篇；v0.1 IOAPIC 空壳，外设线基本无法接上）。
   - aarch64：`intid = AARCH64_TRAP_ID_TO_IRQ(local_irq_id)`，再 `gic.unmask_irq` / `set_affinity`。
4. 未 alloc 就 register 全局号：error log 后 return。
5. ISR 返回后由 `trap_handler` 做 EOI——handler 内**不要**自行写 EOI，除非明确绕过 `IRQ_NEED_EOI`。

---

## 7. 公开 API

本篇涉及的接口分布在：两套 id 与分发——`struct local_irq` / `struct global_irq`、`irq_global_*`、`register_irq_handler`、`trap_handler`、`init_interrupt`、`arch_map_*` / `arch_init_interrupt`、`arch_eoi_irq`、`arch_unknown_trap_handler`。说明改写自 `trap.h` / `arch/*/trap/trap.h` Doxygen（已与 `kernel/trap/trap.c`、`arch/*/trap/trap.c` 核对）。

**本篇不涉及：** `arch_register_fixed_trap` / `trap_class`（见 `23`）；weak `syscall`（见 `24`）；APIC / PIC / GIC 寄存器与路由（见 `26` / `27`）；timer / IPI 业务语义（见时间篇 / `30`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| BSP 一次 | `arch_start_platform` 里 **`irq_global_vector_alloc`**（须已有 `kallocator`） |
| 每核装表 | `arch_start_core` 调 **`init_interrupt`**（检查全局数组 → `arch_init_interrupt`），然后平台 IRQ iface、`smp_ipi_init`，……再到 `rendezvos_time_init` |
| 设备驱动 | **先 `irq_global_alloc`，再 `register_irq_handler(local, cpu_id, isr, IRQ_NEED_EOI)`，然后**硬件 unmask / 路由 |
| Fault / SVC | `arch_register_fixed_trap`（内部再 `register_irq_handler`；本核表内编号） |
| Timer / IPI | 本核表内固定号，直接 `register_irq_handler(ARCH_IRQ_ID_*, this_cpu, …)` |

未 alloc 就 register 全局号：error log 后 return。ISR 内勿自写 EOI（除非故意不设 `IRQ_NEED_EOI`）。

### 7.2 表项与属性

```c
struct local_irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;  /* IRQ_NEED_EOI | IRQ_ATTR_USED */
};
DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);
```

| 符号 | 说明 |
|------|------|
| `IRQ_ATTR_USED` | 全局项已占用（alloc）；本核 register 时也会置 used |
| `IRQ_NEED_EOI` | handler 返回后 `trap_handler` 调 `arch_eoi_irq` |
| `irq_attr_*` / `irq_atomic_attr_*` | get/set USED、need_eoi；全局项用 atomic 版 |

### 7.3 全局申请 / 占用 / 释放

```c
error_t irq_global_vector_alloc(size_t count);
error_t irq_global_alloc(size_t global_irq_id, cpu_id_t cpu_id,
                         size_t *local_irq_id_out);
error_t irq_global_free(size_t global_irq_id, cpu_id_t cpu_id);
struct global_irq *irq_global_find(size_t local_irq_id, cpu_id_t cpu_id);
```

| 接口 | 说明 |
|------|------|
| `irq_global_vector_alloc` | 全机一次；表项初值为无效 id / 未占用。`count==0` → `-E_IN_PARAM`；已分配或分配器失败 → `-E_RENDEZVOS`。 |
| `irq_global_alloc` | CAS 置占用；aarch64 由表外号固定算出 local，x86 扫描 `[ALLOC_LO, ALLOC_HI]` 空闲号。map 失败回滚 → `-E_REND_AGAIN`；窗口用尽 → `-E_REND_OVERFLOW`；参数非法 → `-E_IN_PARAM`。 |
| `irq_global_free` | 清 map → 复位架构私有字段 → 清 handler → **最后**清占用位。亲和不符或未占用 → `-E_REND_NOFOUND`。 |
| `irq_global_find` | map 后再核占用 / local / cpu_id；`CPU_ID_INVALID` = 当前 CPU。 |

### 7.4 注册与分发

```c
void init_interrupt(void);
void register_irq_handler(size_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *), u64 irq_attr);
void trap_handler(struct trap_frame *tf);  /* 汇编入口；声明在 trap.h */
void arch_eoi_irq(u64 trap_info);
void arch_unknown_trap_handler(struct trap_frame *tf);
```

| 接口 | 说明 |
|------|------|
| `init_interrupt` | 全局数组未就绪则 panic；再 `arch_init_interrupt`。 |
| `register_irq_handler` | 本核表内直接写该 `cpu_id`；全局须已 alloc 且亲和匹配。`handler==NULL` 可清指针。 |
| `trap_handler` | 按 `TRAP_ID` 查本核表或 `irq_global_find`；无则 unknown+panic；若 NEED_EOI 则 EOI；用户来源且 `core_tm` 就绪则 `schedule`。 |
| `arch_eoi_irq` | 平台 ACK（细节见 `26`/`27`）。 |
| `arch_unknown_trap_handler` | 打印帧；随后 `trap_handler` panic。 |

驱动 checklist 见 §6.3。x86 `syscall` **不**经本路径。

---

## 8. 多架构

上表编号空间 / 表外 map / INTID 映射见 §1.1、§4.2–§4.3。差异再收三条：

| | x86_64 | aarch64 |
|--|--------|---------|
| 表外 map | 每核一份；同 LO 可异 GSI | 公式；同 LO 必同 SPI |
| alloc 时的 local | 扫描空闲表外号 | 由 `global_irq_id` 固定算出 |
| 打开外设线 | IOAPIC 未实现 | `gic.unmask_irq` |

控制器细节见 APIC / GIC 篇。riscv / loongarch 非 v0.1 主线。

---

## 9. 测试

间接：timer、IPI、SMP。无单独「全局占用耗尽 / 亲和错误」测试用例。

---

## 10. 限制与后续

- `irq_global_alloc`：忙或 map 竞态 → `-E_REND_AGAIN`；表外窗口用尽 → `-E_REND_OVERFLOW`；参数非法 → `-E_IN_PARAM`。
- x86 全局数组长度仍是启动时的暂定值；待 MADT 给出真实 GSI 数量。IOAPIC 路由未齐时，alloc 出的表外号无处可编程。
- Spurious 无专用 handler。
- 无「同一 id 挂多设备」的共享链（后写覆盖）；level 共享场景见 §6.2。
- 无「同一全局中断每核不同 handler」。
- 无 portable `irq_set_affinity`（旧笔记 backlog）；GIC 有内部 `set_affinity`，x86 无。绑核语义见 GIC 篇。

---

## 11. 变更记录

- 2026-10-22：对齐两套 id 模型——`local_irq_vector` / `global_irq_vector`、`irq_global_*`、按 `cpu_id` 注册；补本核表↔全局表图；§4.3 / §6 / §7 由旧 reserve/pool 叙述改写为现行语义。**保留** EOI / 共享时序图、编号空间布局、驱动 checklist 与既有变更记录条目。
- 2026-10-07：§1 概述瘦身——EOI 时序图与 IRQ 共享图迁入 §6.2，与 `trap_handler` 流程合并；§1.3 只留概念句并回指。
- 2026-10-04：补 EOI 时序图（正常 vs 漏 EOI）与 IRQ 共享 + EOI 时序图；扩 x86 向量空间布局示意（标注 256 项全宽、APIC 合法范围 16–255、空洞位置）；「塞进/本篇拥有」改地道中文（放入/本篇涉及的接口分布在）。
- 2026-10-04：§4.3 改题为「固定槽、设备池与空洞」；`arch_init_irq_vector_state` 调用步骤迁入 §6.1。
- 2026-10-01：§4.3 按中文阅读习惯重写（保留布局示意）；同步扩写代码侧 Doxygen / 注释。
- 2026-10-01：§4.3 扩写——`arch_init_irq_vector_state` 的每核 reserve / 全局 pool / 空洞三类槽位与布局示意；澄清为何不用 boot 期 `reserve_for_all_cpus`。
- 2026-09-27：中文措辞整理——弱化「真源」等生硬堆砌用语；补 `software_enable_APIC` 的「软件使能」对照。
- 2026-09-26：§7 全文审阅——补 `trap_handler` 声明与 Doxygen；强化 alloc/free/register 全 CPU 语义与「先软件后路由」编排序；划清 vs `23`/`24`/`26`/`27`。
- 2026-09-25：扩写硬件「号」与软件 trap id 贴合；per-CPU 表为何、EOI 硬件含义；驱动 checklist 强调先软件后路由；手册入口。
- 2026-08-29：整篇重做——各 ISA reserve 实表；NR_IRQ / EC+offset；free / USED 陷阱。
- 2026-08-27：初稿。
- 2026-10-05：任务 1/3/5 精读——「测例」改「测试用例」；口语词修正（贴上→映射到、自己变成→自动变成、挑出→选出、随便拿个数字→随意取一个数字、撞车→冲突、再也不来→再也收不到中断、长活 IPC→长任务应通过 IPC、再也进不来→不再触发、就够了→即可、占坑→占位（3 处）、拿来当→用作、替别人标表→替其他核标记表项、只干两件事→只做两件事、接不上→无法接上、自己乱写 EOI→自行写 EOI）。
- 2026-10-05：最终词句顺畅。
