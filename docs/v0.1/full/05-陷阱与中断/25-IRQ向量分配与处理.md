# IRQ 向量分配与处理

v0.1 · 2026-10-23

本篇讲三件事怎么叠在一起，以及**各自管到哪里为止**：

| 一层 | 管什么 | 主要代码 | 本篇篇幅 |
|------|--------|----------|----------|
| **编号空间（irq_space）** | 两套软件编号、本核表 / 全局表、map、占有与释放 | `kernel/trap/irq_space.*`、`arch/*/trap/irq_space.*` | **主体** |
| **分发** | 按编号找到 handler、要不要 EOI、要不要 `schedule` | `kernel/trap/trap.c`、`trap.h` | 衔接说明 |
| **硬件控制器** | 选号、投递到哪颗核、屏蔽或打开线路、写 EOI 寄存器 | APIC / PIC、GIC 等 | **只对照编号**；寄存器细节见 `26` / `27` |

Trap 分类与装表见 `23`；系统调用旁路见 `24`；timer / 软 IPI 见时间篇与 `30`。

官方手册（本篇只用来对照「硬件交出来的号」长什么样）：

- **Intel SDM Vol.3：** IDT、向量 0–255、Local APIC 投递的向量号。
- **ARM GIC（GICv2，IHI0048）与 ARM ARM：** INTID、GICC_IAR / EOIR；CPU 侧只有 VBAR 的 IRQ 入口。

---

## 1. 概述

### 1.1 一条中断从硬件走到软件表

硬件中断不会自动变成 C 函数调用。整条链是：

1. **控制器**（8259 / LAPIC / GIC）选出一个硬件意义上的**号**，并决定打到哪颗核；
2. **架构入口**把这个号写进 `trap_frame`——落到软件里就叫 **`local_irq_id`**；
3. **`trap_handler`（分发）**看这个号：小于 `NR_ARCH_LOCAL_IRQ`（即 `ARCH_IRQ_ID_ALLOC_LO`，下文 **LO**）时，查**本核** `local_irq_vector[]`；大于等于 LO 时，本核表里没有这一格，要先经架构 map 找到共享的 `global_irq_vector[]`，再调 handler；
4. 若属性要求，再做 **EOI**（告诉控制器「这条处理完了」）——具体写哪个寄存器是平台的事。

**irq_space 只负责第 3 步里的「表」和「编号怎么对应」**：谁占了哪个 `global_irq_id`、映到哪个 `local_irq_id`、绑在哪颗核上。它**不**替你写 IOAPIC / GIC 的路由，也**不**替你打开外设线。分发在 `trap.c`；开线、改亲和、写 EOI 寄存器在平台篇。

两套软件编号：

- **`local_irq_id`**：每核同一套数字。小于 LO 的，本核 `local_irq_vector[]` 里有格子，编号就是下标；大于等于 LO 的没有格子，必须 map 到全局表。
- **`global_irq_id`**：`global_irq_vector[]` 的下标。x86 上对应 GSI；aarch64 上是 SPI 下标（`INTID - SPI_START`）。

全局表每一项在软件里只绑一对 `(local_irq_id, cpu_id)`。占有、换核、要不要再挂 handler，见 **§1.4**。这不是 Linux 的 `request_irq`，也不是「随便写个数字当中断号」。

### 1.2 硬件交出来的号，和软件 id 怎么对上

| | x86_64 | aarch64 |
|--|--------|---------|
| 硬件交付物 | **IDT 向量**（0–255） | **GIC INTID**（0–1019…），CPU 只看见 VBAR 的 IRQ 槽 |
| `local_irq_id` | **= 向量号** | **`intid + 64`**（前 64 槽留给 ESR.EC）；LO 以下含 EC、SGI、PPI |
| `global_irq_id` | GSI 下标（长度暂由启动时申请决定） | SPI 下标 = `INTID - SPI_START` |
| 谁写出这个号 | PIC 基址 / LAPIC LVT·SVR·ICR /（将来）IOAPIC RTE | `GICC_IAR` 读出 INTID；SGI 由 `GICD_SGIR` 注入 |
| 表外编号怎么进全局表 | 每核 `local_irq_map[]`（用原子操作更新；**同号在不同核可映不同 GSI**） | 公式一一对应（无每核 map；**同号各核必映同一 SPI**） |

