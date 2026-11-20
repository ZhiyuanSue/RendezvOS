# per-CPU 数据与访问约定

v0.1 · 2026-09-27

本篇覆盖：`kernel/smp/percpu.c`、`include/rendezvos/smp/percpu.h`、`arch/{x86_64,aarch64}/percpu.c`（`arch_enable_percpu` / `get_per_cpu_base`）。

SMP 身份 / BSP_ID（BSP = Bootstrap Processor，引导核；AP = Application Processor，非引导核）时序见 SMP 篇（`28`）；各变量「为何 per-CPU」见子系统篇（TM、kmalloc、EBR、IRQ…）。MCS（Mellor-Crummey & Scott 队列锁）的 `me` 参数事故亦见 `doc/ai/INVARIANTS.md`。

**官方手册：** Intel SDM — `IA32_GS_BASE`（及与 SWAPGS / `KERNEL_GS_BASE` 的分工）；ARM ARM — `TPIDR_EL1`（EL1 软件线程 ID，本仓库用作 per-CPU 基址）。

---

## 1. 概述

per-CPU 输入段 **`.percpu..data`** 由链接脚本收进输出段 `.percpu`，放一份 **CPU0 模板**：`_per_cpu_start` 在 `ALIGN(4K)` 之后，`_per_cpu_end` 在 `ALIGN(64)` 之后——这让 `per_cpu_size` 本身是 64（缓存行）的整数倍，后面「铺槽」不必在槽内补洞。映像后为 CPU1…MAX-1 **预留连续物理区并 memset 0**。

不是「每 CPU 从模板 memcpy」。AP 槽是**清零预留**——`DEFINE_PER_CPU(...)= { init }` **只对 CPU0 生效**；AP 的 GDT / TSS 等必须在 `arch_start_core` 重建。x86_64 上就有现成例证：`gdt[]`、`gdt_desc` 是带 CPU0 初值的 `DEFINE_PER_CPU`，每颗核在 `arch_start_core` 里 `set_gdt(cpu_id)`、`prepare_per_cpu_tss(cpu_id)` 逐个填满本核槽位（顺带 `wrmsr(MSR_KERNEL_GS_BASE, 0)` 把本核用户 GS 清零）。留意模板里 `gdt_desc.base_addr` 写死的是 **CPU0 的 `gdt` 虚拟地址**——它同时说明另一条铁律：**初值里不要嵌 per-CPU 槽相关的指针**，否则即使 memcpy 模板也会错。

| 宏 | 含义 |
|----|------|
| `DEFINE_PER_CPU(type, name)` | 放入 `.percpu..data` |
| `per_cpu(var, cpu)` | `__per_cpu_offset[cpu] + (&var - &_per_cpu_start)` |
| `percpu(var)` | `get_per_cpu_base() + 同上`（本核） |
| `arch_enable_percpu(cpu)` | x86：写 **`MSR_GS_BASE`**；aarch64：写 **`TPIDR_EL1`** |

### 1.1 硬件为何需要「基址寄存器」

每核要有一份自己的 `core_tm` / `local_irq_vector` / MCS node，又不能在热路径上「查询本核编号再乘以偏移量」走全局表（还要抗重入）。惯例做法是：

1. 算出本核 per-CPU 区的**虚拟基址**；
2. 写进 **只有本核能读的系统寄存器**；
3. `percpu(var)` 用「符号相对段起点的偏移 + 基址」得到本核副本。

x86 选 **GS_BASE**（内核态直接当基址；用户 TLS 另走 FS / KERNEL_GS，见线程篇 `switch_to`）。aarch64 选 **TPIDR_EL1**（EL1 私有；**不要**和用户 TLS 的 `TPIDR_EL0` 混）。

未 `arch_enable_percpu` 就调 `percpu()`，会读到错误基址，进而静默误访问错误槽位或崩溃。

#### 1.1.1 x86 GS 段机制要点

x86-64 的 per-CPU 基址靠 **`MSR_GS_BASE`**（`0xC0000101`）提供。64 位长模式下段描述符的 base 字段被忽略，`%gs` 段的真正基址完全来自这个 MSR；系统软件约定「`%gs: disp`」访问等于 `GS_BASE + disp`，limit / 权限在 64 位下基本不起作用，等价于一条「带基址的线性地址」寻址。配套寄存器：

