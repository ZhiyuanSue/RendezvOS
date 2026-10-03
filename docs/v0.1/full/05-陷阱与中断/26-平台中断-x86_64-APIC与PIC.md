# 平台中断：x86_64 APIC 与 PIC

v0.1 · 2026-09-27

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
| **I/O APIC** | 板级 / 芯片组；把 PCI / ISA 线编成「发到某个 LAPIC 的某个向量」 | **`IOAPIC.h` 空壳**；MADT IOAPIC 条目 `break` 为空操作 (no-op) |

没有 IOAPIC，就没有规范的「外设 IRQ → 向量」编程路径——timer / IPI 仍可走 LAPIC 自有源；PCI 设备中断基本接不上。

### 1.3 OS 对硬件的硬依赖（读代码前先对齐）

1. **IDT 必须先装好**（`lidt`），向量 gate 指向 `trap_N`；否则开中断即飞。
2. **写进 LVT / SVR / ICR / PIC ICW2 的向量号**，必须已在 `irq_vector[]` **reserve**，且已 `register_irq_handler`（timer / IPI）或可进 unknown。
3. **处理完设备 / timer / IPI 类中断后要 EOI**（`IRQ_NEED_EOI` → `APIC_EOI` 写 0，或 `PIC_EOI`）；漏 EOI 会卡住同级 / level 线。
4. **用 APIC 就必须 mask 掉 8259**，否则双路径乱投。
5. **xAPIC 须把 `0xFEE00000` 映成 UNCACHED**（`map_LAPIC`）；缓存一致性错误会读到脏寄存器镜像。

### 1.4 边沿触发与电平触发（后面读 TMR / EOI 会用到）

硬件认两种「这条线什么时候算来了一枪」：

- **边沿（edge）**：电平一变就记一次。ISR 跑完后，线即使还停在新电平上，也不会再要一次，除非再翻一次边。
- **电平（level）**：只要线还维持在有效电平，控制器就认为中断还在。漏 EOI、或 ISR 里没把设备状态清掉，同一条线会反复打进来。

8259 初始化走边沿；Local APIC 用 **TMR** 记住每个向量当时是边沿还是电平。电平向量在写 EOI 时，硬件还可能向 I/O APIC 广播一声（见 §5.6）——本仓库没有 IOAPIC，这条广播目前没有消费者，但读手册时不要当成「写 0 就完事」。

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

端口：主片命令/数据 `0x20` / `0x21`，从片 `0xA0` / `0xA1`（`io_port.h`）。写顺序固定：**ICW1 → ICW2 → ICW3 → ICW4**；ICW1 必须写到偶地址（命令口），后面几个写到奇地址（数据口）。漏一步或顺序反了，向量基址和级联关系都会错。

| ICW | 本实现要点 | 再多记一点 |
|-----|------------|------------|
| ICW1 | 需要 ICW4、边沿触发、级联（非单片） | bit0=1 表示后面还有 ICW4；单片系统才会清 cascade |
| ICW2 | 向量基址高 5 位：主 `0x20`、从 `0x28` | 低 3 位由线号填进，所以主片 IRQ0→`0x20`、IRQ7→`0x27` |
| ICW3 | 主：bit2=1（从片挂 IRQ2）；从：写 index=2 | 主片与从片必须说的是「同一根 cascade 线」 |
| ICW4 | 8086 模式（`uPM`）；**手动 EOI**（非 AEOI） | AEOI 会在进 ISR 时自动结束，嵌套和调试都更难；我们刻意不用 |

随后用 **OCW1** 写 IMR（中断屏蔽寄存器）：主片初值 **`0xFB`**（放开 cascade IRQ2，其余 mask）、从片 **`0xFF`** 全 mask。设备要用时再 `enable_PIC_IRQ`（清 IMR 对应位）。

8259 内部还有和 APIC 同名但不同片的概念，读端口时别混：

| 名字 | 在 8259 里 | OS 日常 |
|------|------------|---------|
| **IMR** | 哪些线被软件挡住 | OCW1；`enable/disable_PIC_IRQ` |
| **IRR** | 哪些线正在请求 | 调试时可经 OCW3 读；启动路径不碰 |
| **ISR** | 哪条线正在被服务 | 同上；写 EOI 会清这里 |

