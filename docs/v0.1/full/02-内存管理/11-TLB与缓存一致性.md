# TLB 与缓存一致性

v0.1 · 2026-09-25

本篇覆盖：`include/rendezvos/mm/tlb_cpu_mask.h`、`include/arch/x86_64/sync/tlb.h`、`include/arch/aarch64/sync/tlb.h`、`arch/x86_64/mm/arch_smp_tlb_flush.c`；以及 `schedule` / `map_handler` 里和它们的交界。软 IPI 的握手细节见 `06-SMP与同步/30-软IPI机制.md` 与 `32-TLB_shootdown与跨核一致性.md`——本篇不把那一套再抄录一遍。

关于标题里的「缓存」：aarch64 的 `sync/cache.h` 目前是空的，树里也没有对称的 x86 `cache.h`。本篇**没有**通用 dcache/icache 维护 API；设备 / DMA 路径后续另写。这里说的「一致性」，主要指 **CPU 侧页表翻译（TLB）** 在改 PTE、换地址空间之后还能不能保持一致。

何时在 `map` / `unmap` 里调哪一类 invalidate，页表篇 §6.2 已经锁定过；本篇把 **mask 是什么意思、跨核如何发送、和调度怎么衔接**，以及各 ISA 硬件指令分别对应什么，讲清楚。

---

## 1. 概述

改了 PTE，或者拆了用户映射之后，别的核上的 TLB 里可能还保留着旧翻译。若不知道「谁跑过这个地址空间」，要么不敢拆 AS，要么只能全机全部刷掉。

core 的答案分两层：

1. **`VSpace::tlb_cpu_mask`**：哪些 CPU **可能**还缓存着这个用户地址空间的翻译。
2. **arch 的 invalidate API**：本地刷，或者跨核。x86 走 **按 mask 的软 IPI + `invlpg`**；aarch64 在用户 / 跨核路径上多用 **`tlbi …is` 广播**（常常**忽略** mask 参数），但 mask 仍然约束拆除能不能放行。

mask 回答「该通知谁、能不能拆」；具体怎么刷是架构的事。少了任一层，要么漏刷，要么 ASID 不敢回收（见 ASID 篇）。

---

## 2. 目标与边界

提供：mask 类型与调度维护约定；与 `map_handler` / `schedule` 对齐的 invalidate 策略；x86 IPI shootdown 入口；各 ISA 硬件指令与官方手册的对照。

不做：通用 cache 维护 API；把 SMP 篇里的 IPI 握手再详细写一遍。

---

## 3. 分层与调用方

- 首次 map 还是改映射、走本地还是 all-core：位于 `map_handler.c`（页表篇）。
- 切线程时 set / clear mask、本地刷旧 AS：位于 `schedule`（VSpace 所有权篇）。
- x86 远程 `invlpg`：`arch_smp_tlb_flush.c` + 软 IPI。
- aarch64 的 IS 广播：主要在 `arch/aarch64/sync/tlb.h` 的 inline 里。

---

## 4. 数据结构与不变量

### 4.1 `tlb_cpu_mask`

位图宽度是 `RENDEZVOS_MAX_CPU_NUMBER`，修改时要持有 `tlb_cpu_mask_lock`（CAS）。字段在 `VSpace` 上（完整结构见 07 §4.2）：

```c
#define VS_TLB_CPU_MASK_BITS (RENDEZVOS_MAX_CPU_NUMBER)
BITMAP_DEFINE_TYPE(vs_tlb_cpu_bitmap_t, VS_TLB_CPU_MASK_BITS)

/* VSpace 里： */
vs_tlb_cpu_bitmap_t tlb_cpu_mask;
cas_lock_t tlb_cpu_mask_lock;
```

用户线程切到另一个用户地址空间时，`schedule` 里的顺序不能打乱：

