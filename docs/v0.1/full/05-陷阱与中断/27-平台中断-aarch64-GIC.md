# 平台中断：aarch64 GIC

v0.1 · 2026-10-02

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
| 16–31 | **PPI** | Private Peripheral Interrupt | **每核私有**（timer 走某条 PPI，**不**硬编码；见 `arch_get_timer_irq_num`） |
| 32–1019 | **SPI** | Shared Peripheral Interrupt | 板级 / 外设共享线；可配目标核（ITARGETSR affinity） |

**纠正：** Timer 是 **PPI**，不是 SPI。QEMU virt 上常见 INTID **30**（ns-phys），但软件以 DTB `arm,armv8-timer` 的 **第 2 组** interrupts 为准——勿写死宏。

1020–1022 保留；**1023** 是 spurious（读 IAR 得到它表示「此刻没有可交给你的有效中断」）。SGI 是边沿触发；PPI/SPI 可配边沿或电平。手册还把投递分成 **1-N**（多核都看见 pending，但只有一个 CPU 真正 ack，其余再读 IAR 会拿到 1023）和 **N-N**（多个 CPU 都可以各自处理）。本仓库 SPI 初始化写成 edge + 1-N。

GICv2 最多按 8 个 core 来想 SGI 的 CPUID 字段；同一 SGI 号可以同时有多对「源核→目标核」pending，靠 **INTID + 源 CPUID** 区分，而不是靠再开一套 INTID。

### 1.3 四种状态（后面读 IAR / EOIR 都站在这张图上）

一条 INTID 在 GIC 里不是「来了 / 没来」两态，而是：

```text
inactive  ──到达──►  pending  ──读 IAR──►  active
                         ▲                    │
                         │                    │ 处理中又来一枪
                         │                    ▼
                         │            active and pending
                         │                    │
                         └──── 写 EOIR / 优先级被丢掉 ────┘
```

- **inactive**：没有活着的请求，或已经处理完。
- **pending**：已经记下，还没交给 CPU。
- **active**：本核已经 ack（读过 IAR），ISR 还在跑。
- **active and pending**：正在服务，同号又来了一枪。

「结束」其实是两步，容易混：

1. **优先级下降（priority drop）**：告诉接口「当前这档 running priority 可以放下了」。
2. **deactivate**：真正把这条 INTID 从 active 拿掉。

`GICC_CTLR.EOImode` 决定两步是否捆在一次写 EOIR 里。本仓库走**合并路径**，只写 EOIR，不用 DIR。分离模式下：写 EOIR 只做 drop，还要按与读 IAR **相反的顺序**写 `GICC_DIR`。

### 1.4 OS 对硬件的硬依赖

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

**驱动** — alloc 得 trap id 后，对 SPI：`intid = trap_id - 64`，再 `gicd_v2_unmask_irq` / `set_affinity`；勿把手写 EC 当设备号。

---

## 4. 硬件深度：Distributor（GICD）

GICD 是**全局一份**的配置面：哪条 INTID 开着、优先级多少、SPI 打给哪些核、SGI 往哪发。它**不**应答中断——ack / EOI 在每核的 GICC。手册把同一套使能/pending/active 寄存器做成 **bank**：SGI/PPI 每个 CPU interface 各有一份副本，SPI 才是真正的共享配置。读「同一个 INTID」时，要想清楚看的是哪份 bank。

Group0 / Group1（`GICD_IGROUPR`）在有 Security 的实现里对应 Secure / Non-secure；无 Security 的 virt 上常常塌成「开没开」——见 §5.4。本仓库 dist init 开的是 Group0 那一位。

### 4.1 寄存器组：先认功能，再对号入座

按「OS 实际会碰到的问题」分组，而不是按 MMIO 偏移背表。

**开关与容量**

| 寄存器 | 干什么 |
|--------|--------|
| **GICD_CTLR** | 整块 Distributor 的转发总闸。改使能位之前应先关掉转发，配完再开。本仓库 init 先写 0，最后开 Group0。 |
| **GICD_TYPER** | `ITLinesNumber` 等：能推出「有多少个 ISENABLER 字」、SPI 上界（还要和 INTID 1019 取小）。probe/init 用来决定循环扫到哪。 |