### 4.3 EOI（OCW2）与「先从后主」

`PIC_EOI(irq_num)`：向对应片写 OCW2 的 EOI。8259 常见两种：

- **非特定 EOI**：清 ISR 里当前最高优先级那一位（本仓库宏走这类）。
- **特定 EOI**：显式点名清哪一条——多级嵌套时偶尔需要，当前代码没用。

从片上的中断（IRQ8–15）在硬件上会先拉高主片的 **IRQ2（cascade）**。经典做法是：**先给从片 EOI，再给主片 EOI**，否则主片 ISR 里 cascade 位清不掉，整条从片链路可能再也进不来。

`PIC_EOI` 对从片向量按该顺序写两片（非特定 EOI）；主片向量只 EOI 主片。

### 4.4 OS 依赖小结（PIC）

- 必须先 `lidt`，IDT[`0x20`…] 有效。
- 向量基址改了就必须同步改 `ARCH_IRQ_VEC_*` / reserve——当前固定为 `0x20`/`0x28`。
- 手动 EOI：handler 经 `arch_eoi_irq` → `PIC_EOI`。
- `enable_PIC_IRQ` 的参数是 **IDT 向量**（如 `0x21`），不是「线号 1」；实现里会减基址再算 IMR 位。

---

## 5. 硬件深度：Local APIC

Local APIC 是**每核一份**的控制器：管本核的 timer / LINT / IPI 接收，也管「向量已经到了本核之后」的排队与 EOI。板级外设线本来该由 I/O APIC 编成「打给哪个 LAPIC、哪个向量」——那一层本仓库还是空壳，所以下面只讲 **CPU 脚边这一块**。

手册入口：SDM Vol.3 第 10 章。寄存器占一个 4 KiB 窗口，复位物理基址 **`0xFEE00000`**。xAPIC 必须映成 **strong uncacheable（UC）**（SDM 11.3）；访问 32 位以上的寄存器要对齐（手册要求 128-bit align）。可选把窗口迁到别处（10.4.5），本仓库**不迁**，且要求 MADT 里的地址与 `0xFEE00000` **完全一致**。

常用 MMIO 偏移（相对基址；x2APIC 用「索引 = 偏移/0x10」进 MSR `0x800+index`）：

| 偏移 | 寄存器 | 本仓库 |
|------|--------|--------|
| `0x020` | APIC ID | 当 CPU 身份 |
| `0x030` | VERSION | 几乎不读 |
| `0x080` | TPR | `reset` 写 0 |
| `0x0B0` | EOI | `APIC_EOI` 写 0 |
| `0x0E0` | DFR | 仅 xAPIC；`reset_xAPIC_LDR` |
| `0x0D0` | LDR | 同上 |
| `0x0F0` | SVR | `software_enable_APIC` |
| `0x300` / `0x310` | ICR_LOW / HIGH | `APIC_send_IPI` |
| `0x320` | LVT Timer | timer 路径 |
| `0x350` / `0x360` | LVT LINT0 / LINT1 | `reset_APIC` |
| `0x340` | LVT PERF | → NMI |
| `0x380` / `0x390` / `0x3E0` | INIT_CNT / CURR_CNT / DCR | timer 计数 |

复位时各 LVT 的 **Mask 位为 1**（默认全挡住），软件必须显式放开要听的源。

### 5.1 启用、寻址、以及「这颗核叫什么」

MSR **`IA32_APIC_BASE`**（SDM 10.4.3）：

| 位 | 含义 |
|----|------|
| bit 8 | 本核是不是 BSP |
| bit 10 | 开 x2APIC |
| bit 11 | APIC 全局使能 |
| bit 12–MAXPHYADDR | MMIO 基址（xAPIC） |

组合：`11`=x2APIC，`10`=仅 xAPIC，`00`=关掉，`01` 非法。探测：CPUID.01H EDX bit 9 = 有 Local APIC；ECX bit 21 = 有 x2APIC。