1. `ref_get` 新的 vs；
2. **先**在**新** mask 上 set 本 CPU 这一位；
3. 装硬件根（aarch64 还会带上 ASID，见下篇）；
4. 本地 invalidate **旧** ASID / 旧空间；
5. **再** clear 本 CPU 在**旧** mask 上的位；
6. `ref_put` 旧的。

先 set 再装表，是为了避免「新线程已经在新 AS 上运行了，mask 里却还没有本核」——这时别人发 shootdown 会漏掉本核。先本地刷再 clear，是为了避免「mask 里已经没有本核了，本地 TLB 却还留有脏项」。

还有一处故意的滞后：下一线程若不是用户态，schedule **可以不**清 mask、也可以不换硬件根。位和 CPU 侧引用会保留到之后某次用户→用户切换。拆除回收必须等 mask 空，细节见所有权篇 / ASID 篇。

### 4.2 map 路径：新 vs 改

决策条件**不是**「是不是只有本 CPU」，而是 **「是不是第一次把这个 VA 映射上」**：

- 首次 map（`new_map`）：内核 VA 做本地 kernel invalidate；用户 VA 做本地 `invalidate_page(asid, v)`。假定别人还没缓存过这个翻译。
- remap / unmap：内核走 `*_kernel_page_all_core`；用户走 `*_page_all_core(asid, v, &mask)`。改过、拆过的翻译，运行过的核都要处理到。

各 ISA 具体用哪条指令刷 TLB，见 §6.1；cache 相关名词（本仓库尚无通用 API）见 §6.3；函数签名见 §7。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `tlb_cpu_mask.h` / `VSpace` 字段 | mask 类型 |
| `task_manager.c` 里的 `schedule` | set / clear + 本地刷 |
| `map_handler.c` | new vs all-core 的调用点 |
| `arch_smp_tlb_flush.c` | x86 IPI shootdown |
| `arch/*/sync/tlb.h` | 本地 invalidate，以及 aarch64 带 Inner Shareable 的 `tlbi *is` |

---

## 6. 流程

`tlb_cpu_mask`、切地址空间、以及 `map` 何时本地刷、何时跨核刷，约定在 §4。下面补充两件事：硬件实际用什么指令；启动时各核还要不要额外 init。cache 维护和 TLB 不是同一件事，附录放在 §6.3。

### 6.1 背景：x86_64 / aarch64 怎样让 TLB 失效旧项

#### x86_64（Intel SDM：TLB 管理 / Multiprocessor）

手册里，改完页表之后要用合适的方式让 TLB 失效旧项。本实现常用的本地手段是 **`invlpg`**（按线性地址失效一页）。没有 PCID 时，换 **CR3** 会把当前核上与该地址空间相关的用户 TLB 一并刷掉——`arch_tlb_invalidate_all` 一类路径就是靠重载 CR3 这类方式。

跨核没有「一条总线广播让所有核 `invlpg`」的通用指令可用，所以要依赖 **IPI**：本仓库在 `arch_smp_tlb_flush.c` 里，每个 CPU 一条 flush 消息（generation / busy）；发送方先在本地刷，再按 mask 给其他 online 核发软 IPI，等到 `done_gen`。APIC 尚未就绪时，可能只做本地。文件头注释写明：**no PCID**。

内核侧的 `arch_tlb_invalidate_kernel_page_all_core` 在源码注释里倾向 **只本地 `invlpg`**（按「内核映射 percpu / 不共享脏翻译」来解释）——读实现，不要想当然地当成全机广播。

#### aarch64（ARM ARM：TLBI / 屏障）

AArch64 用 **`TLBI`** 系列指令做 TLB 维护，参数里常带 ASID 与页号。本仓库 inline 里能直接对应的有：

