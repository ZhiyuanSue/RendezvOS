# IRQ 向量分配与处理

v0.1 · 2026-10-23

本篇讲三件事怎么叠在一起，以及**各自管到哪里为止**：

| 一层 | 管什么 | 主要代码 | 本篇篇幅 |
|------|--------|----------|----------|
| **编号空间（irq_space）** | 两套软件编号、本核表 / space `priv[]`、占有与释放、**线控 ops**（enable/disable/eoi） | `kernel/trap/irq_space.*`、`irq_space_common.h`、`arch/*/trap/irq_space*.c` | **主体** |
| **分发** | 按编号找到 handler、要不要 EOI、要不要 `schedule` | `kernel/trap/trap.c`、`trap.h` | 衔接说明 |
| **硬件控制器** | MMIO / 寄存器位域（RTE、GICD…） | APIC / PIC、GIC 等 | **寄存器细节**见 `26` / `27`；线控由 space ops 调用 |

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
3. **`trap_handler`（分发）**看这个号：小于 `NR_ARCH_LOCAL_IRQ`（即 `ARCH_IRQ_ID_ALLOC_LO`，下文 **LO**）时，查**本核** `local_irq_vector[]`；大于等于 LO 时，本核表里没有这一格——经 `irq_global_find` → `arch_irq_entry_from_local` 找到所属 space 的 `priv[]` 表项，再调 handler；
4. 若属性要求，再做 **EOI**：先 `arch_local_eoi`（CPU 侧），再可选 `space->eoi`（如 x86 IOAPIC 定向 EOI）。

**irq_space 管编号 + 线控入口**：谁占了哪个 `global_irq_id`、映到哪个 `local_irq_id`、绑在哪颗核上；`irq_enable` / `irq_disable` 经 `space->enable` / `disable` 下发。MMIO 位域仍在 `26` / `27`；分发在 `trap.c`。

两套软件编号：

- **`local_irq_id`**：每核同一套数字。小于 LO 的，本核 `local_irq_vector[]` 里有格子，编号就是下标；大于等于 LO 的没有格子，必须经 map（x86）或公式（aarch64）落到全局表。
- **`global_irq_id`**：space 跨度坐标，**不存在 entry 上**；`priv[global_irq_id - base]`。今天两 arch 的 space 都是 base 0：x86 下标 ≡ GSI；aarch64 下标 ≡ SPI index（INTID = `GIC_SPI_START + 下标`）。

全局表每一项在软件里只绑一对 `(local_irq_id, cpu_id)`。占有、换核、要不要再挂 handler，见 **§1.4**。这不是 Linux 的 `request_irq`，也不是「随便写个数字当中断号」。

### 1.2 硬件交出来的号，和软件 id 怎么对上

| | x86_64 | aarch64 |
|--|--------|---------|
| 硬件交付物 | **IDT 向量**（0–255） | **GIC INTID**（0–1019…），CPU 只看见 VBAR 的 IRQ 槽 |
| `local_irq_id` | **= 向量号** | **`intid + 64`**（前 64 槽留给 ESR.EC）；LO 以下含 EC、SGI、PPI |
| `global_irq_id` | GSI 下标（长度暂由启动时申请决定） | SPI 下标 = `INTID - SPI_START` |
| 谁写出这个号 | PIC 基址 / LAPIC LVT·SVR·ICR / IOAPIC RTE（经 `irq_enable`） | `GICC_IAR` 读出 INTID；SGI 由 `GICD_SGIR` 注入 |
| 表外编号怎么进全局表 | 每核 `local_irq_map[]`（用原子操作更新；**同号在不同核可映不同 GSI**） | 公式一一对应（无每核 map；**同号各核必映同一 SPI**） |

OS 必须保证：**硬件还会交出来的那个号，在软件表里已经有着落**（本核表内可直接 `register_irq_handler`；表外须先 `irq_global_alloc`，并且通常还要挂好 handler——迁移时 handler 可能是上次留下的，见 §1.4）。否则进 `unknown` 再 panic。

### 1.3 为什么本核表是每核一份

`DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ])`：每核一份本核表。表外编号再经本核 map（x86）或公式（aarch64）落到共享的 space `priv[]`。

硬件原因：SMP 上同一外设可能被路由到不同核（或 IPI 投递到指定核）；每核都有自己的 IDT / VBAR / GICC，入口汇编跑在**被中断的那颗核**上，查的是**本核**表（及本核 map）。若只在 BSP 上挂 handler、AP 上同一编号空着，AP 收到中断就会 panic。

`register_irq_handler(local_irq_id, cpu_id, …)` 只写**那一个** `cpu_id` 上的槽（或已占到该核的全局项）。v0.1 **没有**「同一全局中断在每核挂不同处理函数」；绑核靠软件记的 `cpu_id`，再经 `irq_enable` 写入硬件路由（GIC / IOAPIC），不是靠在每核各挂一套函数指针。

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
                 space priv[]（全机共享；下标 = gid - base）
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
                 space priv[]（全机共享；下标 = gid - base）
                 +------+------+------+-----+
                 | g[0] | g[1] | g[2] | ... |
                 +--+---+--+---+------+-----+
                    ^      ^
                    |      |
           各核的 LO       各核的 LO+1     （公式一一对应）