OS 必须保证：**硬件还会交出来的那个号，在软件表里已经有着落**（本核表内可直接 `register_irq_handler`；表外须先 `irq_global_alloc`，并且通常还要挂好 handler——迁移时 handler 可能是上次留下的，见 §1.4）。否则进 `unknown` 再 panic。

### 1.3 为什么本核表是每核一份

`DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ])`：每核一份本核表。表外编号再经本核 map（x86）或公式（aarch64）落到共享的 `global_irq_vector[]`。

硬件原因：SMP 上同一外设可能被路由到不同核（或 IPI 投递到指定核）；每核都有自己的 IDT / VBAR / GICC，入口汇编跑在**被中断的那颗核**上，查的是**本核**表（及本核 map）。若只在 BSP 上挂 handler、AP 上同一编号空着，AP 收到中断就会 panic。

`register_irq_handler(local_irq_id, cpu_id, …)` 只写**那一个** `cpu_id` 上的槽（或已占到该核的全局项）。v0.1 **没有**「同一全局中断在每核挂不同处理函数」；绑核靠软件记的 `cpu_id`，再加上硬件路由（GIC 的目标寄存器 / 将来的 IOAPIC），不是靠在每核各挂一套函数指针。

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

### 1.4 谁可以动这条全局项（占有规则）

先把一件事说清楚：`IRQ_ATTR_USED` 上的原子比较交换 **只负责「抢还没人占的格子」**。两个核同时对同一条 `global_irq_id` 做 `irq_global_alloc` 时，谁抢成功谁占有。它**不是**引用计数，也管不住「别人已经 `irq_global_find` 拿到表项指针」之后你再改表——所以占有之后还要另定规矩。

**一句话：抢号可以并发；抢走之后，这条全局项上的软件改动只许属主那颗 CPU 做。**

| 阶段 | 谁可以做 | 说明 |
|------|----------|------|
| 还未占用 | 任意核 `irq_global_alloc(编号, 目标 cpu, …)` | 启动时可以替别的核抢号；换核时也可以由旧核替新核抢号 |
| 已经占用 | 仅属主：`irq_global_free`、全局路径的 `register_irq_handler` | 调用方必须跑在该 `cpu_id` 上，否则直接拒绝 |
| 分发 | 硬件打到哪颗核，哪颗核跑 `trap_handler` → `find(本核)` | 软件假定「路由目标」和表里记的亲和一致 |

**为什么释放时不清 handler，只放下「已占用」位？**

换核时，多半还是**同一个**设备处理函数，只是从 CPU A 换到 CPU B。若释放时把 `handler` 置空、再把整份属性写成 0，新核占上号之后还得再挂一遍函数，还容易把「需要 EOI」这类标志弄丢（再次占用时只会重新打上「已占用」）。所以释放时约定是：

- 清掉本核 map、亲和字段（`local_irq_id` / `cpu_id`）、架构私有字段；
- **保留** `irq_handler`；
- 属性只用 `irq_atomic_attr_try_set_unused` **清掉「已占用」**，留下「需要 EOI」等。

换核可以按这个顺序做：在属主上先挡住投递（屏蔽线路或改路由）→ `irq_global_free` →（可在旧核上）给新核 `irq_global_alloc` → 改硬件路由并重新打开。**中间不必再挂处理函数**，除非处理函数本身换了。

**什么时候才要改 handler？**

典型不是换核，而是驱动生命周期：卸掉模块再装一个新的，入口函数变了。那时在**当时的属主核**上再调 `register_irq_handler`（可以先传空指针摘掉，再释放 / 卸模块；或直接挂上新函数）。`register` 不禁止空指针，就是覆盖写。