| 模式 | 怎么访问 | 基址 |
|------|----------|------|
| xAPIC | MMIO，寄存器间距 0x10 | 物理 **`0xFEE00000`** |
| x2APIC | `RDMSR`/`WRMSR`，索引 `0x800 + 寄存器号` | 不再走那块 MMIO |

`enable_xAPIC` 置 bit 11；`enable_x2APIC` 再置 bit 10（全局使能保持）。`map_LAPIC` **只在 xAPIC**：映成 **UNCACHED | GLOBAL | RW**。缓存页会让 EOI / 读-改-写看到过期镜像。x2APIC 的 MSR 访问不是一定可序列化的；需要顺序时要用 fence。MMIO+UC 路径本身带某种有序性，所以 xAPIC 不另加 fence。

**APIC ID**（xAPIC MMIO `FEE0 0020H`；x2APIC 为 MSR `802H`，整 32 位都是 ID）：多核里我们把它当 CPU 身份。启动决定的 ID 还能从 CPUID.01H EBX[31:24] 读到；**即使软件改了 APIC ID 寄存器，这条 CPUID 仍返回启动时的值**。x2APIC 下 8 位不够：若 CPUID 最大叶 ≥ `0x0B`，且 `0x0B, ECX=0` 时 EBX ≠ 0，用 **CPUID.0BH:EDX** 拿 32 位 ID（低 8 位与 01H 那条一致）。

**VERSION**（SDM 10.4）：[7:0] 版本（`0xH` 为 82489DX，`10H–15H` 为集成 APIC）；[23:16] 最大 LVT 项；bit 24 表示能不能靠 SVR bit 12 **关掉 EOI 广播**。本仓库几乎不读这个寄存器，但解释 TMR/EOI 广播时会用到。

**ESR（Error Status）**：若干出错原因位。当前路径不作为启动依赖。

### 5.2 SVR — 软件使能与 Spurious（SDM：Spurious-Interrupt Vector Register）

**SVR**（寄存器索引 `0xF`）：

- **bit8 `SW_ENABLE`**：Local APIC 软件使能（soft-enable）。关着时大量投递被吞或行为异常。
- **低 8 位**：spurious 向量——硬件无法交付「真」中断时可能投这个向量。

其余位：Focus Processor Checking（bit 9）；**bit 12** 控制写 EOI 时要不要向 I/O APIC **广播**（电平触发、且 VERSION 允许禁用时才有意义）。默认是允许广播的；手册更推荐关掉广播、改写产生该中断的那颗 IOAPIC 的 EOI——本仓库没有 IOAPIC，bit 12 也未特意去关。

本仓库：`software_enable_APIC()` 置 enable，并把 spurious 写成 **`ARCH_IRQ_VEC_SPURIOUS`（0x27）**。

**时序陷阱：** SVR 软件使能 **不在** `init_irq` 末尾，而在 **`arch_init_timer` 路径**上才调用。开中断（`sti`）与真正「APIC 开始干活」之间可能有窗口——读启动日志时别假设 `init_irq` 完就能收 timer。

**Spurious 无 handler**：IRQ 篇只 reserve `0x27`；若误入 → `trap_handler` unknown → panic。正常不应频繁 spurious。软件抬高 TPR、把刚到的中断 mask 掉时，硬件就可能改投这条 spurious 向量——和 GIC 读 IAR 得到 1023 是同一类「来了但此刻不该进 ISR」的出口。

### 5.3 LVT — 本地向量表（SDM：Local Vector Table）

每项描述「本核本地源 → 哪个向量、什么投递模式、是否 mask」。常见项：Timer、Thermal、PERF、LINT0、LINT1、Error、CMCI（较新的 CPU 才有）。

| LVT | 本实现 | 含义 |
|-----|--------|------|
| **Timer** | 校准后写向量=`arch_get_timer_irq_num`（0x20）、模式 periodic / one-shot / TSC-deadline | 系统 tick 源 |
| **LINT0** | `reset_APIC` 里 **MASKED** | 常接 ExtINT / 8259；我们用 APIC 时 mask 掉 |
| **LINT1** | **漏写**（像复制粘贴错误，LINT0 写了两次） | 常接 NMI；当前未显式 mask |
| **PERF** | 投递模式 **NMI** | 性能计数溢出 → NMI（进 IRQ class，见 Trap 篇） |
| Thermal / Error / CMCI | 未重点用 | — |