```

`LO = ARCH_IRQ_ID_ALLOC_LO`；`g[i]` = 所属 space 的 `priv[i - base]`。

### 1.4 谁可以动这条全局项（占有规则）

先把一件事说清楚：`IRQ_ATTR_USED` 上的原子比较交换 **只负责「抢还没人占的格子」**。两个核同时对同一条 `global_irq_id` 做 `irq_global_alloc` 时，谁抢成功谁占有。它**不是**引用计数，也管不住「别人已经 `irq_global_find` 拿到表项指针」之后你再改表——所以占有之后还要另定规矩。

**一句话：抢号可以并发；从 `irq_enable` 到 `irq_global_free` 结束，这条线上的软件改动只许属主 CPU 做。**

| 阶段 | 谁可以做 | 说明 |
|------|----------|------|
| 还未占用 | 任意核 `irq_global_alloc(编号, 目标 cpu, …)` | 启动时可以替别的核抢号；换核时也可以由旧核替新核抢号 |
| 已占用、**尚未 enable** | 允许多核改软状态 | 线仍 masked，无投递；migrate 时旧属主可为新核 `alloc` 再 `enable` |
| 已 **enable** 直至 free 结束 | 仅属主：`irq_disable` / `irq_global_free` / 全局 `register_irq_handler` / 投递 | free / register 要求 `percpu(cpu_number) == owner`；`irq_enable` **不**要求 caller==新属主（migrate：旧属主 enable） |
| 分发 | 硬件打到哪颗核，哪颗核跑 `trap_handler` → `find(本核)` | 软件假定路由目标与表里亲和一致 |

**为什么释放时不清 handler，只放下「已占用」位？**

换核时，多半还是**同一个**设备处理函数，只是从 CPU A 换到 CPU B。若释放时把 `handler` 置空、再把整份属性写成 0，新核占上号之后还得再挂一遍函数，还容易把「需要 EOI」这类标志弄丢（再次占用时只会重新打上「已占用」）。所以释放时约定是：

- 清掉投递绑定（x86：`bind_local(NULL)` 清 map）与亲和字段（`local_irq_id` / `cpu_id`）；
- **保留** `irq_handler`，以及 arch 拓扑缓存（x86：`ioapic *`；aarch64 SPI：无额外字段）；
- 属性只用 `irq_atomic_attr_try_set_unused` **清掉「已占用」**，留下「需要 EOI」等；成功后再 `nr_used--`。

换核（migrate）整段应在**旧属主**上做：`irq_disable`（内含 mask 与排空本核 local pending）→ `free` → `alloc` → `irq_enable`。道理与步骤见 **§1.7**；英文动机见 `trap.h` 文件头 `@note_start` … `@note_end`（请勿改动该段注释）。

**什么时候才要改 handler？**

典型不是换核，而是驱动生命周期：卸掉模块再装一个新的，入口函数变了。那时在**当时的属主核**上再调 `register_irq_handler`（可以先传空指针摘掉，再释放 / 卸模块；或直接挂上新函数）。`register` 不禁止空指针，就是覆盖写。

**卸模块时务必注意：** 释放故意留下旧 handler。若模块代码已经卸掉、表项里却还挂着旧指针，下一次有人只占号、忘了重新挂函数，就会打进已经不存在的代码。正确做法是：仍是属主、并且已经挡住投递时，先 `register(..., NULL, ...)`（或先挂一个仍安全的空操作），再释放，再卸模块。

**同核上 irq_space 管不到的事：** 释放或改 handler 之前，调用方应先停掉投递，避免本核正在 `trap_handler` 里用着表项时表被拆掉。跨核靠「只有属主能释放 / 能改 handler」挡住；同核仍靠这条调用约定。

### 1.5 EOI 与调度（分发层，不是 irq_space）

带「需要 EOI」时，`trap_handler` 先调 `arch_local_eoi`（x86：LAPIC/PIC；aarch64：`GICC_EOIR`），若全局项所属 `space->eoi` 非空再调一次（如 x86 IOAPIC 定向 EOI；GICv2 SPI 无额外 source EOI）。漏做 CPU 侧 EOI 时，同优先级或电平触发的线可能再也进不来（时序与共享线见 §6.2）。

用户态被打断且调度器就绪时，`trap_handler` 末尾可能 `schedule`（x86 的 `syscall` 旁路除外）。处理函数应短、不要阻塞；长活应通过 IPC 交给线程。这些都不属于 irq_space 的职责。

### 1.6 软件占了表，不等于硬件已经打开外设线

`irq_global_alloc` / `register_irq_handler` **只**改软件表。外设线还要经 trap 的 **`irq_enable(local, cpu, …)`**（按亲和 pair 找 entry → `space->enable`）写控制器并 unmask（反操作为属主上的 `irq_disable(local)`）：

- aarch64 SPI：`irq_enable` → SPI space ops → `gic.enable_spi`（触发 + 按 `cpu_id` 路由 + unmask；见 `27`）；
- x86 外设：`irq_enable` → IOAPIC `space->enable`（内部写 RTE + unmask；见 `26`）。ISO / MSI / 具体设备接线仍可能未齐。

本核本地中断（异常、IPI、timer 的 LVT / PPI 等）一般在每核启动时已经使能，不走「先占全局表再 `irq_enable`」这条路径。

Trap **不**吃 `global_irq_id` 做 enable/disable；`global_irq_id` 留在 irq_space 的 alloc/free 与 space range 路由。

### 1.7 全局外设中断怎么换核（migrate）

这一小节讲的是：**已经占着的全局外设中断**（某个已注册 `irq_space` 里的 entry），想把软件亲和（以及随后的硬件路由）从 CPU A 换到 CPU B，上层该怎么做、为什么必须这么做。编排仍由上层拼原语；core 暂不提供「一键 migrate」API。更细的英文说明以 `include/rendezvos/trap/trap.h` 文件头里 `@note_start` … `@note_end` 为准。

**软件层是 space 无关的通用框架**，也适用于未来的 MSI / MSI-X space；今天已实现的后端是管脚形（IOAPIC RTE / GIC SPI），MSI 的 `space->disable` / `enable` 硬件细节尚未接线。但是 disable→free→alloc→enable 这条设计是一样的。

口令（在**旧属主**上）：

`irq_disable(old_local)` → `irq_global_free` → `irq_global_alloc`（把条目记到新属主，得到 `new_local`）→ `irq_enable(new_local, new_cpu, …)`（仍由**旧属主**去写芯片并 unmask；**不必**再发 IPI 让新属主去做 enable）。

其中 **`irq_disable` 已包含**「space mask + 排空本核 local pending」；上层**不必**再单独调一步 wait，也**没有**对外的 nosync / 软件计数式接口。处理函数不变时，**不要**为换核再 `register` 一遍（free 会留下 handler，见 §1.4）。

#### 1.7.1 为什么换核相关的操作都要由旧属主来做？

先看竞态。设一开始 CPU A 是中断 I 的属主。

接着 I 来了，A 已经进了 handler。若此时 **CPU B** 去改 I 的映射 / 亲和，把它改成打到 B，就会变成：handler 还在 A 上跑，软件里记的亲和却已经是 B。这不安全。举例：handler 末尾做 EOI 时，可能因为 `irq_global_find`（按 local）失败而找不到对应的 `struct arch_global_irq`（映射已经对不上「本核 + 旧 local」），EOI 也就做不全——x86 上定向 `ioapic_eoi` 尤其依赖这次反查（见 `26`）。

所以要把换核整段都放在旧属主上，针对的就是「非属主改亲和」这类问题。对本线 **mask 掉、并把本核 local 接口上可能还要进 handler 的 pending 排空之后**，亲和变更与这条 I 的处理都在同一颗属主核上推进：本线被 `space->disable` 挡住以后，新的 I 不会再投递出来，属主再改亲和是安全的——它会给 B 选出并绑上新的 `local_irq_id`，再 unmask；改 B 侧 map 时，同样处在「本线已 mask、本核侧已排空」的前提下，也是安全的。

（说明：`irq_global_alloc` **仍然允许**任意核替目标 CPU 去抢未占用项——那是 §1.4 的占有规则本身。这里约束的是**换核编排**：让旧属主连「替新核 alloc」和「最后 enable」一并做完，并不是改掉 alloc 的异核能力。）

上层怎么用？

- 调用方**已经在属主核**上：直接按口令做 `irq_disable` → `free` → `alloc`（给另一核的 local 号并 map）→ `enable`。替新核 `alloc` 时，靠条目上 CAS `USED`，以及给目标核绑 map（`irq_global_alloc` 本就支持为别的核选 local 号）。
- 调用方**不在属主核**上：按 IPC 侧约定，发给「系统 / 内核」的消息应由该核的 **boot 线程**代收（boot 与 `kernel_handle_msg` 见线程篇 `13`）。把换核请求发到**属主核的 boot 线程**，由它在本核跑完整段口令。这条执行流须**开着中断**：`irq_disable` 在 mask 之后靠本核还能进 handler，才能把 local 接口上已 latched 的那次 I 处理完；关着中断空转 `arch_irq_local_pending` 没有意义。

#### 1.7.2 为什么 `irq_disable` 在 mask 之后还要等？

原则（对所有 space，含未来 MSI）：`space->disable` 必须挡住**再新发出的** I；已经落在**核本地中断接口**上的那一个pending中断，mask 挡不住，之后仍可能进本核 handler。因此 `irq_disable` 在属主上校验软表后：先 mask，再 **`while (arch_irq_local_pending(local_irq_id))`**（**须开着本核中断**），确认本核上这条 local 号不会再在未 unmask 的情况下跑 handler，然后才返回给上层去做 `free`。

**今天的管脚后端举例**（IOAPIC RTE / GIC SPI，见 `26` / `27`）：

设备 → 管脚 → 全局分发（IOAPIC、GIC GICD 等）→ 核本地中断接口（LAPIC、GICC 等）→ 核

这里 `space->disable` 挡的是「管脚 → 全局分发」。**未来 MSI / MSI-X**（尚未接线）：mask 多半在设备 PCI MSI / MSI-X 能力（或 interrupt remapping）；但是总线上已发出、或已入 LAPIC IRR 的 message 仍可能进 handler——同一套「先 disable 再排空」仍然适用，具体 `space->disable` / pending 语义由 MSI space 定义。

`arch_irq_local_pending` 是硬件查询，不是软件计数：

- x86：本核 LAPIC 上该向量的 **IRR ∪ ISR**（见 `26` §5.6）；
- aarch64：GIC 上该 INTID 的 **Active**；若仍 enable 则也看 Pending。mask 之后单独亮着的 distributor Pending **不**用来空转（电平线否则可能永远等不完，见 `27`）。开着中断 wait 时，已打到 GICC、尚未 IAR 的中断会先被 handler 吃掉（IAR 置 Active），再回到 while 看到 false——属有意设计，不是漏查 IRR 式 latched 位。

当前按芯片模型假定：管脚路径上「mask 之后还可能再进 handler」的状态可被上述查询 + 开中断 wait 覆盖；GIC 对同一 INTID 是状态位而非无限长队列。riscv64 irqchip 未接前无此 hook。对外暂不提供「只 mask、不等待」的 API；若以后有调用方需要再拆。

---


### 1.8 Space 寿命、`nr_used` 与并发（现行模型）

本节对齐 `include/rendezvos/trap/irq_space.h` 文件头注释与 `kernel/trap/irq_space.c`。

#### 寿命状态机

| `life` | 含义 |
|--------|------|
| `ACTIVE` | 已 `irq_space_common_init`，尚未入注册表；创建 持有 refcount **1** |
| `REGISTERED` | 已在全局 list 上；那 **1** 已**转让**给 list（register **不再**额外 get） |
| `CLOSING` | unregister 已下手，仍挂在 list 上；新 `ops_begin` / 新 alloc 应失败 |
| `CLOSED` | 已 unlink；unregister 方持有那 1，随后 `ref_put` → `free_irq_space_ref` |

`irq_space_register(struct irq_space *space)`：校验必填 ops（lookup / choose / bind / enable / disable；`eoi` / `destroy_priv` 可空）、range 不重叠、life==ACTIVE → CAS 到 REGISTERED → `list_add`。成功后调用方**不得**再 `ref_put`/free 该 space。

`irq_space_unregister`（**非阻塞**）：CAS REGISTERED→CLOSING → 若 `refcount!=1` 或 `nr_used!=0` 则恢复 REGISTERED、返回 `-E_REND_AGAIN`；否则 unlink、store CLOSED、`ref_put(free_irq_space_ref)`。`free_irq_space_ref` 要求 CLOSED 且 node 已 detach，再调 `destroy_priv`（释放 `priv` 并置空），最后 `m_free(space)`。`priv` 的释放与 `priv=NULL` **只**在 `destroy_priv` 里做。

今天树内**尚无**调用 unregister；SPI / IOAPIC space 视为 boot 常驻。

#### `refcount` vs `nr_used`

- **`refcount`**：list 持有的那 1 + 在飞的 `ops_begin`（仅 **alloc/free** 冷路径 pin）。`ops_end` 的 `ref_put` 传 NULL freer，不会在 ops 里拆对象。
- **`nr_used`**：已 CAS 上 `USED` 的 **claim 计数**（alloc 成功 +1；free 在 `try_set_unused` 成功后 −1）。**不是** entry 引用计数，也不表示「线已 enable」。unregister 要求其为 0。

**没有 per-entry refcount。** 热路径 `irq_global_find` / `trap_handler` / enable / disable / register **不** `ops_begin`。

#### 热路径投递与 space 指针

| arch | 热 find | space 句柄 |
|------|---------|------------|
| x86 | per-CPU `local_irq_map[]` 原子读出 `entry*` | entry→space；无静态 space 单例 |
| aarch64 SPI | 公式：`local → INTID → 下标 → priv[]` | 文件内静态 `gic_spi_space`（**不是** ref；register 成功后赋值；`destroy_priv` 清掉） |

因此：**热 find 与 unregister 没有共同的 pin。** 安全假设是「先 disable/drain 再 free，且 `nr_used==0` 才拆 space」；若在仍可能进 trap 的 CPU 上并发 unregister，已读到旧 `gic_spi_space` / 旧 map 指针的核可能与 `free(priv)` 竞态——属已知缺口，现行用法不热拆 space。

#### alloc / free 顺序（锁与发布）

公共路径在 **MCS `irq_space_list_lock`** 下 lookup + `ops_begin`，然后**放锁**再做 choose / bind：

1. **alloc**：`choose_local` → CAS `USED` → `nr_used++` → 再确认 life==REGISTERED → **先写** `local_irq_id`/`cpu_id` → **再** `bind_local`（x86：CAS map 0→entry；aarch64：no-op）。失败则回滚 unbind/清亲和/`nr_used--`/清 USED。
2. **free**：属主校验 → `bind_local(NULL)` → 清亲和 → `try_set_unused` → `nr_used--`。

x86 上「先写亲和再 bind」+ map 的 `lock cmpxchg`，保证 find 不会看到半发布槽。aarch64 无 map，enable 前须保证 handler/亲和对目标核可见（migrate 口令 + disable 排空）；详见 §1.7 与限制节。

#### dense 坐标（无 pin / 无 intid 字段）

- x86 entry 只缓存 `ioapic *`（EOI）；GSI = `(entry - priv) + base`。
- aarch64 SPI entry **无** arch 扩展字段；INTID = `gic_spi_intid((entry - priv) + base)`。

---

## 2. 目标与边界

**本篇（irq_space + 与分发的衔接）提供：** 两套软件编号；全局表的申请 / 占用 / 释放 / 查找；按 `(local_irq_id, cpu_id)` 挂 handler；属性里的「需要 EOI」「已占用」；以及分发末尾对 EOI / 调度的挂钩说明。

**明确不做（留给别的篇或调用约定）：**

- APIC / GIC **寄存器位域手册**与探测细节（`26` / `27`）；线控由本篇 space ops 调用那些原语；
- MSI / MSI-X **后端与 PCI 接线**（软件 migrate 口令见 §1.7；本篇不写 MSI 寄存器）；Linux 式 `request_irq` 框架；
- 把异常 EC / 缺页当成「设备 IRQ」来讲（`23`）；
- 同核上「正在处理时拆表」的同步——调用方先停投递；
- 运行期热拆 space（`unregister` 与热路径 find 无共同 pin）——见 §1.8。

---

## 3. 分层与调用方

| 角色 | 做什么 |
|------|--------|
| `arch_start_platform` | BSP 上 `irq_space_register`（须已有 `kallocator`；range 由平台决定） |
| `init_interrupt` | 要求全局数组已就绪；再 `arch_init_interrupt`（装 IDT / 设 VBAR） |
| Timer | `rendezvos_time_init`：取 `arch_get_timer_irq_num` 后 `register_irq_handler(…, this_cpu, IRQ_NEED_EOI)`（x86=`0x20`；aarch64=DTB ns-phys） |
| IPI | `arch_smp_ipi_init`：`register_irq_handler(ARCH_IRQ_ID_IPI, this_cpu, …)` |
| Fault / SVC | `arch_register_fixed_trap` 内部再调 `register_irq_handler`（带每个匹配 id 与每个 `cpu_id`） |
| 设备 | **先 `irq_global_alloc`，再 `register_irq_handler(..., IRQ_NEED_EOI)`，再 `irq_enable(local, cpu, trigger, polarity)`** |

---

## 4. 数据结构与不变量

### 4.1 表项

```c
struct local_irq {                     /* rendezvos/trap/irq_space.h */
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;                  /* 普通写；每核一份 */
};
DEFINE_PER_CPU(struct local_irq, local_irq_vector[NR_ARCH_LOCAL_IRQ]);