**卸模块时务必注意：** 释放故意留下旧 handler。若模块代码已经卸掉、表项里却还挂着旧指针，下一次有人只占号、忘了重新挂函数，就会打进已经不存在的代码。正确做法是：仍是属主、并且已经挡住投递时，先 `register(..., NULL, ...)`（或先挂一个仍安全的空操作），再释放，再卸模块。

**同核上 irq_space 管不到的事：** 释放或改 handler 之前，调用方应先停掉投递，避免本核正在 `trap_handler` 里用着表项时表被拆掉。跨核靠「只有属主能释放 / 能改 handler」挡住；同核仍靠这条调用约定。

### 1.5 EOI 与调度（分发层，不是 irq_space）

带「需要 EOI」时，`trap_handler` 会调 `arch_eoi_irq`（x86 写 LAPIC EOI 或 PIC；aarch64 写 `GICC_EOIR`）。漏做 EOI 时，同优先级或电平触发的线可能再也进不来（时序与共享线见 §6.2）。

用户态被打断且调度器就绪时，`trap_handler` 末尾可能 `schedule`（x86 的 `syscall` 旁路除外）。处理函数应短、不要阻塞；长活应通过 IPC 交给线程。这些都不属于 irq_space 的职责。

### 1.6 软件占了表，不等于硬件已经打开外设线

`irq_global_alloc` / `register_irq_handler` **只**改软件表。外设线还要在控制器里打开或改路由：

- aarch64 的 SPI：`gic.unmask_irq(INTID)`（需要时再改亲和）；
- x86 外设：IOAPIC 等（v0.1 尚未接上）。

本核本地中断（异常、IPI、timer 的 LVT / PPI 等）一般在每核启动时已经使能，不走「先占全局表再打开外设线」这条路径。

---

## 2. 目标与边界

**本篇（irq_space + 与分发的衔接）提供：** 两套软件编号；全局表的申请 / 占用 / 释放 / 查找；按 `(local_irq_id, cpu_id)` 挂 handler；属性里的「需要 EOI」「已占用」；以及分发末尾对 EOI / 调度的挂钩说明。

**明确不做（留给别的篇或调用约定）：**

- 写 APIC / GIC 寄存器、改硬件亲和、屏蔽或打开外设线（`26` / `27`）；
- MSI、Linux 式 `request_irq` 框架；
- 把异常 EC / 缺页当成「设备 IRQ」来讲（`23`）；
- 同核上「正在处理时拆表」的同步——调用方先停投递。

---

## 3. 分层与调用方

| 角色 | 做什么 |
|------|--------|
| `arch_start_platform` | BSP 上 `irq_global_vector_alloc`（须已有 `kallocator`；长度由平台决定） |
| `init_interrupt` | 要求全局数组已就绪；再 `arch_init_interrupt`（装 IDT / 设 VBAR） |
| Timer | `rendezvos_time_init`：取 `arch_get_timer_irq_num` 后 `register_irq_handler(…, this_cpu, IRQ_NEED_EOI)`（x86=`0x20`；aarch64=DTB ns-phys） |
| IPI | `arch_smp_ipi_init`：`register_irq_handler(ARCH_IRQ_ID_IPI, this_cpu, …)` |
| Fault / SVC | `arch_register_fixed_trap` 内部再调 `register_irq_handler`（带每个匹配 id 与每个 `cpu_id`） |
| 设备 | **先 `irq_global_alloc`，再 `register_irq_handler(..., IRQ_NEED_EOI)`，然后由平台打开线路 / 改路由** |

---

## 4. 数据结构与不变量

### 4.1 表项

```c
struct local_irq {                     /* rendezvos/trap/irq_space.h */
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;                  /* 普通写；每核一份 */
};
DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

/* trap_common.h：各 arch 的 arch_global_irq 公共前缀 */
#define GLOBAL_IRQ_COMMON \
        void (*irq_handler)(struct trap_frame *tf); \
        atomic64_t irq_attr;           /* 占用时原子置「已占用」*/ \
        size_t local_irq_id; \
        cpu_id_t cpu_id;

struct arch_global_irq {               /* 各 arch/*/trap/irq_space.h */
        GLOBAL_IRQ_COMMON
        /* x86 另有 ioapic_id、pin；aarch64 目前无额外字段 */
};
extern struct arch_global_irq *global_irq_vector;
extern size_t nr_global_irq;
```