每一项大致共用同一套字段（Timer / LINT 略有加减，见图 10-8）：

| 位域 | 含义 |
|------|------|
| **[7:0] Vector** | 投进 IDT 的向量。APIC 合法范围是 **16–255**；写 **0–15** 且 Delivery=Fixed 时可能置 ESR「非法向量」，**不一定**真投递。软件 reserve 的 timer/IPI 都已避开 0–31 里的异常槽。 |
| **[10:8] Delivery Mode** | Fixed / SMI / NMI / ExtINT / INIT…。SMI/INIT 时向量域习惯写 0。ExtINT 表示「去问外面的 8259 要向量」——和我们 mask LINT0 的动机一致。 |
| **[12] Delivery Status** | 只读：1 = 还在往核里送。 |
| **[13] Polarity** | LINT：0 高有效 / 1 低有效（仅 Fixed 时有意义）。 |
| **[14] Remote IRR** | 电平 + Fixed 时用；表示远端还认为这条线 active。 |
| **[15] Trigger** | LINT：0 边沿 / 1 电平（仅 Fixed）。NMI/SMI/INIT 一律当边沿；ExtINT 一律当电平；**Timer / Error 恒为边沿**。 |
| **[16] Mask** | 1 = 不接收。复位默认 1；PERF 溢出处理完硬件常会**自动再 mask**，要继续听得软件清。 |
| **[18:17] Timer Mode** | 仅 LVT Timer：`00` one-shot，`01` periodic，`10` TSC-deadline，`11` 保留。 |

**OS 依赖：** 改 LVT 向量前，软件 `register_irq_handler` 必须已挂好；否则第一声 tick 就 panic。向量不要写进 0–15。

### 5.4 定时器计数器（与 LVT Timer 配套）

| 寄存器 | 作用 |
|--------|------|
| **DCR（Divide Configuration）** | 把 APIC bus 时钟再分频。常用编码：÷2 / ÷4 / ÷8 / ÷16 / ÷32 / ÷64 / ÷128 / ÷1。本实现校准 / 运行多用 **`DIV_16`**。 |
| **INIT_CNT** | 写入初始计数；开始倒数。写它会重装计数器。 |
| **CURR_CNT** | 当前剩余（只读）。到 0 时依 LVT Timer 模式：one-shot 停；periodic 从 INIT_CNT 再装一轮。 |

时钟源：APIC timer 的计数时钟来自 **CPU bus / APIC timer clock**（具体倍频因平台而异），**不是** TSC。所以换机器必须校准。`APIC_timer_calibration` 用 **8254 PIT** 作时间基准（`PIT_mdelay`），测「APIC 计数走过多少 ≈ 真实多少时间」；TSC-deadline 路径另用 `TSC_timer_calibration`。HPET 未接。

**ARAT（Always Running APIC Timer，CPUID.06H:EAX bit 2）：** 部分 CPU 在深 C-state 时仍让 APIC timer 跑；无 ARAT 时 bus-clock timer 可能随核睡眠停走，唤醒后校准值不可靠。代码有 `ARAT_support()` 探测，**当前校准 / 选型路径未据此分支**——文档记能力位；真正选型仍看 TSC-Deadline / ONE_SHOT（见 `33`）。

TSC-deadline 模式：计数走 CPU 时钟而不是 APIC bus，一般更稳。步骤是：确认 CPUID.01H ECX.TSC_Deadline → LVT Timer 选 TSC 模式 → 写 MSR **`IA32_TSC_DEADLINE`（`6E0H`）** → 到期仍经 LVT Timer 向量进 IDT → 下次再写 deadline。不走 INIT_CNT / CURR_CNT。写 deadline 之前若模式还没切到 TSC，行为未定义——代码路径里要先改 LVT 再写 MSR。

### 5.5 TPR / PPR：谁会被送给 CPU

接收侧先过一张「现在还接不接得住」的门槛（SDM 流程图 10-17）：