/* irq_space_common.h：各 arch 的 arch_global_irq 公共前缀 */
#define GLOBAL_IRQ_COMMON \
        void (*irq_handler)(struct trap_frame *tf); \
        atomic64_t irq_attr;           /* 占用时原子置「已占用」*/ \
        local_irq_id_t local_irq_id; \
        cpu_id_t cpu_id; \
        struct irq_space *space;       /* 所属 space；ops 挂在 space 上 */

struct arch_global_irq {               /* 各 arch/*/trap/irq_space.h */
        GLOBAL_IRQ_COMMON
        /* x86：仅 ioapic *（EOI）；GSI 由 dense priv[] 下标反推。
         * aarch64 SPI：无额外字段（INTID = GIC_SPI_START + 下标）。 */
};

struct irq_space {
        struct list_entry node;
        void *priv;                      /* dense arch_global_irq[] */
        global_irq_id_t global_irq_id_base;
        size_t nr_global_irq_id;
        atomic64_t nr_used;              /* claim 计数，非 refcount */
        ref_count_t refcount;            /* list(1) + ops_begin */
        atomic64_t life;                 /* ACTIVE/REGISTERED/CLOSING/CLOSED */
        /* ops: lookup / choose_local / bind_local / enable / disable /
         *      eoi(可空) / destroy_priv(可空，但有 priv 时必须提供) */
};
```

`local_irq` 不再套全局宏：本核表只有 handler + 普通 `u64` 属性。属性位：`IRQ_NEED_EOI`、`IRQ_ATTR_USED`（见 `irq_space_common.h`）。`irq_attr_*` / `irq_atomic_attr_*` 见 `irq_space.h`。`enum irq_trigger` / `enum irq_polarity` 在 `trap_common.h`（供 `irq_enable` 与 space ops 共用）。已无独立 `irq_chip`：vtable 在 space 的 enable/disable/eoi。寿命 / `nr_used` / 热路径约定见 **§1.8**。

### 4.2 编号空间（硬件贴合）

| | x86_64 | aarch64 |
|--|--------|---------|
| 本核表长度 | `NR_ARCH_LOCAL_IRQ` = LO（`0x40`） | 同宏（EC+SGI+PPI 上界） |
| IDT / 硬件全宽 | IDT **256**（SDM：向量 0–255）；表外号仍可作 IDT 向量，但不进 `local_irq_vector[]` | CPU **没有** 256 项向量表；GIC 用 INTID |
| 身份 | 向量号 = `local_irq_id` | `[0,64)` = EC；`[64,…)` = intid+64 |
| 表外窗口 | `[ALLOC_LO, ALLOC_HI]` = `[0x40, 0xEF]` | 同；`choose_local`：local = INTID + 偏移 |
| 全局数组长度 | `ioapic_init` 内按 `ioapic_nr_gsi()` 申请 | `gic.nr_spi()` |

x86 上 Intel 保留 **0–31** 给架构异常。设备 / timer / IPI 必须落在 ≥32 的向量上；timer / spurious / IPI 用固定宏，设备走表外窗口再 bind 进全局表。

aarch64 上为了复用同一套「按编号分发」外形，把 EC 放入前 64 槽、IRQ 整体平移 +64——Trap 篇说的「假统一、真映射」。SGI+PPI 落在本核表内；SPI 是表外号，经公式映到 space `priv[]`。

### 4.3 本核固定槽、表外窗口与空洞

读布局时，把编号分成三类即可：

**① 本核表内的固定槽（结构性）**

异常 / EC、timer、IPI，以及 aarch64 上整段 SGI+PPI，都会打到「被中断的那颗核」。入口汇编查的是本核的 `local_irq_vector[]`，所以这些编号落在 `[0, LO)`，直接 `register_irq_handler(id, this_cpu, …)` 即可。handler 可以晚装（timer / IPI），也可以故意不装（x86 spurious：误入则 unknown，随后 panic）。

**② 表外窗口 → 某 `irq_space` 的 `priv[]`**

外设 SPI / IOAPIC·GSI（MSI 未做）用大于等于 LO 的 `local_irq_id`。软件上先 `irq_global_alloc(global_irq_id, cpu_id, &local)`，再 `register_irq_handler`，再 `irq_enable(local, cpu, …)`。Global 表按 space 持有（今天各 arch 一个：x86 `irq_space_ioapic_register` / aarch64 `irq_space_gic_spi_register`）；`global_irq_id` 机器内唯一，用 space 的 `[base, base+n)` 路由。亲和写在表项的 `(local_irq_id, cpu_id)` 上，硬件路由由 `irq_enable` → `space methods` 下发。

`irq_global_alloc` 怎么定本核编号：交给 `space->choose_local`——aarch64 用公式直接给出；x86 在目标核上扫描 `[ALLOC_LO, ALLOC_HI]` 找空闲号。定下之后公共层 CAS `USED` 占全局项，再 `space->bind_local`。

**③ 空洞（本核表长度内、又未被固定宏占用的空隙）**

例如 x86 上 timer / spurious / IPI 周围那些空隙（如 `0x21–0x26`）。仍落在本核表长度内，但勿手写进 LVT 或路由——留给以后固定用途，或故意留空。

```text
x86_64 local_irq_id（= IDT 向量）布局示意：

  0          31   32       0x20 0x27 0x30        0x40              0xEF  0xF0      0xFF
  ├─架构异常─┤  空洞    ──┤timer│spur│IPI│  空洞   ├─ 表外 → space entries ─┤ 空洞 │
  └─本核表内─┘            │      │    │          └─ LO..HI（设备）──────┘        │
                          └─本核表内固定槽───────┘                  └─未使用────┘

  256 项 IDT 全宽；向量 0–15 不可作 APIC 投递目标（APIC 合法范围 16–255）。