- **`MSR_GS_BASE`**：当前生效基址（内核态读 `%gs:` 走它）。`arch_enable_percpu` 写入的是本核 per-CPU 区的**绝对虚拟地址**（`__per_cpu_offset[cpu]` 表值）。
- **`MSR_KERNEL_GS_BASE`**（`0xC0000102`）：内核运行期间用户态的 GS 基址暂存处。`SWAPGS` 把它与 `GS_BASE` 互换，让内核态用 per-CPU 基址、用户态用 TLS 基址，无需手动 wrmsr。注意按 Intel SDM，`SWAPGS` **不做特权检查**（任意 CPL 可执行），所以内核 entry / exit 必须严格配对使用——本仓库在 `kernel_entry.S` 与 `trap_vec.S` 的进入 / 退出各用一次。
- **`MSR_FS_BASE`**（`0xC0000100`）：FS 段基址，本仓库用作用户 TLS，随线程保存恢复（`switch_to`）；与 per-CPU 数据**无关**。

GS 相对寻址的直接好处是 entry 代码零算术：x86 链接脚本固化了两个常量——`__tss_rsp_offset = cpu_tss + 4 - _per_cpu_start`（`+4` 是 RSP0 字段在 TSS 结构内的位置）与 `__user_rsp_scratch_offset = user_rsp_scratch - _per_cpu_start`。`kernel_entry.S` 里 `swapgs` 之后直接 `mov %gs:__tss_rsp_offset, %rsp` 就切到本核内核栈，中间连一次加法都没有。`switch_to` 则分别走 `MSR_FS_BASE` / `MSR_KERNEL_GS_BASE` 保存恢复 `user_fs` / `user_gs`（64 位下用户 GS 在内核态恒暂存于 `KERNEL_GS_BASE`，见线程篇）。

`SWAPGS` 只在 ring 转换处用；`arch_enable_percpu` 在启动时每核直接 `wrmsrq(MSR_GS_BASE, offset)`，不动 `KERNEL_GS_BASE`（后者由 `arch_start_core` 逐核清零）。

#### 1.1.2 aarch64 线程 ID 寄存器

ARM 上没有「段基址」概念，per-CPU 借用 **线程 ID 寄存器**——它们只是 64 位通用系统寄存器，无权限 / limit 检查，软件约定存什么由 OS 决定：

- **`TPIDR_EL1`**：EL1（内核）私有；本仓库存放 per-CPU 基址。EL2/EL3 各有对应 `TPIDR_EL2/EL3`。
- **`TPIDR_EL0`**：EL0（用户）可读写，用作用户 TLS；`switch_to` 切线程时换。
- **`TPIDRRO_EL0`**：EL0 可见的只读版本；本仓库代码通篇未用到（Linux 用它做 this-CPU 访问优化，⚠待复核）。

EL1 代码读 `mrs x0, TPIDR_EL1` 取得基址，再加偏移即得本核副本地址。

---

## 2. 目标与边界

**提供：** 布局、宏、GS / TPIDR、跨核读纪律、MCS `me` 规则。

**不做：** Linux `this_cpu_ptr` 动态分配 API；动态对象（如 `core_tm`）仍是运行时 kmalloc 再 `per_cpu(ptr,cpu)=…`。

---

## 3. 分层与调用方

典型住户按用途分组：调度与线程 `core_tm`、`boot_thread_ptr`、`idle_thread_ptr`；内存 `kallocator`、`current_vspace`、`Map_Handler`、`boot_stack_bottom`、`pmm_spin_lock[ZONE_NR_MAX]`、`asid_mcs_node`；陷阱 `local_irq_vector[]`（x86 另有 `local_irq_map[]`）、`irq_space_mcs_node`；EBR 一组 `ebr_*`；时间 `tick_cnt`、`boot_base_time`、`heartbeat_event`、`event_tree_root`（x86 另有 APIC 定时基准 `apic_timer_base` / `apic_timer_gap`）；SMP `cpu_number`、`CPU_STATE`、`smp_ipi_pending`；各类锁 `log_spin_lock`、`id_spin_lock`、`port_table_spin_lock`、`name_index_spin_lock`。架构私有槽：x86_64 的 `gdt[]` / `gdt_desc`、`cpu_tss`、`user_rsp_scratch`、`smp_tlb_flush_message`（aarch64 **无** TLB msg 槽）；aarch64 的 `cpu_device_node`（本核对应设备树节点指针，启动时查 `reg` / `enable-method` 用，见 `28`）。

跨核读取其他核的 per-CPU：仅约定路径（TLB msg、EBR scan、调试）；并发修改须持锁或发 IPI。