- **NMI / SMI / INIT / ExtINT** 一类：不跟普通向量比优先级，可以直接 accept。
- 其余 **Fixed** 向量：要过 **TPR（Task-Priority Register）**。

APIC 给每个向量一个优先级，就拆在向量号自己身上：

- **优先级 class** = 向量 `[7:4]`（0–15）
- **sub-class** = 向量 `[3:0]`

TPR 同样拆成 class / sub-class。规则大意是：只有 **class 高于** 当前 TPR class 的才会送给 CPU（同 class 时再看 sub-class 的细节以 SDM 为准）。64 位下写 **CR8** 只动 TPR 的 class 那半（CR8[3:0] → TPR[7:4]），写完后的重仲裁会在下一条指令边界生效。

**PPR（Processor-Priority Register）** 只读，表示「CPU 正在执行的那档」：PPR[7:4] = max(TPR[7:4], ISRV[7:4])；若 TPR class 更大则 PPR[3:0]=TPR[3:0]，否则为 0。ISRV 是 ISR 里当前最高那一位对应的向量优先级。

和 GIC 对比：GIC 用 `IPRIORITYR` **单独**给每条 INTID 打分；x86 APIC **没有**那张表——换向量号就等于换优先级。这也是为什么软 IPI 选 `0x30`、timer 选 `0x20`：既要避开 0–31 异常区，又要落在「足够高、彼此可区分」的 class 上。

本仓库 `reset_APIC` 把 **TPR=0**：门槛放到底，尽量都收。调试 API（`lapic_get_vec`）可读 ISR / IRR / TMR；对只读寄存器做 set/clear 可能无效——已知缺口。

### 5.6 IRR / ISR / TMR / EOI（一条中断怎么进 CPU、怎么结束）

可以按时间顺序读这四组：

1. 中断被本核 **accept** → 置 **IRR**（Interrupt Request）里对应向量位：已经收下、还没交给 CPU。
2. CPU 准备处理下一条 → 清 IRR 里当前最高优先级那一位，同时在 **ISR**（In-Service）同一位置位：正在服务。
3. ISR 跑完、`iret` 之前写 **EOI**（32 位寄存器，手册没规定「写哪个向量」，惯例写 **0**）→ 清 ISR 里最高优先级位，同级后续才能再来。
4. **TMR（Trigger Mode）** 与向量一一对应：1 = 当时按电平收下，0 = 边沿。写 EOI 时若 TMR=1，硬件可能向所有 I/O APIC 发一次 EOI 广播；可用 SVR bit 12 关掉（还要看 VERSION 那一位允不允许关）。

IRR 和 ISR 都是 **256 位**，向量 0–15 保留（CPU 异常，不是 APIC 投递的有效向量）。同一向量可以 **IRR 与 ISR 同时置位**：一个正在服务，另一个在排队——也就是同一向量最多缓存两枪。更高优先级、且 CPU 没关中断时，**不必先 EOI** 就能打断当前 ISR，形成嵌套。

本仓库：`APIC_EOI()` → `APIC_WR_REG(EOI, …, 0)`；由 `arch_eoi_irq` 在 `IRQ_NEED_EOI` 时调用。**不要**在 ISR 里提前 EOI 再做长活（除非明确理解电平触发会立刻重入）。

### 5.7 ICR — 核间中断（SDM：Interrupt Command Register）

IPI / AP 启动拉核（INIT-SIPI）都走 ICR：

| 字段 | 本代码宏 | 用途 |
|------|----------|------|
| Vector | 低 8 位 | 软 IPI 用 **`0x30`** |
| Delivery mode | FIXED / INIT / STARTUP / NMI / … | 启核用 INIT + STARTUP；运行时软 IPI 用 FIXED |
| Dest mode | physical / logical | |
| Level / Trigger | assert + edge 等 | INIT 序列需要 |
| Destination shorthand | self / all / all-ex-self / no shorthand | |
| Dest field | xAPIC 在 ICR_HIGH[63:56]；x2APIC 在 64-bit ICR | 目标 APIC ID |

**xAPIC 发送顺序（硬件要求，代码已遵守）：**