`local_irq` 不再套全局宏：本核表只有 handler + 普通 `u64` 属性。属性位：`IRQ_NEED_EOI`、`IRQ_ATTR_USED`（见 `trap_common.h`）。`irq_attr_*` / `irq_atomic_attr_*` 见 `irq_space.h`。

### 4.2 编号空间（硬件贴合）

| | x86_64 | aarch64 |
|--|--------|---------|
| 本核表长度 | `NR_ARCH_LOCAL_IRQ` = LO（`0x40`） | 同宏（EC+SGI+PPI 上界） |
| IDT / 硬件全宽 | IDT **256**（SDM：向量 0–255）；表外号仍可作 IDT 向量，但不进 `local_irq_vector[]` | CPU **没有** 256 项向量表；GIC 用 INTID |
| 身份 | 向量号 = `local_irq_id` | `[0,64)` = EC；`[64,…)` = intid+64 |
| 表外窗口 | `[ALLOC_LO, ALLOC_HI]` = `[0x40, 0xEF]` | 同；`try_direct`：local = INTID + 偏移 |
| 全局数组长度 | 启动时申请（暂定 `NR_ARCH_GLOBAL_IRQ`，待 MADT） | `gic.nr_spi()` |

x86 上 Intel 保留 **0–31** 给架构异常。设备 / timer / IPI 必须落在 ≥32 的向量上；timer / spurious / IPI 用固定宏，设备走表外窗口再 map 进全局表。

aarch64 上为了复用同一套「按编号分发」外形，把 EC 放入前 64 槽、IRQ 整体平移 +64——Trap 篇说的「假统一、真映射」。SGI+PPI 落在本核表内；SPI 是表外号，经公式映到 `global_irq_vector[]`。

### 4.3 本核固定槽、表外窗口与空洞

读布局时，把编号分成三类即可：

**① 本核表内的固定槽（结构性，无需旧式 reserve）**

异常 / EC、timer、IPI，以及 aarch64 上整段 SGI+PPI，都会打到「被中断的那颗核」。入口汇编查的是本核的 `local_irq_vector[]`，所以这些编号落在 `[0, LO)`，直接 `register_irq_handler(id, this_cpu, …)` 即可。handler 可以晚装（timer / IPI），也可以故意不装（x86 spurious：误入则 unknown，随后 panic）。

**② 表外窗口 → `global_irq_vector[]`**

外设 SPI / 将来 IOAPIC·MSI 用大于等于 LO 的 `local_irq_id`。软件上先 `irq_global_alloc(global_irq_id, cpu_id, &local)`，再 `register_irq_handler`。全局表全机一份；亲和写在表项的 `(local_irq_id, cpu_id)` 上。

`irq_global_alloc` 怎么定本核编号：先问架构 `arch_try_direct_map_local_irq_id`——aarch64 用公式能直接给出就用它；架构给不出（例如 x86 没有 GSI 与向量的固定公式，返回无效）时，再在目标核上扫描 `[ALLOC_LO, ALLOC_HI]` 找空闲号。定下之后用原子操作占全局项，并写下 map。

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

和 §1.3 一起看：本核表是每核一份的，固定槽在每核各自挂函数；设备走全局表并记亲和，不是旧式「全 CPU 共用一个 handler」。

### 4.4 注册不变量

细则与「为什么」见 §1.4，这里列硬规则：