探测「哪些 INTID 真的存在」的手册套路：关 CTLR → 对 ISENABLER 写 `0xffffffff` 再读回（不支持的位 RAZ/WI，支持的常表现为 RAO/WI）→ 再用 ICENABLER 看哪些位被永久占用 → 开 CTLR。本仓库没走这套探测，而是信 TYPER + 固定 INTID 分段。

**使能（enable ≠ pending）**

ISENABLER / ICENABLER / ISPENDR / … 都是 **每 32 个 INTID 占一个 32 位字**：字 `n` 管 INTID `32*n … 32*n+31`。第一个字里 bit0–15 是 SGI，bit16–31 是 PPI；SPI 从第二个字起。

| 寄存器 | 干什么 |
|--------|--------|
| **GICD_ISENABLER** | 位置 1 = 允许这条 INTID 从 Distributor 转出去。 |
| **GICD_ICENABLER** | 对应位清使能。 |

改这两个**不会**把已经 pending 的请求抹掉：mask 之后那一枪可能还挂着，unmask 时会再打过来。参数永远是 **INTID**，不是 trap id。

有 Security 时，SGI **0–7** 常留给 Non-secure，**8–15** 留给 Secure（实现相关）。本仓库 IPI 用 **SGI 0**，落在 Non-secure 常用区。

**Pending / Active（软件改状态）**

| 寄存器 | 干什么 |
|--------|--------|
| **GICD_ISPENDR / ICPENDR** | 置 / 清 pending。边沿：写 ISPENDR 等于软件插一枪；电平：线还有效时清 ICPENDR 往往马上被硬件再置回来，写 ISPENDR 也不一定能「代替」那根线。 |
| **GICD_ISACTIVER / ICACTIVER** | 看 / 改 active。pending 且 active 时两边都会亮。 |
| **GICD_SPENDSGIR / CPENDSGIR** | SGI 专用的 pending 位图（按源 CPU 展开）。 |

对 **SGI** 写普通的 ISPENDR/ICPENDR **没有生成效果**（读还能看见状态）；要发 SGI 得写 **SGIR**，或改 SPENDSGIR/CPENDSGIR。

**优先级、路由、边沿**

| 寄存器 | 干什么 |
|--------|--------|
| **GICD_IPRIORITYR** | 每 INTID **一字节**（一个 32 位字管 4 条）。越小越优先。实现最少 16 级、最多 256 级；用不满 8 bit 时低位 **RAZ/WI**（写了再读回来，低位会变 0——可用来探测实际宽度）。平台也可以把某些 INTID 做成只读优先级。本仓库 init 写 `irq/8`，实现是 **`\|=`**，写 0 可能是空操作 (no-op)。 |
| **GICD_ITARGETSR** | SPI 目标 CPU **位图**，每 INTID 一字节（一个字管 4 条）。bit0 = CPU0，bit1 = CPU1，… GICv2 常见实现最多 8 核，高位可能 RAZ。**仅 SPI 可写**；SGI/PPI 的目标字节是只读镜像。多 bit 置位 = 这份 SPI 可以打到多个 CPU interface（再叠加 1-N / N-N）。 |
| **GICD_ICFGR** | 每 INTID **2 bit**（一个字管 16 条）。对 SPI/PPI：常见编码是 bit[1]=1 表示边沿、=0 表示电平（bit[0] 与 1-N 等模型位实现相关——本仓库 SPI 写成 edge + 1-N）。**SGI 的配置位只读**，硬件规定为边沿。 |
| **GICD_IGROUPR** | 每 INTID 一 bit：进 Group0 还是 Group1。 |
| **GICD_SGIR** | 写即发 SGI，字段大意： |

```text
GICD_SGIR（写）：
  [3:0]   SGIINTID     要发的 SGI 号 0–15
  [15:8]  CPUTargetList 目标位图（TargetList filter = 0b00 时才用）
  [25:24] TargetListFilter
          0b00 = 用上面的 list
          0b01 = 除自己外所有
          0b10 = 仅自己
          0b11 = 保留
  其余位  NSATT 等（Secure 相关；virt 常可忽略）
```