1. 等 ICR **Delivery Status** 清零（上一次送完）；
2. **先写 ICR_HIGH**（目的地），**再写 ICR_LOW**（触发发送）。

**x2APIC：** 单次 `wrmsr` 64-bit ICR；无 Delivery Status 轮询。注释提醒：部分路径 `edx` 强制 0 时定向目的地可疑——已知缺口。

**OS 依赖：** `0x30` 必须已 register；软 IPI 协议（generation / pending）见 SMP 篇——本篇只提供「把向量砸到目标核」的门铃。

除 ICR 外，逻辑目的地还看 **LDR（Local Destination）** 和 **DFR（Destination Format）**（仅 xAPIC 有完整可编程语义）：

- **物理 dest**：目标就是 APIC ID。
- **逻辑 dest**：8-bit MDA。LDR 高 8 位是 logical APIC ID。DFR 高 4 位：全 1 = **flat**（MDA 与 LDR 做 AND，非 0 就收）；全 0 = **cluster**（更绕，本仓库未走这条）。shorthand 为 self / all / all-ex-self 时 **不用** LDR/DFR。
- **Lowest Priority**：会在候选核里转一圈，最终由优先级最低的那个接——还要看 **APR**。本仓库软 IPI 走物理 + shorthand，不依赖这套。

写 ICR **低 32 位**的那一下，IPI 才上系统总线。SIPI（Delivery Mode `0b110`）失败**不会**自动重发，其它消息会。

**启核（INIT–SIPI）和运行时软 IPI 不是同一条故事：** AP 起来时通常先发 INIT（让目标进 wait-for-SIPI），再发一两次 STARTUP/SIPI（向量字段里带的是启动页框号，不是普通 IDT 向量）。运行期门铃则是 Delivery=Fixed、向量=`0x30`。两者都走 ICR，但字段组合完全不同——SMP 启核细节见启动 / PSCI 对照的 x86 篇，本篇不展开时序。

### 5.8 DFR / LDR 与 x2APIC 的差异

`reset_xAPIC_LDR`：DFR=全 1（flat），LDR 低位置 bit0。x2APIC：**没有 DFR**（`80EH` 不存在）；ICR 合成一个 64 位 MSR，**去掉 Delivery Status**（手册说从此不必再读 ICR 等发送完成）；另有 **Self IPI**（只写向量，等价于 ICR「打给自己」）。LDR 扩成 32 位（高 16 cluster、低 16 logical ID）。必须先进入 x2APIC 模式才能用 MSR 访问这组寄存器，否则会出错。

MSI（Message Signaled Interrupt）走 PCI 的 message address/data，最终仍会在总线上变成一发 APIC 消息。编程属于 PCI，不在本篇；本仓库也还没有这条路径。

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
| 仅 APIC | **先** `arch_irq_type=xAPIC_IRQ` + `disable_PIC` → MADT 基址须 **`== 0xFEE00000`** → map LAPIC → enable → LDR → `reset_APIC` |
| 无 APIC | `PIC_IRQ`：`init_PIC()` |

MADT 地址若非 FEE00000，或 `map_LAPIC` 失败：xAPIC 路径 **直接 return**——此时 **`arch_irq_type` 已是 `xAPIC_IRQ`、PIC 已 mask，无 PIC 回退**（半初始化缺口；Doxygen 已写）。**不**跟 override 重映射。

### 8.3 一条 timer 中断的端到端

```text
LAPIC 计数到 0（或 TSC deadline）
  → LVT Timer 向量 0x20
  → IDT[0x20] → trap_entry → trap_handler
  → timer ISR（IRQ_NEED_EOI）
  → arch_eoi_irq → APIC_EOI
  → 若用户来源 → schedule
```

EOI 分支差异（实现细节）：PIC 路径 `PIC_EOI(向量号)` 区分主/从片；从片路径 **先从后主** 各写一次 OCW2。APIC 路径只向 EOI 寄存器写 0，硬件自己清最高 ISR 位。
---

## 9. 公开 API