---

## 4. 数据结构与不变量

### 4.1 offset 布局

`reserve_per_cpu_region`（在 `phy_mm_init` 路径）：

- `offset[0] = &_per_cpu_start`（链接段内 CPU0 模板）；
- 把游标 `ROUND_UP(…, 64)` 对齐后，`offset[1] = KERNEL_PHY_TO_VIRT(phy_end)`；
- 再为 CPU1…MAX-1 预留 `(MAX-1) * per_cpu_size` 物理空间并推进游标；
- `calculate_per_cpu_offset` 线性填 `offset[2…MAX-1]`。

注意**表值是每 CPU 区基址的绝对虚拟地址**，不是「相对 CPU0 的差值」：`offset[0]` 是链接段本身的虚地址，`offset[1]` 是新物理区换算出的虚地址。正因为此，`offset[cpu] + (&var - &_per_cpu_start)` 在 cpu=0 时恰好退化为 `&var`，C 代码里 CPU0 不需要任何特判。`per_cpu_size = &_per_cpu_end - &_per_cpu_start`，链接脚本已把它凑成 64 的倍数；`reserve` 再把游标对齐到 64B——两处合起来保证**每个槽 64B（缓存行）对齐、互不共享缓存行**。

`percpu.c` 注释写明：**只用 MAX-1 份额外区**（index 0 在链接脚本里）。随后 `clean_per_cpu_region` **memset 0** 额外区——不是 memcpy 模板。

#### 4.1.1 布局示意

```text
                  .percpu..data (链接段，CPU0 模板)
                  ┌──────────────────────────────────────┐
   &_per_cpu_start │ DEFINE_PER_CPU(core_tm) ...          │ ◀ offset[0]
                  │ DEFINE_PER_CPU(local_irq_vector) ... │
                  │ DEFINE_PER_CPU(smp_ipi_pending) ...  │
                  │ ...（所有 per-CPU 变量一份模板）       │
   &_per_cpu_end  └──────────────────────────────────────┘
                            │ 64B 对齐
                            ▼
                  额外预留区（AP 槽，memset 0）
                  ┌──────────────────────────────────────┐
   offset[1]  ──▶ │ CPU1 副本（per_cpu_size 字节）        │
                  ├──────────────────────────────────────┤
   offset[2]  ──▶ │ CPU2 副本                             │
                  ├──────────────────────────────────────┤
                  │ ...                                   │
                  ├──────────────────────────────────────┤
   offset[N-1]──▶ │ CPU(N-1) 副本                         │
                  └──────────────────────────────────────┘
                            ▲
                            │ phy_kernel_end 推进到这里
```

同一变量 `var` 在不同核的副本地址：`__per_cpu_offset[cpu] + (&var - &_per_cpu_start)`。`percpu(var)` 只是把 `__per_cpu_offset[cpu]` 换成 `get_per_cpu_base()`（即从 GS_BASE / TPIDR_EL1 读），省一次显式 `cpu` 查询。

### 4.2 MCS `me`

`lock_mcs(lock, me)` 的 `me` 必须是 **本 CPU 队列节点**（`&percpu(port_table_spin_lock)`、`&percpu(asid_mcs_node)`，或嵌在 `Map_Handler` 里的 node）。使用其他核的 slot 会损坏链表（INVARIANTS）。

### 4.3 启用时机

| 核 | 何时 `arch_enable_percpu` |
|----|---------------------------|
| BSP | `cmain`：`arch_cpu_info` 之后、`virt_mm_init` 之前（`arch_enable_percpu(BSP_ID)`） |
| AP | `start_secondary_cpu` **尽早**（在 `virt_mm_init` / `arch_start_core` 之前） |

`percpu()` 早于 enable 则基址错。`get_per_cpu_base()`：x86 `rdmsr GS_BASE`；aarch64 `mrs TPIDR_EL1`。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `percpu.h` / `kernel/smp/percpu.c` | 宏、offset、reserve / clean、`cpu_number` |
| `arch/x86_64/percpu.c` | `wrmsrq(MSR_GS_BASE, …)` / `rdmsrq` |
| `arch/aarch64/percpu.c` | `msr/mrs TPIDR_EL1` |
| `script/link/{x86_64,aarch64}_linker.ld` | `.percpu` 输出段、`_per_cpu_start` / `_per_cpu_end`；x86 另定义 `__tss_rsp_offset` / `__user_rsp_scratch_offset` 供 entry 汇编 |
| `arch/x86_64/trap/kernel_entry.S` | GS 基址消费方：`swapgs` 后直接 `%gs:链接期常量` 访问本核槽位 |