aarch64 local_irq_id 布局示意：

  0 ……… 63   64 ……… 95   96 ………………… （SPI 上界）
  └─本核 EC─┘  └本核 SGI+PPI─┘  └─ 表外 → space entries ─┘
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
| `[0x40, 0xEF]`（`ALLOC_LO..HI`） | 表外设备号 | IOAPIC GSI（表已申请；MSI 未做）；经 per-CPU map 进 space entries |

中间空洞（如 `0x21–0x26`）既不在表外窗口、也未被固定宏占用——勿写进 LVT。

**aarch64（对齐 GIC INTID 分段）：**

| 区间 | 用途 | 硬件 |
|------|------|------|
| `[0, 63]` | EC / sync | ESR.EC，不是 GIC |
| **64–95** | SGI+PPI（intid 0–31；含 IPI0、timer PPI） | INTID 固定，落在本核表内。timer 的具体 id 仍由 `arch_get_timer_irq_num` 解析，但槽位已经落在这一段里 |
| **≥96**（`ALLOC_LO` 起） | SPI（intid 32+） | Distributor 上可配的外设线；公式映 `global_irq_id` |

和 §1.3 一起看：本核表是每核一份的，固定槽在每核各自挂函数；设备走全局表并记亲和，不是「全 CPU 共用一个 handler」。