- `register_irq_handler` / `irq_global_alloc` / `irq_global_free`：**必须给具体 `cpu_id`**（不能用 `CPU_ID_INVALID`）。
- `irq_global_free`、**全局**路径的 `register_irq_handler`：调用方必须是属主（`percpu(cpu_number) == cpu_id`），否则 free 返回 `-E_IN_PARAM`，register 打 error 后返回。
- `irq_global_alloc`：允许任意核替目标 `cpu_id` 抢未占用项（启动 / 迁移）。
- 换核：属主先挡住投递 → 属主 `free`（保留 handler 与「需要 EOI」）→ 再 `alloc` 给新核 → 改硬件路由并打开；处理函数不变则**不必**再 register。
- map / find：`CPU_ID_INVALID` 表示当前 CPU。
- 本核表内：写该 `cpu_id` 上的 `local_irq_vector[local_irq_id]`（实现会 `attr | USED`）。本核表路径**不**要求 caller == `cpu_id`（fixed trap 启动时会替各核装表）。
- 全局：须已 alloc 且亲和匹配，否则 find 失败；register 另做属主检查。
- `handler` 传空指针可以摘掉处理函数；**释放不会**把 handler 置空，也**不会**把属性整字清零，只清「已占用」。
- 没有「同一全局中断每核不同处理函数」——全局项只有一对亲和。

---

## 5. 代码对应

| 路径 | 属于哪一层 | 职责 |
|------|--------------|------|
| `kernel/trap/irq_space.c`、`irq_space.h` | **编号空间** | 两套编号、占有 / 释放 / 查找、属性辅助 |
| `arch/*/trap/irq_space.c`、`irq_space.h` | **编号空间（按架构）** | `struct arch_global_irq`、区间宏、local↔global 的 map |
| `kernel/trap/trap.c`、`trap.h` | **分发** | `register_irq_handler`、`trap_handler`、`init_interrupt` |
| `arch/*/trap/trap.c` | **分发 + 装表** | fixed 注册、`arch_eoi_irq`、IDT / VBAR |
| `arch/*/boot/start_arch.c` | 启动 | 申请 `global_irq_vector` |
| `kernel/time/time.c`、`arch/*/smp` | 调用方 | timer / IPI 挂本核表 |
| 平台 APIC / GIC | **硬件** | 路由、打开线路、EOI 寄存器（见 `26` / `27`） |

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

**首次占用（装驱动）：**

1. `irq_global_alloc(global_irq_id, cpu_id, &local_irq_id);` — **勿**手写 timer / IPI / EC 当设备号。内部先 `arch_try_direct_map_local_irq_id`；架构不给直接映射再扫空闲 local（见 §4.3 ②、§7.3）。可在别的核上替目标 `cpu_id` 调用。
2. 在**属主核**上 `register_irq_handler(local_irq_id, cpu_id, isr, IRQ_NEED_EOI);` — 软件表就绪。
3. **再**由平台改路由 / 打开线路：
   - x86：向量 = `local_irq_id`（IOAPIC / MSI 见 APIC 篇；v0.1 IOAPIC 还是空壳，外设线基本接不上）。
   - aarch64：`intid = AARCH64_TRAP_ID_TO_IRQ(local_irq_id)`，再调用 GIC 的打开与（可选）改亲和。
4. 未占号就挂函数、或非属主核去挂全局号：打错误日志后返回。
5. 处理函数返回后由 `trap_handler` 做 EOI——函数里面**不要**自己写 EOI，除非故意不设「需要 EOI」。

**只换核、处理函数不变：** 属主上先挡住投递 → `irq_global_free` → `irq_global_alloc` 给新核 → 改硬件路由并重新打开。**不要**为换核再 register 一遍（见 §1.4）。挡住投递、改路由属于硬件层，不在 irq_space 里。

**卸模块 / 换处理函数：** 属主上先挡住投递 →（建议）`register(..., NULL, …)` 或直接挂新函数 → 若不再占用则 `irq_global_free` → 再卸代码。切记释放不会替你清掉 handler。

---

## 7. 公开 API

本篇涉及的接口分布在：两套 id（`irq_space.h` / `irq_space.c`）与分发（`trap.h` / `trap.c`）——`struct local_irq` / `struct arch_global_irq`、`irq_global_*`、`register_irq_handler`、`trap_handler`、`init_interrupt`、`arch_map_*`（`arch/*/trap/irq_space.c`）/ `arch_init_interrupt`、`arch_eoi_irq`、`arch_unknown_trap_handler`。说明改写自 `irq_space.h` / `trap.h` / `arch/*/trap/irq_space.h` Doxygen。