---

## 6. 流程

1. 链接：CPU0 模板在 `.percpu..data`。
2. `phy_mm_init`：`reserve_per_cpu_region` 预留 → m_regions 与内核映像合并预留一整段 → **同 1G 硬检查**（不满足即终止，日志 `cannot put percpu data and kernel data in the same 1G space`）→ `arch_map_percpu_data_space` 只映射额外区（模板随内核映像早已在映射内）→ `clean_per_cpu_region` memset 0（其间打印 `[ PERCPU_REGION @ … ]`）。
3. BSP / AP：先 `arch_enable_percpu(cpu_id)`，之后才可安全使用 `percpu()`。
4. 运行时：各子系统往本核槽填指针 / 状态；跨核用 `per_cpu(var, cpu)` **显式**指名。

---

## 7. 公开 API

接口分布在：per-CPU 布局与访问——`DEFINE_PER_CPU` / `percpu` / `per_cpu`、`arch_enable_percpu` / `get_per_cpu_base`、`__per_cpu_offset` / `cpu_number`、以及 boot 侧 `reserve_per_cpu_region` / `clean_per_cpu_region` / `calculate_per_cpu_offset`。说明改写自 `percpu.h` Doxygen（已与 `percpu.c`、`arch/*/percpu.c` 核对）。

**本篇不涉及：** 各住户变量语义（`core_tm`、`local_irq_vector`、MCS node…，见各子系统篇）；BSP_ID / 上线时序（见 `28`）；MCS 锁原语本身（见 `31`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 物理布局 | `phy_mm_init` 内：**`reserve_per_cpu_region`** → m_regions 预留 → 同 1G 检查 → `arch_map_percpu_data_space` → **`clean_per_cpu_region`**（额外区 **memset 0**） |
| BSP | `cmain`：`arch_cpu_info` 之后 **`arch_enable_percpu(BSP_ID)`**，此后才可 `percpu()`；`arch_start_core` 写 `cpu_number` |
| AP | `start_secondary_cpu` **最先** `arch_enable_percpu(cpu_id)`，再 `virt_mm_init` / `arch_start_core` |
| MCS | `lock_mcs(lock, me)` 的 `me` = **本核** node（`&percpu(...)`），禁止他核 slot |

未 enable 就 `percpu()` 则基址错。AP 槽无模板初值——勿假定 `DEFINE_PER_CPU(...)= {x}` 在 AP 上成立。

### 7.2 宏与本核访问

```c
DEFINE_PER_CPU(type, name);   /* → .percpu..data（CPU0 模板） */
per_cpu(var, cpu);            /* 显式 CPU；可跨核读 */
percpu(var);                  /* 本核 = get_per_cpu_base() + 偏移 */
```

| 宏 | 说明 |
|----|------|
| `DEFINE_PER_CPU` | 链接段一份模板；初值只服务 CPU0。 |
| `per_cpu` | `__per_cpu_offset[cpu] + (&var - &_per_cpu_start)`。跨核改须有协议。 |
| `percpu` | 依赖 GS_BASE / TPIDR_EL1 已正确初始化。 |

#### 7.2.1 访问路径示意

```text
  C 代码：percpu(core_tm)
        │
        ├─ 编译期：算 &core_tm - &_per_cpu_start  ── 常量偏移 OFF
        │
        └─ 运行期：get_per_cpu_base() + OFF
                          │
                          ▼
        ┌─────────────────────────────────────────────┐
        │ x86_64                                       │
        │   rdmsr MSR_GS_BASE  ──▶ 本核 per-CPU 区基址  │
        │   （SWAPGS 已在 syscall entry 切到内核态）   │
        ├─────────────────────────────────────────────┤
        │ aarch64                                      │
        │   mrs TPIDR_EL1     ──▶ 本核 per-CPU 区基址  │
        │   （EL1 私有，无 ring 切换开销）             │
        └─────────────────────────────────────────────┘
                          │
                          ▼
        基址 + OFF  ──▶ 本核 core_tm 副本
```

热路径上没有「先查 cpu_id 再查 offset 表」的全局访问，只有一次 MSR / 系统寄存器读 + 一次加法。这正是 per-CPU 数据存在的核心原因。

### 7.3 基址寄存器