### 4.4 注册不变量

细则与「为什么」见 §1.4，这里列硬规则：

- `register_irq_handler` / `irq_global_alloc`：**必须给具体 `cpu_id`**（不能用 `CPU_ID_INVALID`）。
- `irq_global_free`：只收 `global_irq_id`；owner 取自条目，调用方必须是属主（`percpu(cpu_number) == entry->cpu_id`），否则 `-E_IN_PARAM`。
- **全局**路径的 `register_irq_handler`：调用方必须是属主（`percpu(cpu_number) == cpu_id`），否则打 error 后返回。
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
| `kernel/trap/irq_space.c`、`include/rendezvos/trap/irq_space.h` | **编号空间** | registry、alloc/free/find、local 表、`ops_begin` |
| `include/rendezvos/trap/irq_space_common.h` | **编号空间** | `GLOBAL_IRQ_COMMON`、id / attr 位 |
| `arch/x86_64/trap/irq_space.h` + `irq_space.c` | **投递 map** | `arch_global_irq`、`local_irq_map`、共享 `arch_irq_space_choose/bind_local` |
| `arch/x86_64/trap/irq_space_ioapic.c` | **IOAPIC space** | dense GSI 表、`ioapic *` EOI、`irq_space_ioapic_register` |
| `arch/aarch64/trap/irq_space.h` + `irq_space_gic_spi.c` | **SPI space** | 公式投递、`gic_spi_space` 旁路、`irq_space_gic_spi_register` |
| `kernel/trap/trap.c`、`trap.h` | **分发 / 线控** | `register_irq_handler`、`irq_enable`/`disable`、`trap_handler` |
| `arch/*/trap/trap.c` | **装表 + CPU EOI** | fixed 注册、`arch_local_eoi`、IDT / VBAR、`arch_irq_local_pending` |
| `ioapic_init` / `arch_start_platform` | 启动 | 注册 IOAPIC / SPI space |
| 平台 APIC / GIC | **硬件 MMIO** | 寄存器细节（见 `26` / `27`） |