| 本仓库 API（概念） | 指令侧 | 含义（简要说明） |
|--------------------|--------|------------------|
| 本地按页（带 ASID） | `tlbi vae1` | EL1 当前 VMID，按 VA+ASID |
| 跨核按页 | `tlbi vae1is` | 同上，带 **Inner Shareable** 广播 |
| 本地按 ASID 整空间 | `tlbi aside1` | 按 ASID 清 |
| 跨核按 ASID | `tlbi aside1is` | 广播版 |
| 内核页（常不看 ASID） | `tlbi vaale1` / `vaale1is` | 任意 ASID 的该 VA |
| 全清 | `tlbi vmalle1` | 当前 EL1 翻译 |

前后用 `dsb(ISHST)` / `dsb(ISH)` / `isb` 做同步，这是 ARM 要求的 TLBI 上下文同步流程，不是随意添加的装饰。`*_all_core` 路径上 **mask 参数常被 `(void)cpu_mask` 丢弃**——硬件广播已经覆盖 shareable domain 里的观察者；软件 mask 仍然用来约束 `del_vspace` / `vspace_clear_user_mappings(allow_self_use=false)`，避免「还有人可能持有旧翻译就拆 AS」。

范围失效若要用 `rvae1` 一类，需要更新的 TLBI 扩展；本实现的 range 路径目前仍可能退化为逐页 `vae1`（头文件注释已写明）。

### 6.2 启动时还要做什么

每核都会执行到 `arch_start_core`。里面会 `smp_ipi_init`；x86_64 另外再调 `arch_smp_flush_tlb_init`：给本核准备一条 flush 消息槽，并（全局一次）把 TLB 的软 IPI handler 注册上。aarch64 **没有**对等的「靠 IPI 刷 TLB」的 init——跨核依赖 `tlbi *is` 广播，不经过这条通道。

### 6.3 附录：内存类型与 cache 维护（尚无通用 API）

树内 **没有** 可移植 `dcache_clean` / `icache_invalidate` 族（aarch64 `sync/cache.h` 为空）。设备 / DMA / 自修改代码接入之前，先把硬件名词记录在此，避免和 TLB 篇混为一谈。

#### 6.3.1 x86 内存类型与 PAT

**SDM 卷三表 11-2 的内存类型**（页表 / MTRR / PAT 共同决定最终类型）：

| 类型 | 直观含义 | 写回语义 | 本仓库 |
|------|------|------|--------|
| **UC**（Strong Un-cacheable） | 不缓存，强序 | 不可重排、不可合并 | LAPIC 等 MMIO：`UNCACHED` → `PCD\|PWT` |
| **UC-**（PAT 中可降级） | UC 的弱化版，允许在某些组合下被 WC 覆盖 | 同 UC | PAT 编程时区分 |
| **WC**（Write Combining） | 写合并 | 写可暂存到 write-combining buffer，一次刷出 | 未单独暴露 |
| **WT**（Write Through） | 通写 | 写同时进 cache 与内存 | 普通 RAM 默认不走 |
| **WB**（Write Back） | 回写 | 写只进 cache，脏后回写 | 普通 RAM 默认 |
| **WP**（Write Protected） | 写保护 | 读可 cache，写穿透 | 当前未用 |

**全局开关 CR0.CD / CR0.NW**（SDM 卷二 CR0）：

| 位 | 置 1 含义 | 清零含义 |
|----|----|----|
| **CD**（bit 30，Cache Disable） | 整机禁 cache（仅调试用） | 允许 cache，个别页可由 PCD/PWT/PAT 单独禁 |
| **NW**（Not Write-through） | 配合 CD 控制写行为 | 正常回写策略 |

正常运行两位置零即可——单页粒度的非 cache 由 PCD/PWT/PAT 决定，不需要动 CR0。

**PAT（Page Attribute Table）**（SDM 卷三 §11.12）：8 项寄存器，把 PTE 的 `PAT|PCD|PWT` 三位（共 8 种组合）映射到上表的具体内存类型。v0.1 **未编程 PAT**——上电默认布局足以让 `PCD|PWT=11` 解析为 UC、`00` 解析为 WB，覆盖 MMIO 与普通 RAM 两类需求；后续若要在大块 RAM 上开 WC（例如图形缓冲、大 DMA 描述符），再写 IA32_PAT 并同步到所有核。本仓库页表篇 §4.5 已写明粗粒度策略。