SGI 的 pending 要用「INTID + 源 CPU + 目标 CPU」才能说清楚：两个核可以同时向第三核打**同一个** SGI 号，GIC 仍能分开记；**同一个 CPU 不能**对同一 INTID 叠两发还没被 ack 的相同源。读 IAR 时会带回源 CPUID（IAR[12:10]，GICv2 常见 3 bit → 最多区分 8 个源），EOIR 必须写回去——这就是 trap_info 里 `TRAP_SET_CPU` 的硬件来由。每个目标核上，同一 SGI 号还可以有**不同**的优先级字节。

### 4.2 `init_distributor` 在做什么

```text
GICD_CTLR = 0
读 TYPER → irq_num / cpu_num
对每个 SPI:
  set_type(edge | 1-N)
  set_affinity(…, 0)     // C 注释写「打到 core0」；实际 |=0 → **空操作 (no-op)**
  set_priority(irq/8)
  mask_irq
GICD_CTLR = Group0 enable
```

**不**在 dist init 里动 SGI/PPI——那些在每核 `init_cpu_interface`。

**`set_affinity(irq, 0)` 并不会绑到 CPU0。** 实现是 `ITARGETSR |= cpu_id_mask`：mask 为 0 时 OR 零，寄存器不变（见 Doxygen / `gic_v2.c`）。因此 `init_distributor` 循环里的这次调用是空操作 (no-op)，不会把 SPI 强制路由到 core0。要绑核须传非零位图（如 `1 << cpu`）；`cpu_id_mask >= 0xff` 则直接 return。

### 4.3 SGIR 与 IPI

写 **GICD_SGIR**（字段见 §4.1）即注入一发 SGI。和 x86 写 ICR 不同：这里**没有** Delivery Status 轮询——写完就当发出去了；目标侧能不能立刻 ack，取决于对方 CPU interface 的 PMR / running priority / DAIF。

本仓库软 IPI / SMP 门铃用 **SGI 0** → trap id **64**。协议（pending 位、generation）见软 IPI 篇；本篇只提供「硬件把 SGI 砸到目标 CPU interface」。

同一目标核上，若上一发同号同源还没 EOI，再写 SGIR **不会**再叠一发 pending（见 §1.2 / §4.1）——软 IPI 协议因此必须自己做 generation / pending 位，不能假设「写多少次 SGIR 就进多少次 ISR」。

### 4.4 Affinity 限制（硬件）

- **SPI**：可写 ITARGETSR，决定哪些 CPU 能收到。
- **SGI / PPI**：**不能**靠 ITARGETSR 绑核（对 PPI/SGI 那些字节是 RO）。PPI 天然「只打到本核」；SGI 的目标由 SGIR 的 filter/list 决定。

因此，「给 timer 设 affinity」在 GICv2 上对 PPI **无意义**——每核自己的那条 PPI 只进本核 GICC。

对 SPI：`set_affinity(intid, 0)` 同样是空操作 (no-op)（见 §4.2）；`init_distributor` 里那一轮循环不改变 ITARGETSR 的复位值。

---

## 5. 硬件深度：CPU Interface（GICC）

GICC 是**每核一份**的「取号窗口」：Distributor 把中断转到某个 CPU interface 之后，由这颗核自己读 IAR、比优先级、写 EOI。寄存器组也是 banked 的——每个核看到的是自己那一份 PMR / IAR / running priority。

手册还提到：若 CPU interface 的 IRQ/FIQ 信号被关掉，legacy 的 IRQ/FIQ 脚可能**旁路**直达 CPU（`GICC_CTLR` 上有一组 BypDis 位用来关旁路）。QEMU virt + 本仓库不靠这条路，读手册时别和「读 IAR」混在一起。

### 5.1 优先级门槛：PMR、BPR、能不能嵌套

先回答「这条 pending 会不会真的打到 CPU」：

- **GICC_PMR（Priority Mask）**：阈值。只有优先级**数值更小（更紧急）**、严格优于这个阈值的才会被考虑。比较时**不看**优先级分组。复位常为 0，等于什么都不收。本仓库写成 **`0xff`**：门槛放到最低，尽量全收。
- **GICC_BPR（Binary Point）**：把 8-bit 优先级切成 **group priority | subpriority**。抢占只看 group：同一 group 里即使 subpriority 不同，也视为同一档 **preemption level**，这一档最多一个 active。要嵌套，必须同时满足：新中断优于 PMR，且 **group 优先级高于**当前 CPU interface 的 running priority。
- GICv2 可以有两套 BPR（Secure / Non-secure），由 `GICC_CTLR.CBPR` 决定用哪套。本仓库只写 **BPR = 3**，没有按 Security 切换。