---

## 6. 流程

### 6.1 Boot（软件表必须先于「能收到中断」）

1. BSP 的 `arch_start_platform`：x86 经 `ioapic_init` → `irq_space_ioapic_register`；aarch64 → `irq_space_gic_spi_register(gic.nr_spi())`（须已有内核分配器）。
2. 每核 `init_interrupt`：仅 `arch_init_interrupt`（lidt / VBAR；aarch64 路径上可能立刻 enable IRQ）。
3. 平台 IRQ 接口（x86 `init_irq` / aarch64 `gic.init_cpu_interface`）→ `smp_ipi_init` → …… → `rendezvos_time_init`（register timer；x86 路径上才 `software_enable_APIC`——SVR 软件使能）。

```text
arch_start_platform          // BSP：irq_space_ioapic_register / irq_space_gic_spi_register
…
init_interrupt
  → （space 由平台 register）
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
  表外：irq_global_find(id, CPU_ID_INVALID, &entry)
→ handler 或 unknown+panic
→ NEED_EOI → arch_local_eoi [→ space->eoi]  // CPU 侧 ACK，再可选源侧
→ !from_kernel && core_tm → schedule
```

#### EOI 时序（漏 EOI 会卡住后续中断）

```text
  正常路径（IRQ_NEED_EOI=1）：

   IRQ 到达 ─► IRR 置位 ─► CPU accept ─► ISR 置位 ─► ISR 执行
                                              │
                                              └─ trap_handler 末尾
                                                 arch_local_eoi ─► EOI 寄存器写 0
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

1. `irq_global_alloc(global_irq_id, cpu_id, &local_irq_id);` — **勿**手写 timer / IPI / EC 当设备号。内部由 `space->choose_local` 选号（见 §4.3 ②、§7.3）。可在别的核上替目标 `cpu_id` 调用。此时写入表项的 `local_irq_id` / `cpu_id` 并置 `USED`。
2. 在**属主核**上 `register_irq_handler(local_irq_id, cpu_id, isr, IRQ_NEED_EOI);` — 挂 ISR。
3. `irq_enable(local_irq_id, cpu_id, trigger, polarity);` — 按亲和 pair 找 entry，经 `space->enable` 写控制器并 unmask（见 `26` / `27`）。
4. 未占号就挂函数、或非属主核去挂全局号：打错误日志后返回。
5. 处理函数返回后由 `trap_handler` 做 EOI——函数里面**不要**自己写 EOI，除非故意不设「需要 EOI」。

**只换核、处理函数不变：** 在**旧属主**上：`irq_disable(old_local)` → `irq_global_free` → `irq_global_alloc` 给新核 → `irq_enable(new_local, new_cpu, …)`（仍由旧属主调用）。不在属主上则 IPC 到属主 boot 线程代做。**不要**为换核再 register 一遍（见 §1.4、**§1.7**）。

**卸模块 / 换处理函数：** 属主上先挡住投递 →（建议）`register(..., NULL, …)` 或直接挂新函数 → 若不再占用则 `irq_global_free` → 再卸代码。切记释放不会替你清掉 handler。

---

## 7. 公开 API

本篇涉及的接口分布在：两套 id（`irq_space.h` / `irq_space.c`）与分发（`trap.h` / `trap.c`）——`struct local_irq` / `struct arch_global_irq` / `struct irq_space`、`irq_space_register` / `irq_global_*`、`register_irq_handler`、`irq_enable` / `irq_disable`、`arch_irq_local_pending`、`trap_handler`、`init_interrupt`、space `choose_local` / `bind_local`（x86 共享 `arch_irq_space_*`；aarch64 在 `irq_space_gic_spi.c`）/ `arch_init_interrupt`、`arch_local_eoi`、`arch_unknown_trap_handler`。说明改写自 `irq_space.h` / `trap.h` / `arch/*/trap/irq_space.h` Doxygen。

**本篇不涉及：** `arch_register_fixed_trap` / `trap_class`（见 `23`）；weak `syscall`（见 `24`）；irqchip 寄存器细节（见 `26` / `27`）；timer / IPI 业务语义（见时间篇 / `30`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| BSP 一次 | `arch_start_platform` / `ioapic_init` 里注册 space（须已有 `kallocator`） |
| 每核装表 | `arch_start_core` 调 **`init_interrupt`**（→ `arch_init_interrupt`），然后平台 IRQ iface、`smp_ipi_init`，……再到 `rendezvos_time_init` |
| 设备驱动（首次） | **先 `irq_global_alloc`，再在属主核 `register_irq_handler(..., IRQ_NEED_EOI)`，再 `irq_enable(local, cpu, trigger, polarity)`** |
| 设备换核 | **旧属主**上：`irq_disable(old_local)`（mask + pending）→ `free` → `alloc` 给新核 → `irq_enable(new_local, new_cpu, …)`（旧属主 enable；处理函数不变则不 register）。详述与 IPC/boot；整体语义通用于各 space（含未来 MSI）见 **§1.7** |
| Fault / SVC | `arch_register_fixed_trap`（内部再 `register_irq_handler`；本核表内编号） |
| Timer / IPI | 本核表内固定号，直接 `register_irq_handler(ARCH_IRQ_ID_*, this_cpu, …)` |

未 alloc / 非属主就 register 全局号：error log 后 return。ISR 内勿自写 EOI（除非故意不设 `IRQ_NEED_EOI`）。

### 7.2 表项与属性

本核表见 §4.1 的 `struct local_irq`。全局表项公共前缀是 `GLOBAL_IRQ_COMMON`（`irq_space_common.h`），各 arch 的 `struct arch_global_irq` 再叠私有字段。

| 符号 | 说明 |
|------|------|
| `IRQ_ATTR_USED` | 全局项已占用（alloc）；本核 register 时也会置 used |
| `IRQ_NEED_EOI` | handler 返回后 `trap_handler` 调 `arch_local_eoi`（再可选 `space->eoi`） |
| `irq_attr_*` / `irq_atomic_attr_*` | get/set USED、need_eoi；全局项用 atomic 版 |

### 7.3 全局申请 / 占用 / 释放

```c
error_t irq_space_register(struct irq_space *space);
error_t irq_space_unregister(struct irq_space *space); /* 非阻塞；忙则 -E_REND_AGAIN */
error_t irq_global_alloc(global_irq_id_t global_irq_id, cpu_id_t cpu_id,
                         local_irq_id_t *local_irq_id_out);