```c
void arch_enable_percpu(cpu_id_t cpu_id);
vaddr get_per_cpu_base(void);
```

| | x86_64 | aarch64 |
|--|--------|---------|
| 写入 | `MSR_GS_BASE = __per_cpu_offset[cpu]` | `TPIDR_EL1 = …` |
| 读取 | `rdmsr GS_BASE` | `mrs TPIDR_EL1` |
| 勿混 | 用户 TLS：FS / `KERNEL_GS` | 用户 TLS：`TPIDR_EL0` |

写入基址寄存器的是本核区的**绝对虚拟地址**，前提是它已在当前页表的映射之内——这正是 §10 「同 1G」硬检查的由来。x86 上 GS 基址还被 entry 汇编以 `%gs:链接期常量` 的形式直接消费（`__tss_rsp_offset` / `__user_rsp_scratch_offset`，见 §1.1.1）；aarch64 上 `TPIDR_EL0` 存的是用户 TLS、由 `switch_to` 随线程换入换出，内核绝不可把它当作 per-CPU 基址读写。

### 7.4 布局辅助（mm/boot 内部）

```c
void reserve_per_cpu_region(paddr *phy_kernel_end);
void calculate_per_cpu_offset(void);
void clean_per_cpu_region(paddr per_cpu_phy_addr);
```

| 接口 | 说明 |
|------|------|
| `reserve` | `offset[0]`=链接段；`offset[1…]` 从 64B 对齐后的游标起预留 `(MAX-1)*size`；推进游标（调用方传 `&per_cpu_phy_end`）。 |
| `calculate` | 线性填 `offset[2…MAX-1]`。 |
| `clean` | 额外区清零——**不是** memcpy 模板（入参是物理地址，内部自行换虚址）。 |

表值均为**绝对虚拟基址**（§4.1）。`cpu_number`：`DEFINE_PER_CPU`；每核在 `arch_start_core` 写 `per_cpu(cpu_number, cpu_id) = cpu_id`（两架构同款；`irq_space` 注册、TLB flush、IPI 处理等都靠它核对「这就是本核」）。

### 7.5 MCS `me`（硬规则）

`me` 必须指向**当前 CPU** 的队列节点。用 `per_cpu(node, other)` 当 `me` 会损坏链表。细节亦见 INVARIANTS。

---

## 8. 多架构

| | x86_64 | aarch64 |
|--|--------|---------|
| 基址寄存器 | `IA32_GS_BASE` | `TPIDR_EL1` |
| 用户 TLS | FS / KERNEL_GS（`switch_to`） | `TPIDR_EL0` |
| TLB flush msg 槽 | 有 | **无** |

布局与宏相同。勿把用户 TLS 寄存器当 per-CPU 基址。

---

## 9. 测试

无 per-CPU 专项测试用例，间接覆盖：布局或基址启用时序若有错，多核启动后第一次 `percpu()` 访问就会崩，活不到后面（启动判据见 `28` §9）；`smp_test` 的并发矩阵则从多核同时反复打 per-CPU 住户——`spmalloc` 打各核 `kallocator`，`smp_lock` 打 MCS `me` 槽，`thread_affinity` 打各核 `core_tm`。

---

## 10. 限制与后续

- AP 零页 vs CPU0 初值：初值只服务 CPU0，且初值里不要嵌 per-CPU 槽相关的指针（`gdt_desc.base_addr` 写死 CPU0 `gdt` 虚址的教训，§1）。
- 槽位总量固定死：`RENDEZVOS_MAX_CPU_NUMBER × per_cpu_size`，链接期定段、boot 期预留，无 Linux 式动态 percpu allocator，运行时不能新增槽位。
- **内核映像与 percpu 额外区必须落在同一个 1G 内**：`phy_mm_init` 用 `ROUND_DOWN(…, GIGAN_PAGE_SIZE)` 硬检查，不等即终止启动（与启动期直映射按 1G 粒度建立有关，也顺带约束了 per-CPU 区总大小）。
- x86_64 的 `cpu_id` 是 APIC id，可非零、可稀疏（`BSP_ID` 取引导核 APIC id，SMP 篇（`28`）；`__per_cpu_offset[]` 按该 id 下标，越界防护见 `RENDEZVOS_MAX_CPU_NUMBER` 检查。aarch64 的 `BSP_ID` 恒为 0。
- 稀疏拓扑下勿用 `NR_CPU` 当合法下标上界遍历全表。

---

## 11. 变更记录