本篇拥有：x86_64 中断控制器原语——`init_irq` / `arch_irq_type`、8259（`PIC.h`）、Local APIC（`LocalAPIC.h`：SVR / EOI / ICR / LVT timer 后端）、8254 PIT（`PIT.h`）。说明改写自上述头文件 Doxygen（已与 `.c` 核对）。`IOAPIC.h` **空壳，无 API**。

**本篇不拥有：** `irq_vector_*` / `register_irq_handler` / `trap_handler` → `25`；`arch_eoi_irq` 声明与分发约定 → `25`（实现调本篇 `PIC_EOI` / `APIC_EOI`）；可移植 `rendezvos_time_*` → `33`；软 IPI 协议 → `30`；MADT 枚举 → `35`。

上层设备应走 `25` 的 alloc+register，**勿**假定 IOAPIC 路由。

### 9.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 每核 | `init_interrupt`（reserve+lidt）→ **`init_irq`** → `smp_ipi_init` → … → `sti` → **`rendezvos_time_init` → `software_enable_APIC`**（APIC 路径） |
| APIC 选型 | 见 `init_irq`：x2APIC / xAPIC（基址须 `== 0xFEE00000`）/ PIC |
| Timer ISR 就绪 | 先 `register_irq_handler(0x20, …)`，再写 LVT Timer / 开 SVR |
| IPI | 先 register `0x30`，再 `APIC_send_IPI`（协议见 `30`） |
| EOI | 由 `trap_handler` 经 `arch_eoi_irq` 调本篇；ISR 内勿自写 |

### 9.2 选型

```c
enum IRQ_type { NO_IRQ, PIC_IRQ, xAPIC_IRQ, x2APIC_IRQ };
extern enum IRQ_type arch_irq_type;
void init_irq(void);
```

| 接口 | 说明 |
|------|------|
| `init_irq` | 一次选型 + 使能/reset。**不**调 `software_enable_APIC`。xAPIC：先置 type + `disable_PIC`；MADT 基址 ≠ `0xFEE00000` 或 map 失败则 return——**无 PIC 回退**。 |
| `arch_irq_type` | 之后只读；驱动 `APIC_RD/WR_REG` 与 EOI 分支。半失败路径也可能已是 `xAPIC_IRQ`。 |

### 9.3 8259 PIC

```c
void init_PIC(void);
void disable_PIC(void);
error_t enable_PIC_IRQ(int irq_num);
error_t disable_PIC_IRQ(int irq_num);
error_t PIC_EOI(int irq_num);
```

| 接口 | 说明 |
|------|------|
| `init_PIC` | ICW1–4；主基址 `0x20`、从 `0x28`；手动 EOI；主 IMR=`0xFB`、从=`0xFF`。 |
| `disable_PIC` | 全 mask；切 APIC 时调用。 |
| `enable/disable_PIC_IRQ` | 参数是 **IDT 向量**（`0x20`…），不是「线号 0–15」。从片 `enable` **不**额外放开主片 IRQ2（init 时主 IMR=`0xFB` 已放开 cascade）。 |
| `PIC_EOI` | 向对应片写 OCW2。从片向量：**先从后主**（清 cascade IRQ2）。 |

### 9.4 Local APIC

```c
bool map_LAPIC(void);
void enable_xAPIC(void) / enable_x2APIC(void);
void reset_APIC(void); void reset_xAPIC_LDR(void);
void software_enable_APIC(void);
void APIC_EOI(void);
void APIC_send_IPI(u8 dest, u32 dest_sh, u32 trig, u32 level,
                   u32 dest_mode, u32 del_mode, u32 vector);
```

| 接口 | 说明 |
|------|------|
| `map_LAPIC` | 仅 xAPIC：`0xFEE00000` → UNCACHED 内核映射。 |
| `reset_APIC` | mask Timer/LINT0，PERF→NMI，TPR=0；**LINT1 未写**。 |
| `software_enable_APIC` | SVR：`SW_ENABLE` + spurious=`0x27`。在 **timer init** 路径调用。 |
| `APIC_EOI` | EOI 寄存器写 0（不带向量）。 |
| `APIC_send_IPI` | xAPIC：**先 HIGH 后 LOW**；x2APIC：一次 MSR。软 IPI 向量=`0x30`。`dest` 参数为 **u8**（ICR[63:56]）；x2APIC 32-bit dest 的 [55:32] 保持 0。 |