error_t irq_global_free(global_irq_id_t global_irq_id);
error_t irq_global_find(local_irq_id_t local, cpu_id_t cpu_id,
                        struct arch_global_irq **entry_out);
```

| 接口 | 说明 |
|------|------|
| `irq_space_register` | 调用方已填好 space（含 `priv` / ops / range）；life 须 ACTIVE。重叠 / CAS 失败 → `-E_RENDEZVOS`；缺必填 ops / 空 range → `-E_IN_PARAM`。成功则 list 接管 refcount 1。无 IOAPIC 时可不 register。 |
| `irq_space_unregister` | 见 §1.8；现行启动路径不调用。 |
| `irq_global_alloc` | 可替任意目标 `cpu_id` 抢未占用项。USED/bind/`ops_begin` 失败 → `-E_REND_AGAIN`；窗口用尽 → `-E_REND_OVERFLOW`。 |
| `irq_global_free` | 只收 `global_irq_id`；list 锁下 lookup + `ops_begin`；**须在属主 CPU**。unbind → 清亲和 → `try_set_unused` → `nr_used--`。 |
| `irq_global_find` | 按 `(local, cpu)`；`CPU_ID_INVALID` = 当前核。**不** `ops_begin`。 |

`irq_global_alloc` 的过程（细节 §1.8）：

1. list 锁下找 entry + `ops_begin`，放锁；
2. `choose_local` → CAS `USED` → `nr_used++` → 写亲和 → `bind_local`；任一步失败回滚。

### 7.4 注册与分发

```c
void init_interrupt(void);
void register_irq_handler(size_t local_irq_id, cpu_id_t cpu_id,
                          void (*handler)(struct trap_frame *), u64 irq_attr);
error_t irq_enable(local_irq_id_t local, cpu_id_t cpu,
                   enum irq_trigger trigger, enum irq_polarity polarity);