**BPR = 3 粗读：** binary point 指出「从哪一位开始算 subpriority」。取值越大，group 占用的高位越少、可区分的抢占档越粗。BPR=3 是本仓库的固定选择，不是「魔法最优」——换平台若要更细嵌套，才需要重算。读 IHI0048 表 3-2 可以对着具体实现核对切分。

还有只读的 **GICC_RPR（Running Priority）**：当前 CPU interface 认为自己正在跑的那档优先级。嵌套比较的是 **group**，对抗的是 running priority，而不是把整个 8-bit 优先级当整数硬比。

**GICC_HPPIR**：只读，当前这个 CPU interface 上「最高优先级、已经 pending 的」INTID，**读它不会 ack**。和 IAR 的差别就在「看一眼」还是「领走」。本仓库 ISR 路径不读 HPPIR。

和 x86 APIC 对照：APIC 用向量号自己的 `[7:4]` 当天生 class；GIC 用独立的 `IPRIORITYR` 字节 + BPR 切 group。所以 GIC 上「改优先级」是写 Distributor，不是换 INTID。

### 5.2 应答与结束：IAR、EOIR、DIR、CTLR

按一次 IRQ 的时间顺序：

1. 读 **GICC_IAR**：ack。返回值布局（GICv2）大致是：
   ```text
   [9:0]  InterruptID   （0–1019；1023=spurious）
   [12:10] CPUID        （仅 SGI 有意义：哪个核发来的）
   ```
   状态从 pending 变成 active（或 active and pending）。本仓库的 `union irq_source` 与 `TRAP_SET_CPU` 就是按这张布局打包的。
2. **电平触发**时：设备没把线拉下来，pending 会马上再立起来——ISR 里通常还要处理设备寄存器，不能指望「只读一次 IAR」就让线安静。边沿则一枪一次，除非再来边沿。
3. 读 IAR 也可能直接得到 **1023（spurious）**：PMR 抬上去了、1-N 模型里别的核已经领走、或者此刻没有可交的中断。spurious **不必写 EOIR**，更不能拿 1023 去当设备号分发（本仓库会映射成巨大 trap id，容易进 unknown）。**当前实现也尚未在 `get_curr_el_trap_info` 里专门识别 1023**——属缺口。
4. ISR 结束写 **GICC_EOIR**：应写回与 IAR **相同**的 INTID（SGI 还要同一 CPUID）。合并 EOI 模式下这一下同时做 priority drop 和 deactivate。
5. **GICC_DIR**：仅分离模式。写 DIR 的顺序必须与读 IAR **相反**（栈式），才能对上「最近一次 ack 的那条」。本实现 **不用 DIR**。

**GICC_CTLR** 里和结束/分组相关的位：

- **EnableGrp0 / EnableGrp1**：开哪一组。本仓库 CPU IF 写的是 Group1 enable 宏（与 dist 的 Group0 命名不一致，见 §5.4）。
- **EOImode**：drop 与 deactivate 是否拆开。
- **AckCtl**：为 0 时 Group0 / Group1 各有一套应答寄存器（IAR/EOIR/HPPIR vs AIAR/AEOIR/AHPPIR）。ARM 建议置 0。本仓库只走 IAR/EOIR。
- **FIQEn** 以及一组 **IRQ/FIQ BypDis**：和旁路、FIQ 路由有关，查 IHI0048 表 2-2 / 2-3；本仓库未按 Secure 世界去配。

**GICC_APR**：保存 active priority 位图，和电源管理 / 嵌套深度有关。当前驱动不碰。

本仓库链路：

```text
VBAR IRQ → get_curr_el_trap_info
  source = gic.read_irq_num()          // 读 IAR
  tf->trap_info = TRAP_SET_CPU(intid + 64, CPUID)
→ trap_handler → ISR
→ NEED_EOI → arch_eoi_irq
  从 TRAP_ID / TRAP_GET_CPU 还原 irq_source → gic.eoi  // 写 EOIR
```