**本篇不涉及：** `arch_register_fixed_trap` / `trap_class`（见 `23`）；weak `syscall`（见 `24`）；APIC / PIC / GIC 寄存器与路由（见 `26` / `27`）；timer / IPI 业务语义（见时间篇 / `30`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| BSP 一次 | `arch_start_platform` 里 **`irq_global_vector_alloc`**（须已有 `kallocator`） |
| 每核装表 | `arch_start_core` 调 **`init_interrupt`**（检查全局数组 → `arch_init_interrupt`），然后平台 IRQ iface、`smp_ipi_init`，……再到 `rendezvos_time_init` |
| 设备驱动（首次） | **先 `irq_global_alloc`，再在属主核 `register_irq_handler(local, cpu_id, 处理函数, IRQ_NEED_EOI)`，然后**由平台打开线路 / 改路由 |
| 设备换核 | 属主挡住投递 → `free` → `alloc` 给新核 → 改路由并打开（处理函数不变则不 register） |
| Fault / SVC | `arch_register_fixed_trap`（内部再 `register_irq_handler`；本核表内编号） |
| Timer / IPI | 本核表内固定号，直接 `register_irq_handler(ARCH_IRQ_ID_*, this_cpu, …)` |

未 alloc / 非属主就 register 全局号：error log 后 return。ISR 内勿自写 EOI（除非故意不设 `IRQ_NEED_EOI`）。

### 7.2 表项与属性