**缓存拓扑发现：CPUID.02H**（SDM 卷二 CPUID）：EAX=2 一次性枚举 cache / TLB 描述符，调用方按 Intel 公布的查找表解码一字节（例如 `0x0E` = 24 KiB L1 数据 cache、6-way、行 64 B）。core 当前不依赖此信息——buddy / map_handler 不做 cache-aware 着色；记录在此供后续 cache 优化或自修改代码路径使用。

#### 6.3.2 aarch64 屏障域与 DMB/DSB 参数

ARM 屏障按 **共享域** × **访问类型** 两维取参数。共享域四档：

| 域 | 含义 |
|----|----|
| **nSH**（Non-shareable） | 只当前 PE 访问 |
| **ISH**（Inner Shareable） | 一组 PE 间一致（典型一簇核） |
| **OSH**（Outer Shareable） | 跨外设 / 跨簇一致 |
| **SY**（Full System） | 全局 |

访问类型三档：**LD**（Load-Load / Load-Store，对 Store→Load 不约束）、**ST**（Store-Store）、**SY**（Any-Any，全部约束）。组合出下表（`DMB` 与 `DSB` 共用同一组助记符；`DSB` 额外保证「完成才返回」，`DMB` 只保序）：

| | nSH | ISH | OSH | SY |
|----|----|----|----|----|
| LD | `DMB NSHLD` | `DMB ISHLD` | `DMB OSHLD` | `DMB LD` |
| ST | `DMB NSHST` | `DMB ISHST` | `DMB OSHST` | `DMB ST` |
| Any | `DMB NSH` | `DMB ISH` | `DMB OSH` | `DMB SY` |

本仓库 TLBI 上下文同步用 `dsb(ISHST)` / `dsb(ISH)` / `isb`（见 §6.1），其余屏障语义见 `31-锁与内存屏障.md`。`LDAR` / `STLR`（acquire / release 字宽）不在本篇范围。

#### 6.3.3 aarch64 TLBI 变体目录

`TLBI` 系列指令的助记符后缀编码了「域 / 范围 / EL / 是否 Last level」。本仓库 inline 用到的见 §6.1 表；下表给出完整目录供对照 ARM ARM：

| 后缀 | 含义 |
|----|----|
| `*`（无后缀） | 本地 PE |
| `*IS` | Inner Shareable 广播 |
| `*OS` | Outer Shareable 广播（ARMv8.4 起） |
| `ALLE1` | EL1&0 当前 VMID 的 stage-1 全清 |
| `ALLE2` / `ALLE3` | EL2 / EL3 全清 |
| `ASIDE1` | 按 ASID 清（EL1&0） |
| `VAE1` | 按 VA+ASID 清单页（EL1&0） |
| `VAAE1` | 按 VA 清单页，忽略 ASID |
| `IPAS2E1` | IPA→PA 映射清（EL2 stage-2） |
| `VMALLE1` | 按 VMID 清 EL1&0 |
| `RVA*` / `RVAA*` | Range 失效（ARMv8.4 起；带 `base / num / scale / ttl`） |
| `…L`（如 `VALE1`） | Last level only（只刷最后一级翻译，不刷中间级） |

带 `VM*` 的后缀多与 VMID / 虚拟化翻译相关；RendezvOS v0.1 不开 EL2 stage-2，stage-2 变体不在 inline 里。Range 失效若启用，可把 §7.3 表里 `invalidate_range` 的逐页循环换成单条 `rva*is`，但需要 **FEAT_TLBIRANGE**（ARMv8.4）——目前仍退化逐页。

#### 6.3.4 aarch64 IC / DC 维护指令