IRQ 路径用 `TRAP_SET_CPU` 把 **INTID(+64)** 与 **IAR.CPUID** 一并打进 `trap_info`（`AARCH64_TRAP_CPU_*` 与 IAR 同布局）；`arch_eoi_irq` 再还原后写 EOIR，满足 SGI 对源 CPU ID 的要求。

### 5.3 `init_cpu_interface` 在做什么

```text
SGI/PPI: set_priority, unmask, set_type (SGI edge / PPI level)
GICC_BPR = 3
GICC_PMR = 0xff
GICC_CTLR = ENABLE_GROUP1
```

于是 **timer 所用 PPI、IPI SGI0 在硬件上已能到达本核**；软件还须 `register_irq_handler`（timer 经 `arch_get_timer_irq_num`）才不会 panic。

### 5.4 Group0 / Group1 与 Security

GICD 开 **Group0**、GICC 开 **「Group1」** 宏位——在**无 Security 扩展**的 GICv2（QEMU virt 常见）上，这些 bit 的语义常塌成单一 Enable。**virt 上可能仍工作**；不要解读成「已正确区分 Secure / Non-secure Group」。真 Secure 世界要用另一套编程（两套 IAR/EOIR、两套 BPR、SGI 0–7 vs 8–15 的约定），本仓库未做。

读手册时若看到 **GICC_AIAR / AEOIR / AHPPIR**，那是 Group1 的并行应答口；我们全程只用非 A 前缀那一套。

---

### 5.5 和 x86 Local APIC 的一张对照（帮助记）

| 问题 | x86 Local APIC | GICv2 |
|------|----------------|-------|
| 板级外设怎么进核 | 本该 IOAPIC → LAPIC（本仓库 IOAPIC 空） | GICD 配 SPI + ITARGETSR → 各核 GICC |
| 核间门铃 | ICR（向量号） | GICD_SGIR（SGI 号 + filter） |
| 应答 | 向量已在 IDT 投递里；软件写 EOI=0 | **必须读 IAR** 才知道 INTID |
| 结束 | EOI 清最高 ISR | EOIR 写回 INTID[+CPUID] |
| 优先级 | 长在向量号 `[7:4]` | `IPRIORITYR` 字节 + BPR 切 group |
| 门槛寄存器 | TPR / CR8 | PMR |
| Spurious | SVR 里编程的向量（本仓库 `0x27`） | IAR = 1023 |
| 每核私有定时器 | LVT Timer | PPI（DTB ns-phys；virt 常为 30） |

## 6. 数据结构与不变量（软件侧）

### 6.1 INTID ↔ trap（必记）

| 用途 | INTID | trap id |
|------|-------|---------|
| IPI | SGI **0** | **64** |
| Timer | DTB ns-phys PPI（virt 常 **30**） | `arch_get_timer_irq_num`（常 **94**） |
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
rendezvos_time_init()              // arch_get_timer_irq_num → register
```

x86 是先配控制器再 `sti`；aarch64 **先开 IRQ 再配 GICC**——启动窗口可疑，文档诚实写出。窗口内若有 pending，可能在 CPU IF 未就绪时进入 IRQ 入口。

### 8.3 一条 IRQ 的端到端

```text
外设/PPI/SGI 到达 GIC → 目标 CPU 的 IRQ 线拉高
  → VBAR IRQ 入口（DAIF.I 已屏蔽）
  → 读 GICC_IAR → INTID[+CPUID]
  → trap_info = TRAP_SET_CPU(INTID+64, CPUID)   // SGI EOI 需要 CPUID
  → trap_handler → ISR
  → arch_eoi_irq → 还原后写 GICC_EOIR
  → 用户来源则 schedule → eret