error_t irq_disable(local_irq_id_t local); /* 属主当前核 */
void trap_handler(struct trap_frame *tf);  /* 汇编入口；声明在 trap.h */
void arch_local_eoi(u64 trap_info);
void arch_unknown_trap_handler(struct trap_frame *tf);
```

| 接口 | 说明 |
|------|------|
| `init_interrupt` | 直接 `arch_init_interrupt`（不检查全局数组）。 |
| `register_irq_handler` | 本核表内：写该 `cpu_id` 槽（不要求 caller 即该核）。全局：须已 alloc、亲和匹配，且 **caller 必须是属主**；`handler==NULL` 可摘 ISR。 |
| `trap_handler` | 按 `TRAP_ID` 查本核表或 `irq_global_find`（按 local）；无则 unknown+panic；若 NEED_EOI 则 EOI；用户来源且 `core_tm` 就绪则 `schedule`。 |
| `arch_local_eoi` | 平台 ACK（细节见 `26`/`27`）。 |
| `arch_unknown_trap_handler` | 打印帧；随后 `trap_handler` panic。 |

驱动 checklist 见 §6.3。x86 `syscall` **不**经本路径。

---

## 8. 多架构

编号与硬件号的对照见 §1.2、§4.2–§4.3。差异再收三条：

| | x86_64 | aarch64 |
|--|--------|---------|
| 表外绑定 | 每核 `local_irq_map`；同 LO 可异 GSI | 公式；同 LO 必同 SPI |
| `space->choose_local` | 扫 `[ALLOC_LO, ALLOC_HI]` 空闲号 | 公式算出 local |
| 打开外设线 | `irq_enable` → IOAPIC space ops（ISO/MSI/设备接线仍可能缺） | `irq_enable` → SPI space ops → `gic.enable_spi` |

控制器细节见 APIC / GIC 篇。riscv / loongarch 非 v0.1 主线。

---

## 9. 测试

间接：timer、IPI、SMP。无单独「全局占用耗尽 / 亲和错误」测试用例。

---

## 10. 限制与后续

**平台与功能缺口：**

- `irq_global_alloc`：USED/bind 失败 → `-E_REND_AGAIN`；表外窗口用尽 → `-E_REND_OVERFLOW`；参数非法 → `-E_IN_PARAM`。
- x86：`irq_enable` 可写 RTE/mask；**ISO / MSI / 具体 PCI 设备接线**仍未做时，许多外设仍接不上（见 `26` §9.6.8）。
- 伪中断（spurious）无专用 handler。
- 没有「同一编号挂多设备」的共享链（后写覆盖）；电平共享见 §6.2。
- 没有「同一全局中断每核不同 handler」。
- 没有可移植的 `irq_set_affinity` API；绑核靠 alloc 时的 `cpu_id` + `irq_enable` 下发；换核编排见 **§1.7**（见 `26` / `27`）。
- MSI / MSI-X **space 与 PCI 接线未接**；§1.7 软件口令已按「通用于各 space」书写，管脚链仅为现行后端举例。对外无 disable-nosync。
- riscv64：`arch_irq_local_pending` 随 irqchip 再补。

**已加锁 / 已计数的部分：** registry MCS 锁覆盖 register / unregister / alloc·free 的 lookup+`ops_begin`；`USED` CAS；x86 map CAS；`nr_used` / space `refcount`（见 §1.8）。

**仍靠调用约定的点：**

- 同核上：若未先挡住投递就释放或改 handler，仍可能和正在跑的 `trap_handler` 交错——属主检查挡不住；换核须先 `irq_disable`（含排空）再 `free`，见 §1.7。
- 热 find **不** pin space：勿在仍可能投递的 CPU 上热 `unregister`（`gic_spi_space` / map 旁路与 teardown 无共同同步）。
- 释放留下旧 handler：卸模块前必须自己摘掉。
- IOAPIC / GICD 多线共享同一 MMIO 字时，并发 enable 尚无芯片级锁（后续项）。
- aarch64：`arch_irq_local_pending` 在 mask 后不空转裸 Pending（有意）；排空依赖开中断 wait + Active，见 §1.7.2 / `27`。

---

## 11. 变更记录

- 2026-11-14：§1.7 / `trap.h` `@note` §2——soft migrate 口令明确通用于各 irq_space（含未来 MSI）；管脚投递链改为现行后端举例；MSI mask 点不同但 disable→drain 原则相同。§2/§7.1/§10 同步；aarch64 pending 排空改为有意设计表述。
- 2026-11-13：对照 trap→irq_space 模型全面对齐——§1.1/§2 层职责；§1.4 owner 窗；**新增 §1.8**（life / refcount 转让 / `nr_used` / 热 find 不 pin / dense 坐标 / `destroy_priv` / `gic_spi_space` 非 ref）；§4.1/§5/§7.3/§10；拓扑字段与 `arch_local_eoi` / `IOAPIC.h` 中 `gsi_id_t`。
- 2026-11-09：去掉 `irq_chip`；改为 `struct irq_space` + `space enable/disable/eoi`（每 space 一张 global 表、机器内唯一 `global_irq_id` + range）。Trap 线控改为 `irq_enable(local, cpu, …)` / `irq_disable(local)`；`irq_space_register` 取代 `irq_global_vector_alloc`。文档主体与 checklist 对齐；§11 更早条目仍可能写旧名。

- 2026-11-07：§1.7 / §6.3 / §7.1 / §10——`irq_global_disable` 已落地 mask + `arch_irq_local_pending` 排空（x86 IRR|ISR；aarch64 Active / 仍 enable 的 Pending）；口令不再单列 wait；靠硬件 pending/active 查询、对外无 nosync。MSI 与 riscv pending hook 仍为缺口。
- 2026-11-03：新增 §1.7 换核（migrate）：旧属主执行 disable→free→alloc→enable（当时文档将 wait 写成独立步骤且标明未实现）；说明非属主改亲和与 EOI/`find` 竞态、mask 后仍可能有 local 接口 pending、非属主经 boot 线程 IPC；MSI 缺口。§1.4 / §6.3 / §7.1 / §10 交叉引用。对齐 `trap.h` `@note_start`…`@note_end`。
- 2026-11-03：`irq_global_free` 去掉 `cpu_id` 入参；owner 从条目读取并与 `percpu(cpu_number)` 比较。
- 2026-11-01：`enum irq_trigger` / `enum irq_polarity` 下沉到 `trap_common.h`；`GLOBAL_IRQ_COMMON` / `IRQ_*` 归属改为 `irq_space_common.h`。
- 2026-11-01：文档对齐——去掉 Job (A)/(B) 说法；§1.6/§6.3/§8/§10 改为 alloc→register→`irq_global_enable`（§8 表不再写「RTE 未做 / gic.unmask」）；`free` 不 reset arch 拓扑；`vector_alloc(0)` 合法。
- 2026-10-31：`irq_global_enable`/`irq_global_disable`；free 保留 arch 拓扑；checklist 改为 alloc→register→enable。
- 2026-10-29：对齐 IOAPIC bring-up——GSI 全局表已由 `ioapic_init` 申请；缺口改为「RTE/ISO/mask/设备路由未齐」而非「IOAPIC 空壳」；§1.6 / §4 / §6.3 / §8 / §10 与 `26` 同步。
- 2026-10-23：理顺 §1 结构（去掉重复的 1.3 编号）；开篇用表格划清「编号空间 / 分发 / 硬件」；§5 / §10 同步。中文表述按阅读习惯整理。
- 2026-10-23：`IRQ_COMMON` 改为 `GLOBAL_IRQ_COMMON`（handler + atomic attr + local/cpu 亲和）；`local_irq` 直接写字段，不再共用该宏。
- 2026-10-23：占有模型落地——全局 `register` 亦要求属主核；`free` 改为 `try_set_unused` 且保留 handler（换核不必再 register；卸模块须自行摘 handler）。新增占有规则小节，并改 §4.4 / §6.3 / §7。
- 2026-10-23：明确 `USED` CAS 只挡并发 alloc；`irq_global_free` 强制属主 CPU（caller == `cpu_id`），注释与 §4.4 / §7.3 同步。顺带收紧 `irq_global_alloc` 里仅扫描分支使用的 `mapped`。
- 2026-10-23：模块文件改名 `irq` → `irq_space`（表达 id 命名空间）；`struct global_irq` → `struct arch_global_irq`。API 前缀（`irq_global_*` 等）与 `global_irq_vector` 符号名不变。
- 2026-10-23：软件 id / map / alloc 抽到 `kernel/trap/irq_space.c` + `include/rendezvos/trap/irq_space.h`，arch 侧 `arch/*/trap/irq_space.c` / `irq_space.h`；`trap.c`/`trap.h` 只留分发与装表。逻辑未改，仅迁移。
- 2026-10-25：去掉跨 arch 的 `arch_local_irq_id_available`；空闲判断内联进 x86 `arch_choose_local_irq_id`。
- 2026-10-25：§4.2 / §4.3 / §7 / §8 / §10 去掉残留 `arch_map_*` / `try_direct` / 「写下 map」表述，与 choose/lookup/bind 对齐。
- 2026-10-25：`arch_map_global_irq_id` → `arch_local_lookup_global_irq_id`；`arch_map_local_irq_id` → `arch_global_bind_local_irq_id`。
- 2026-10-25：`arch_try_direct_map_local_irq_id` → `arch_choose_local_irq_id`；选号策略（含 x86 扫窗）下沉到 arch，common 只做 claim/bind。
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