**PoC / PoU**（ARM ARM，本仓库未封装）：

| 点 | 含义 |
|----|----|
| **PoC**（Point of Coherency） | 「对系统里所有观察者都一致」的汇合点（常到内存/互连） |
| **PoU**（Point of Unification） | 「对本 PE 的 I 与 D 侧一致」的点（常到本核 unification） |

**IC 系列**（指令 cache）：

| 指令 | 作用 |
|----|----|
| `IC IALLU` | 本 PE 全部指令 cache 失效 |
| `IC IALLUIS` | Inner Shareable 域所有 PE 的指令 cache 失效 |
| `IC IVAU` | 按虚拟地址失效某行 |

**DC 系列**（数据 cache；按 VA 或 set/way）：

| 指令 | 作用 |
|----|----|
| `DC ZVA` | 把 `DCZID_EL0` 给出的一块（不一定等于 cache 行）清零 |
| `DC IVAC` | 按 VA 失效到 PoC |
| `DC CVAC` | 按 VA clean 到 PoC |
| `DC CVAU` | 按 VA clean 到 PoU |
| `DC CIVAC` | 按 VA clean + invalidate 到 PoC |
| `DC ISW` | 按 set/way invalidate |
| `DC CSW` | 按 set/way clean |
| `DC CISW` | 按 set/way clean + invalidate |

**`DC ZVA` 的使能条件**（ARM ARM `DCZID_EL0`）：

| 位 | 含义 |
|----|----|
| **DZP**（bit 4） | 0 = 允许 `DC ZVA`；1 = 禁止。需在用户态使用前确认已清零 |
| **BS**（bits [3:0]） | 块大小为 2^(BS+2) 字节；最大 9 → 2 KiB |

`DC ZVA` 常用于大块用户清零（`memset(p, 0, big)`）：先确认 `DCZID_EL0.DZP==0`，再按行大小（`BS` 算出）逐行清零，比按字节写快得多。core 当前未封装此路径——`map_handler_zero_page` 走普通 `memset`；记录在此供后续 zero-on-fault 优化。

改可执行页、加载固件 blob、将来 DMA 缓冲与 CPU 共享时，需要「先 DC clean 到 PoC / PoU，再 IC invalidate」一类序列——接入业务路径时再写入 `sync/cache.h`，本篇不假装已有实现。

---

## 7. 公开 API

本篇涉及的接口分布在：`include/rendezvos/mm/tlb_cpu_mask.h`，以及可移植名字下的 arch 实现
`include/arch/{x86_64,aarch64}/sync/tlb.h`（x86 另有 `arch_smp_flush_*` /
`arch_smp_flush_tlb_init` 在 `arch/x86_64/mm/arch_smp_tlb_flush.c`）。
`vs_tlb_cpu_mask_*` inline 在 `vmm.h`（与 `VSpace` 同篇生命周期相关，本篇说明语义）。
IPI 握手细节见 30 / 32 篇。无通用 dcache/icache API。

### 7.1 编排顺序（与调用方约定）

| 场景 | 顺序 |
|------|------|
| 调度切入某用户 AS | **`vs_tlb_cpu_mask_set(vs, cpu)`** → 再装 CR3/TTBR（先 set 再装表，避免漏 shootdown） |
| 调度切离 | 本地 TLBI / 换根 → **`vs_tlb_cpu_mask_clear`**（先刷再 clear） |
| `map`/`unmap` 改用户 PTE | 按页表篇：首次/更新选本地或 **`arch_tlb_invalidate_page_all_core(asid, va, &vs->tlb_cpu_mask)`** |
| 内核临时窗 / 内核叶 | **`arch_tlb_invalidate_kernel_page`**；`*_all_core` 见下表 ISA 差异 |
| 拆除 / 回收 ASID | 等待 **`vs_tlb_cpu_mask_is_zero`**（mask 空才放行） |
| x86 每核启动 | `arch_start_core` → **`arch_smp_flush_tlb_init()`**（aarch64 无对等 IPI init） |