```

FIQ 分支解码为空（Trap 篇）——勿指望 FIQ 路径。

### 8.4 与 arch timer 的接线

Generic timer 在 EL1 编程 **CNTP_***（non-secure physical）；到期拉高 DTB 约定的那条 PPI。软件侧：`arch_get_timer_irq_num` 在 BSP 上读 `arm,armv8-timer` 的 **ns-phys** 组，缓存 trap id；AP 复用缓存。同节点常见顺序：secure phys → ns-phys → virt → hyp——**只取第 2 组**。QEMU virt 上 ns-phys 多为 INTID 30（trap 94），换板以 DTB 为准。

---

## 9. 公开 API

本篇拥有：aarch64 GICv2 驱动——全局 `struct gic_v2 gic` 及其 ops、INTID 分段宏 / `gic_v2_is_*`、`union irq_source`。说明改写自 `gic_v2.h` Doxygen（已与 `gic_v2.c` 核对）。`gic_v3.h` **仅占位，无独立 API**。

**本篇不拥有：** `irq_vector_*` / `register_irq_handler` / `trap_handler` → `25`；`arch_eoi_irq` / `get_curr_el_trap_info` 约定 → `25`/`23`（实现调 `gic.read_irq_num` / `gic.eoi`）；软 IPI 协议 → `30`；DTB 解析 → `36`；可移植 timer → `33`。

上层设备：`25` alloc+register 后，对 SPI 用 **INTID**（`trap_id - 64`）调 `gic.unmask_irq` / `set_affinity`。

### 9.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| BSP 平台一次 | DTB 就绪 → **`gic.probe`** → **`gic.init_distributor`**（+ `psci_init`，见 `39`） |
| 每核 | **`init_interrupt`**（VBAR + **已 enable IRQ**）→ **`gic.init_cpu_interface`** → `smp_ipi_init` → `rendezvos_time_init` |
| 设备 SPI | `irq_vector_alloc` → `register_irq_handler` → **`gic.unmask_irq(intid)`**（可选 `set_affinity`） |
| IRQ 运行时 | VBAR → `read_irq_num`(IAR) → `trap_id=intid+64` → handler → **`eoi`(EOIR)** |

与 x86 相反：本平台 **先开 IRQ 再配 GICC**（启动窗口，见 §8.2）。

### 9.2 INTID 与全局对象

```c
#define GIC_V2_SGI_START/END   /* 0–15 */
#define GIC_V2_PPI_START/END   /* 16–31；timer INTID 由 DTB ns-phys 决定 */
#define GIC_V2_SPI_START/END   /* 32–1019；pool trap 96–1083 */
bool gic_v2_is_sgi/ppi/spi(u32 irq_num);