- 2026-11-19：底子首评批次——§7 opener；§9「测例」→「测试用例」。事实沿用 11-14/15。
- 2026-11-15：Maintainer 复审前可读性通读。跨篇引用统一既定昵称：两处「任务篇」→「线程篇」（与 README、`23` 等篇既定称呼一致）、两处「拓扑篇」→「SMP 篇（`28`）」（沿 `16` 篇先例）；§8 前双空行归一。`cpu_number` 写入点（两架构 `arch_start_core` 首句 `per_cpu(cpu_number, cpu_id) = cpu_id`）与 `set_gdt` / `wrmsr(MSR_KERNEL_GS_BASE, 0)` / `prepare_per_cpu_tss` 序列对照 `arch/{x86_64,aarch64}/boot/start_arch.c` 复核；事实层沿用 11-14 核验，未再改动。
- 2026-11-14：全文对照代码与链接脚本精读——§1 补链接段布局（`.percpu..data` 收进输出段 `.percpu`、`_per_cpu_start` 4K 对齐、`_per_cpu_end` `ALIGN(64)`）与 GDT 例证（AP 靠 `set_gdt`/`prepare_per_cpu_tss` 重建；`gdt_desc.base_addr` 初值写死 CPU0 虚址 → 初值勿嵌 per-CPU 指针）；§1.1.1 修正「SWAPGS 是特权指令」（Intel SDM：不做特权检查，任意 CPL 可执行，entry/exit 须严格配对）、澄清 64 位下描述符 base 字段被忽略、`GS_BASE` 直接持有绝对虚址，并补链接期常量 `__tss_rsp_offset = cpu_tss + 4 - _per_cpu_start` / `__user_rsp_scratch_offset` 在 `kernel_entry.S` 的 `%gs:` 直接寻址；§1.1.2 `TPIDRRO_EL0`「Linux 用作 CPU 编号」改标 ⚠待复核；§4.1 明确 `__per_cpu_offset[]` 表值为**绝对虚拟基址**、槽 64B 缓存行对齐、`calculate` 填至 `MAX-1`；§6/§7.1 补全 `phy_mm_init` 真实顺序（m_regions 合并预留 → **同 1G 硬检查** → `arch_map_percpu_data_space` → memset）；§3 住户清单按用途分组补全（`gdt[]`、`cpu_device_node`（aarch64 私有）、`apic_timer_*`、`boot_thread_ptr`/`idle_thread_ptr` 等）；§5 补链接脚本与 `kernel_entry.S` 两行；§7.4 `cpu_number` 写明写入式 `per_cpu(cpu_number, cpu_id) = cpu_id`；§9 展开间接覆盖链条（`smp_test` 并发矩阵）；§10 撤销含糊「GS 窗」表述，改为同 1G 硬检查、x86 APIC id 索引空间、初值勿嵌 per-CPU 指针；变更记录两条 2026-10-05 归位（原沉底于 2026-08-27 之后）。
- 2026-10-05：任务 1/3/5 精读——补全 BSP/AP/MCS 缩写首现释义；「踩错槽/拿基址/装 per-CPU 基址/他核 slot/已装好/扫全表」等口语改正式动词；修正 §7.3.1 编号错位为 §7.2.1。
- 2026-10-05：最终词句顺畅。
- 2026-10-04：§1.1.1/1.1.2 补 x86 GS 段机制（GS_BASE / KERNEL_GS_BASE / SWAPGS / FS_BASE 分工）与 aarch64 TPIDR_EL1/EL0/RO_EL0 线程 ID 寄存器说明；§4.1.1 加 per-CPU 布局示意（链接段模板 + AP 清零预留区）；§7.3.1 加 percpu() 访问路径图（MSR/系统寄存器读 + 偏移）。
- 2026-10-03：§4.3 / §7.1 BSP enable 时点改为 `arch_cpu_info` 之后。
- 2026-09-27：中文措辞整理——弱化「真源」等生硬堆砌用语。
- 2026-09-26：§7 全文审阅——`percpu.h` 全套 Doxygen；写清「清零预留≠memcpy」「先 enable 再 percpu」与 MCS `me`；划清 vs `28`/`31`。
- 2026-09-25：语言整理；§1.1 补 GS_BASE / TPIDR_EL1 硬件动机与用户 TLS 分界。
- 2026-08-29：整篇重做——纠正「复制」→清零预留；MCS me；GS/TPIDR；住户表；与拓扑篇时序交叉。
- 2026-08-27：初稿。