### 7.2 Mask（`tlb_cpu_mask.h` / `vmm.h`）

```c
BITMAP_DEFINE_TYPE(vs_tlb_cpu_bitmap_t, RENDEZVOS_MAX_CPU_NUMBER);
/* VSpace::tlb_cpu_mask + tlb_cpu_mask_lock */
void vs_tlb_cpu_mask_zero / set / clear(VSpace *, ...);
bool vs_tlb_cpu_mask_is_zero(const VSpace *);
```

含义：哪些 CPU **可能**仍缓存该用户 AS 的翻译。x86 shootdown **按 mask 发 IPI**；aarch64 用户/跨核路径常 **广播 `tlbi *is` 并忽略 mask 参数**，但 mask 仍约束拆除放行。

### 7.3 可移植 invalidate 族（各 ISA 同名；语义分 ISA）

```c
void arch_tlb_invalidate_all(void);
void arch_tlb_invalidate_page(u64 asid, vaddr addr);
void arch_tlb_invalidate_page_all_core(u64 asid, vaddr addr,
                                       const vs_tlb_cpu_bitmap_t *cpu_mask);
void arch_tlb_invalidate_kernel_page(vaddr addr);
void arch_tlb_invalidate_kernel_page_all_core(vaddr addr);
void arch_tlb_invalidate_vspace_page(u64 asid, vaddr addr);
void arch_tlb_invalidate_vspace_page_all_core(u64 asid, vaddr addr,
                                              const vs_tlb_cpu_bitmap_t *cpu_mask);
void arch_tlb_invalidate_range(u64 asid, vaddr start, vaddr end);
```

| 接口 | x86_64（无 PCID） | aarch64 |
|------|-------------------|---------|
| `invalidate_page` | 本地 `invlpg`；asid 忽略 | `tlbi vae1` + DSB/ISB；打包 asid\|VPN |
| `…_page_all_core` | **`arch_smp_flush_page_tlb`**：本地 + 按 mask 软 IPI `invlpg` | `tlbi vae1is` 广播；**忽略 mask** |
| `kernel_page` | 本地 `invlpg` | `tlbi vaale1` |
| `kernel_page_all_core` | **仍只本地 `invlpg`**（设计如此） | `tlbi vaale1is` 广播 |
| `vspace_page` | 本地重载 CR3（整空间刷） | `tlbi aside1`（按 ASID；asid≥2¹⁶ 则 no-op） |
| `vspace_page_all_core` | **`arch_smp_flush_all_tlb(mask)`** | `tlbi aside1is`；忽略 mask |
| `invalidate_all` | 重载 CR3 | `tlbi vmalle1` |
| `invalidate_range` | 循环本地 `invlpg` | 循环本地 `tlbi vae1`（无 ARMv8.4 range） |

### 7.4 x86 专用（本篇覆盖）

```c
void arch_smp_flush_page_tlb(vaddr addr, const vs_tlb_cpu_bitmap_t *cpu_mask);
void arch_smp_flush_all_tlb(const vs_tlb_cpu_bitmap_t *cpu_mask);
void arch_smp_flush_tlb_init(void);
```

细节（generation / busy、APIC 未就绪仅本地）见 `arch_smp_tlb_flush.c` 与 32 篇。

---

## 8. 多架构

见 §6.1。riscv / loongarch 未纳入 v0.1 的 TLB 主线。

---

## 9. 测试

没有单独的 TLB 单元测试；依赖 SMP 加上用户 map / unmap / 切换间接覆盖。

---

## 10. 限制与后续

- cache 头文件为空。
- x86 内核 `*_all_core` 是否真广播：以源码为准。
- 与 `32-TLB_shootdown` 篇的分工：本篇讲策略、mask 与硬件对照；那篇写 IPI 握手细节。
- x86 启用 PCID 后，本地 / 远程失效语义会变——属于远期，见 ASID 篇。