Timer 后端（`APIC_timer_calibration` / `init` / `read` / `reset` / `hz`、`TSC_timer_calibration`）：供 `arch_init_timer` 使用；可移植语义见 `33`。校准用 `PIT_mdelay`。

### 9.5 8254 PIT

```c
void PIT_mdelay(int ms);          /* 校准忙等；单次约 ≤25ms */
void PIT_update_timer(u16 step);  /* PIC 时钟路径 */
u64 PIT_timer_read(void);
u64 PIT_get_hz(void);             /* PIT_TICK_RATE = 1193181 */
```

### 9.6 IOAPIC

**无。** `IOAPIC.h` 空；MADT IOAPIC 条目不消费。

---


## 10. 多架构

仅 x86_64。aarch64 → GIC 篇。

---

## 11. 测试

间接：timer tick、SMP 启核、软 IPI。本篇未复测。

---

## 12. 限制与后续 / 已知缺口

- **IOAPIC 空壳**；MADT IOAPIC 未消费 → 无规范 PCI IRQ。
- **xAPIC MADT/map 失败半初始化**：`arch_irq_type` 已 xAPIC、PIC 已 disable，无回退。
- **x2APIC ICR** 定向目的地可疑（`dest_field` 仅 u8 → [63:56]；[55:32] 恒 0）。
- **`reset_APIC` 不写 LINT1**；`disable_APIC` 清 bit 用了 `&` 组合（逻辑可疑）。
- Spurious 无 handler。
- 从片 `enable_PIC_IRQ` 不补 unmask 主 IRQ2（init 时主 IMR=`0xFB` 已放开 cascade）。
- ISR / IRR 调试 API 可能对 RO 寄存器无效。
- HPET 校准未接。

---

## 13. 变更记录

- 2026-10-02：§5.4 补 ARAT（CPUID.06H）说明；注明探测函数存在但选型未分支。
- 2026-10-01：`PIC_EOI` 从片路径改为先从后主；去掉「不连带主片」缺口表述。
- 2026-10-01：再补 PIC ICW/OCW/IMR·IRR·ISR、「先从后主」EOI；LAPIC MMIO 速查表、LVT 位域、向量 0–15 非法、DCR/校准与 TSC-deadline 顺序、TPR 与向量号优先级的关系、INIT–SIPI 与软 IPI 的区分。
- 2026-10-01：对照 `docs/old/interrupt.md` 扩写 Local APIC 寄存器组——APIC ID / VERSION、TPR·PPR 与接收门槛、IRR/ISR 双缓冲与嵌套、TMR 与 EOI 广播、SVR spurious 成因、ICR 逻辑目的地 / SIPI、x2APIC 与 xAPIC 的寄存器差异；补边沿/电平与 PIC/APIC 的衔接。
- 2026-09-27：中文措辞整理——「软件使能 (soft-enable)」对照；bring-up→启核；弱化「钉死 / 真源 / 契约」堆砌；IOAPIC 空操作标 (no-op)。
- 2026-09-26：对照 `IRQ.h` / `IRQ.c`——写清 xAPIC MADT/map 失败时 **type 已是 xAPIC、PIC 已 disable、无回退**；从片 `enable_PIC_IRQ` 不补 unmask 主 IRQ2。
- 2026-09-26：§9 全文审阅——补 IRQ/PIC/LocalAPIC/PIT Doxygen；写清 init_irq→SVR 时序、ICR 先高后低、从片 EOI 缺口；划清 vs `25`/`30`/`33`；IOAPIC 明确无 API。
- 2026-09-25：对照源码补遗漏——`APIC.h` 伞头、`rtc.c` 非 IRQ、`arch_eoi_irq` PIC/APIC 差异；大幅扩写 8259/LAPIC 硬件与 OS 依赖。
- 2026-08-29：整篇重做——三选一；init 时序；IOAPIC 空壳；缺口表。
- 2026-08-27：初稿（偏浅）。