extern struct gic_v2 gic;  /* compatible = "arm,cortex-a15-gic" */
```

| 符号 | 说明 |
|------|------|
| `compatible` | 硬编码；DTB 不匹配则 probe 失败且 **不**填 `gicd`/`gicc`。 |
| MMIO | `probe` 用 `PAGE_ENTRY_DEVICE` 映射；勿当普通缓存页。 |

### 9.3 `gic` ops

```c
gic.probe();
gic.init_distributor();
gic.init_cpu_interface();
gic.unmask_irq / mask_irq(u32 intid);
gic.set_type / set_priority(u32 intid, …);
gic.set_affinity(u32 intid, u32 cpu_id_mask);  /* SPI only */
gic.send_sgi(u32 sgi, u32 target_mode, u32 target_list);
union irq_source gic.read_irq_num(void);
gic.eoi(union irq_source);
gic.pending_clr(u32 intid);
```

| 接口 | 说明 |
|------|------|
| `probe` | 找节点 → `reg` 四元组 → DEVICE map → 设指针。失败只打印。 |
| `init_distributor` | 只配 **SPI**：edge、`set_affinity(…,0)`（**空操作 (no-op)**）、priority、**全 mask**；开 GICD Group0。 |
| `init_cpu_interface` | **unmask 全部 SGI+PPI**；PMR=`0xff`、BPR=`0x3`；开 GICC Group1。 |
| `unmask` / `mask` | 写 ISENABLER / ICENABLER。参数是 **INTID**，不是 trap id。 |
| `set_affinity` | **仅 SPI**；SGI/PPI 直接 return。写法是 `ITARGETSR \|= mask`：**mask=0 为空操作 (no-op)**，不会强制绑到 CPU0；`mask ≥ 0xff` 拒收。 |
| `set_priority` | `|=` 写入——写 0 可能是空操作 (no-op)（缺口）。 |
| `send_sgi` | 写 SGIR；非 SGI 忽略。软 IPI 用 SGI **0**（见 `30`）。 |
| `read_irq_num` | 读 IAR（应答）；返回 INTID+CPUID。 |
| `eoi` | 写 EOIR（回写 INTID[+CPUID]）。`arch_eoi_irq` 从 `TRAP_ID` / `TRAP_GET_CPU` 还原后调用。 |

### 9.4 驱动 checklist（SPI）

1. `irq_vector_alloc(&trap_id)`（池内 ≥96）。
2. `register_irq_handler(trap_id, isr, IRQ_NEED_EOI)`。
3. `intid = AARCH64_TRAP_ID_TO_IRQ(trap_id)` → `gic.unmask_irq(intid)`；需要绑核再 `set_affinity`。
4. EOI 交给 `trap_handler`；勿在 ISR 内自行 `gic.eoi`，除非明确绕过 `IRQ_NEED_EOI`。

---


## 10. 多架构

仅 aarch64。x86 → APIC / PIC 篇。

---

## 11. 测试

间接：timer、SMP IPI、virt 启动。本篇未复测。

---

## 12. 限制与后续 / 已知缺口

- GICv3 / ITS / MSI 无。
- compatible 硬编码；与 DTB 举例可能不一致。
- 开中断早于 CPU IF。
- **未识别 IAR=1023 spurious**：会当成巨大 trap id 往下走。
- **`AARCH64_TRAP_ID_MASK = 0x2FF`（767）** 与 pool 上界 trap **1083** 不一致：`TRAP_ID(tf->trap_info)` 会截断高 SPI（intid ≳ 703）——高编号外设向量在软件分发上不可靠，属实现缺口。
- Timer INTID：**不**写死；`arch_get_timer_irq_num` 读 DTB ns-phys（virt 上常为 30）。
- SPI 默认全 mask；`set_affinity(…, 0)` 与 priority 写 0 在 `|=` 语义下均为空操作 (no-op)（init 不强制 CPU0）。
- Group0 / Group1 命名在无 Security 时塌缩。

---

## 13. 变更记录

- 2026-10-02：对齐现行代码——timer 改为 DTB ns-phys 探测（不再写死 PPI30）；§8.3 端到端补上 `TRAP_SET_CPU` / EOIR 还原。
- 2026-10-01：再补 GICD 字布局 / SGI 0–7 vs 8–15、ICFGR·ITARGETSR·SGIR 字段、SGIR 不叠 pending 与软 IPI 的关系、BPR=3 / RPR、IAR 位域与未处理 1023 缺口、GIC↔APIC 对照表。
- 2026-10-01：对照 `docs/old/interrupt.md` 扩写 GICD/GICC——四种状态与 EOI 两阶段、1-N/spurious、bank、使能/pending/active/SGIR、优先级与 PMR/BPR 抢占、IAR 电平语义、CTLR.EOImode/AckCtl/旁路；保留原有 init 与 trap_info CPUID 打包说明。
- 2026-10-01：补上 IAR.CPUID → `trap_info[12:10]` 打包与 `arch_eoi_irq` 还原；关闭 SGI EOI 缺口（实现此前漏接已有 `AARCH64_TRAP_CPU_*` 设计）。
- 2026-09-27：中文措辞整理——affinity 句通顺化；统一「空操作 (no-op)」；弱化「真源 / 契约 / 钉死」堆砌。
- 2026-09-26：对照 Doxygen / `gicd_v2_set_affinity`——纠正「affinity(0) 打到 core0」；写清 `|=0` 为空操作 (no-op)、不强制 CPU0；§4.2 / §9.3 / §12 同步。
- 2026-09-26：§9 全文审阅——`gic_v2` ops / `irq_source` Doxygen；写清 probe→dist→（每核）CPU IF 与「先开 IRQ」窗口；SPI checklist；划清 vs `25`/`30`/`33`；`gic_v3` 占位说明。
- 2026-09-25：大幅扩写硬件——GICD/GICC 寄存器、IAR/EOIR 协议、INTID 分段、SGIR、affinity 限制、OS 依赖清单与端到端链；手册入口 IHI0048 / ARM ARM。
- 2026-08-29：整篇重做——intid+64；PPI30≠SPI；开中断时序；EOI/CPUID；缺口表。
- 2026-08-27：初稿（偏浅）。