---

## 11. 变更记录

- 2026-10-05：§6.3 扩写硬件背景（PAT/CR0/CPUID.02H、DMB 矩阵、TLBI 目录、DC ZVA）。打分前校正：CPUID `0x0E` 按 SDM 为 24 KiB/6-way 而非 64-set×8-way；range TLBI 是 FEAT_TLBIRANGE 不是 TLS；`DC ZVA` 清的是 DCZID 块（2^(BS+2)）不是 cache 行；`ALLE1` 是当前 VMID 的 stage-1 全清。来源：`core/docs/old/cache&tlb.md` 迁移审核。
- 2026-10-04：全文中文表述整理——「再抄一遍」→「再抄录一遍」；「以后另写」→「后续另写」；「对得上」→「保持一致」；「钉过」→「锁定过」；「怎么送」→「如何发送」；「留着」→「保留着」；「全机狂刷」→「全机全部刷掉」；「不理」→「忽略」；「卡着」→「约束」；「再写长一遍」→「再详细写一遍」；「落在」→「位于」；「拿着」→「持有」；「不能乱」→「不能打乱」；「人已经跑在…你」→「新线程已经在…本核」；「漏掉你」→「漏掉本核」；「没你了/脏着」→「没有本核/留有脏项」；「留到」→「保留到」；「装上」→「映射上」；「跑过的核都得动到」→「运行过的核都要处理到」；「补两件事」→「补充两件事」；「不是一回事」→「不是同一件事」；「丢掉旧项」→「失效旧项」；「冲掉」→「刷掉」；「靠」→「依赖」；「还没就绪」→「尚未就绪」；「叙事」→「解释」；「想当然当成」→「想当然地当成」；「对上」→「对应」；「点到为止」→「简要说明」；「套路/随便加点装饰」→「流程/随意添加的装饰」；「丢掉」→「丢弃」；「带着」→「持有」；「写了」→「已写明」；「走到」→「执行到」；「注册上」→「注册」；「不走这条通道」→「不经过这条通道」；「接上/钉在这里/混谈」→「接入/记录在此/混为一谈」；「粗控」→「粗粒度控制」；「直观」→「直观含义」；「解成」→「解析为」；「未接」→「未接入」；「整机关缓存」→「整机关闭缓存」；「装固件 blob/接到业务路径时再写进」→「加载固件 blob/接入业务路径时再写入」；「整刷」→「整空间刷」；「单测/靠」→「单元测试/依赖」；「属远期」→「属于远期」；§7「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-04：§4.1 补 `vs_tlb_cpu_bitmap_t` / `tlb_cpu_mask` 字段。
- 2026-10-04：§4 只留 mask / map 策略不变量；硬件刷法与 cache 附录迁入 §6.1 / §6.3。
- 2026-10-02：§4.4 附录——x86 内存类型/PAT/CR0.CD·NW；aarch64 PoC/PoU 与 IC/DC（标明尚无 API）。
- 2026-09-27：中文用语整理（teardown→拆除/回收收尾；咬合→衔接；蹲着/揣着→留着/带着）。
- 2026-09-26：§7 全文审阅——各 ISA 的 `tlb.h` / `tlb_cpu_mask.h` 补头文件注释；写清 mask 编排与 x86 IPI vs aarch64 `*is`（含 kernel `*_all_core` 本地-only）。
- 2026-09-25：补 SDM / ARM ARM 硬件对照（`invlpg`、IPI shootdown、`tlbi`/`*is`、屏障）；语言整理；去掉「待加强」备忘。
- 2026-08-29：整篇重做——纠正「仅本 CPU」决策叙事；new/remap；x86 IPI vs aarch64 IS；cache 空头诚实说明。
- 2026-08-26：v0.1 初稿。