本核表见 §4.1 的 `struct local_irq`。全局表项公共前缀是 `GLOBAL_IRQ_COMMON`（`trap_common.h`），各 arch 的 `struct arch_global_irq` 再叠私有字段。

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
struct arch_global_irq *irq_global_find(size_t local_irq_id, cpu_id_t cpu_id);
```

| 接口 | 说明 |
|------|------|
| `irq_global_vector_alloc` | 全机一次；表项初值为无效 id / 未占用。`count==0` → `-E_IN_PARAM`；已分配或分配器失败 → `-E_RENDEZVOS`。 |
| `irq_global_alloc` | 见下。可替任意目标 `cpu_id` 抢未占用项。错误：map/忙 → `-E_REND_AGAIN`；窗口用尽 → `-E_REND_OVERFLOW`；参数非法 → `-E_IN_PARAM`。 |
| `irq_global_free` | **须在属主 CPU 调用**（否则 `-E_IN_PARAM`）。清 map → 清亲和字段 → 复位架构私有字段 → **最后** `try_set_unused`（保留 handler 与 `NEED_EOI`）。亲和不符或未占用 → `-E_REND_NOFOUND`。 |
| `irq_global_find` | map 后再核占用 / local / cpu_id；`CPU_ID_INVALID` = 当前 CPU。 |

`irq_global_alloc` 的过程可以概括成两步：

1. **先试架构直接映射**：调用 `arch_try_direct_map_local_irq_id(global_irq_id)`。架构若能给出确定的 `local_irq_id`（aarch64：SPI 下标 ↔ INTID ↔ local），就用它——前提是该号在目标核上仍空闲。
2. **架构不允许直接映射时再找 id**：返回 `IRQ_LOCAL_ID_INVALID`（x86：没有 GSI 与向量的公式对应）时，在目标核上扫描 `[ALLOC_LO, ALLOC_HI]` 挑空闲 `local_irq_id`，再 `arch_map_local_irq_id` 写进 per-CPU map。

两步都会 CAS 占全局项；map 失败则回滚占用。

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
| `register_irq_handler` | 本核表内：写该 `cpu_id` 槽（不要求 caller 即该核）。全局：须已 alloc、亲和匹配，且 **caller 必须是属主**；`handler==NULL` 可摘 ISR。 |
| `trap_handler` | 按 `TRAP_ID` 查本核表或 `irq_global_find`；无则 unknown+panic；若 NEED_EOI 则 EOI；用户来源且 `core_tm` 就绪则 `schedule`。 |
| `arch_eoi_irq` | 平台 ACK（细节见 `26`/`27`）。 |
| `arch_unknown_trap_handler` | 打印帧；随后 `trap_handler` panic。 |

驱动 checklist 见 §6.3。x86 `syscall` **不**经本路径。

---

## 8. 多架构

编号与硬件号的对照见 §1.2、§4.2–§4.3。差异再收三条：

| | x86_64 | aarch64 |
|--|--------|---------|
| 表外 map | 每核一份；同 LO 可异 GSI | 公式；同 LO 必同 SPI |
| alloc 时的 local | `try_direct` → INVALID → 扫描空闲表外号 | `try_direct` → 公式算出 local |
| 打开外设线 | IOAPIC 未实现 | `gic.unmask_irq` |

控制器细节见 APIC / GIC 篇。riscv / loongarch 非 v0.1 主线。

---

## 9. 测试

间接：timer、IPI、SMP。无单独「全局占用耗尽 / 亲和错误」测试用例。

---

## 10. 限制与后续

**平台与功能缺口：**

- `irq_global_alloc`：忙或 map 冲突 → `-E_REND_AGAIN`；表外窗口用尽 → `-E_REND_OVERFLOW`；参数非法 → `-E_IN_PARAM`。
- x86 全局数组长度仍是启动时的暂定值；待 MADT 给出真实 GSI 数量。IOAPIC 路由未齐时，占出来的表外号还无处可编程。
- 伪中断（spurious）无专用 handler。
- 没有「同一编号挂多设备」的共享链（后写覆盖）；电平共享见 §6.2。
- 没有「同一全局中断每核不同 handler」。
- 没有可移植的 `irq_set_affinity`；GIC 内部有改亲和，x86 尚无。绑核见 GIC 篇。

**irq_space 本身仍靠约定、代码未再加锁的点：**

- 同核上：若未先挡住投递就释放或改 handler，仍可能和正在跑的 `trap_handler` 交错——属主检查挡不住这件事。
- 释放留下旧 handler：调用方卸模块前必须自己摘掉，否则下次只占号不挂函数会踩旧指针。

---

## 11. 变更记录

- 2026-10-23：理顺 §1 结构（去掉重复的 1.3 编号）；开篇用表格划清「编号空间 / 分发 / 硬件」；§5 / §10 同步。中文表述按阅读习惯整理。
- 2026-10-23：`IRQ_COMMON` 改为 `GLOBAL_IRQ_COMMON`（handler + atomic attr + local/cpu 亲和）；`local_irq` 直接写字段，不再共用该宏。
- 2026-10-23：占有模型落地——全局 `register` 亦要求属主核；`free` 改为 `try_set_unused` 且保留 handler（换核不必再 register；卸模块须自行摘 handler）。新增占有规则小节，并改 §4.4 / §6.3 / §7。
- 2026-10-23：明确 `USED` CAS 只挡并发 alloc；`irq_global_free` 强制属主 CPU（caller == `cpu_id`），注释与 §4.4 / §7.3 同步。顺带收紧 `irq_global_alloc` 里仅扫描分支使用的 `mapped`。
- 2026-10-23：模块文件改名 `irq` → `irq_space`（表达 id 命名空间）；`struct global_irq` → `struct arch_global_irq`。API 前缀（`irq_global_*` 等）与 `global_irq_vector` 符号名不变。
- 2026-10-23：软件 id / map / alloc 抽到 `kernel/trap/irq_space.c` + `include/rendezvos/trap/irq_space.h`，arch 侧 `arch/*/trap/irq_space.c` / `irq_space.h`；`trap.c`/`trap.h` 只留分发与装表。逻辑未改，仅迁移。
- 2026-10-22：`arch_map_fixed_local_irq_id` 更名为 `arch_try_direct_map_local_irq_id`——alloc 先试直接映射，架构不接受再扫描空闲 local。
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
