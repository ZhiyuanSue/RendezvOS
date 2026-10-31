# TLB 与缓存一致性

v0.1 · 2026-09-25

本篇覆盖：`include/rendezvos/mm/tlb_cpu_mask.h`、`include/arch/x86_64/sync/tlb.h`、`include/arch/aarch64/sync/tlb.h`、`arch/x86_64/mm/arch_smp_tlb_flush.c`；以及 `schedule` / `map_handler` 里和它们的交界。软 IPI 的握手细节见 `06-SMP与同步/30-软IPI机制.md` 与 `32-TLB_shootdown与跨核一致性.md`——本篇不把那一套再抄一遍。

关于标题里的「缓存」：aarch64 的 `sync/cache.h` 目前是空的，树里也没有对称的 x86 `cache.h`。本篇**没有**通用 dcache/icache 维护 API；设备 / DMA 路径另写一篇。这里说的「一致性」，主要指 **CPU 侧页表翻译（TLB）** 在改完 PTE、换了地址空间之后还对不对得上。

何时在 `map` / `unmap` 里调哪一类 invalidate，页表篇 §6.2 已经锁定过；本篇讲清楚 mask 是什么意思、跨核怎么发出去、和调度怎么衔接，以及各 ISA 的硬件指令分别对应什么。

---

## 1. 概述

改了 PTE，或者拆了用户映射之后，别的核的 TLB 里可能还留着旧翻译。问题在于不知道「谁跑过这个地址空间」——不知道，就要么不敢拆 AS，要么只能全机刷一遍。

core 的答案分两层：

1. **`VSpace::tlb_cpu_mask`**：哪些 CPU **可能**还缓存着这个用户地址空间的翻译。
2. **arch 的 invalidate API**：本地刷，或者跨核。x86 走 **按 mask 的软 IPI + `invlpg`**；aarch64 在用户 / 跨核路径上多用 **`tlbi …is` 广播**（常常把 mask 参数**直接丢掉**），但 mask 仍然卡着拆除能不能放行。

mask 回答「该通知谁、能不能拆」，具体怎么刷是架构的事。少了任一层，要么漏刷，要么 ASID 不敢回收（见 ASID 篇）。

---

## 2. 目标与边界

提供：mask 类型与调度维护约定；与 `map_handler` / `schedule` 对齐的 invalidate 策略；x86 IPI shootdown 入口；各 ISA 硬件指令与官方手册的对照。

不做：通用 cache 维护 API；把 SMP 篇里的 IPI 握手再写一遍。

---

## 3. 分层与调用方

- 首次 map 还是改映射、走本地还是 all-core：在 `map_handler.c`（页表篇）。
- 切线程时 set / clear mask、本地刷旧 AS：在 `schedule`（VSpace 所有权篇）。
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

用户线程切到另一个用户地址空间时，`schedule` 里的顺序不能乱：

1. `ref_get` 新的 vs；
2. **先**在**新** mask 上 set 本 CPU 这一位；
3. 装硬件根（aarch64 还会带上 ASID，见下篇）；
4. 本地 invalidate **旧** ASID / 旧空间；
5. **再** clear 本 CPU 在**旧** mask 上的位；
6. `ref_put` 旧的。

先 set 再装表，是为了不出「新线程已经跑在新 AS 上，mask 里却还没有本核」这种窗口——这时别人发 shootdown 会漏掉本核。先本地刷再 clear，是为了不出「mask 里已经没有本核，本地 TLB 里还留着脏项」这种情况。

还有一处故意的滞后：下一个线程若不是用户态，schedule **可以不清** mask，也可以不换硬件根。这一位和 CPU 侧引用就一直留着，直到之后某次用户态到用户态的切换才处理。拆除回收要等 mask 空，细节见所有权篇 / ASID 篇。

### 4.2 map 路径：新 vs 改

决策条件**不是**「是不是只有本 CPU」，而是 **「是不是第一次把这个 VA 映射上」**：

- 首次 map（`new_map`）：内核 VA 做本地 kernel invalidate；用户 VA 做本地 `invalidate_page(asid, v)`。假定别人还没缓存过这个翻译。
- remap / unmap：内核走 `*_kernel_page_all_core`；用户走 `*_page_all_core(asid, v, &mask)`。改过、拆过的翻译，跑过这个地址空间的核都得动到。

各 ISA 具体用哪条指令刷 TLB，见 §6.1；cache 相关名词（本仓库尚无通用 API）见 §6.3；函数签名见 §7。

### 4.3 在内核页面也按需映射下，通过 IPI 中断进行tlb清理的注意点

这里需要注意一点就是，IPI本身也是一个中断，所以一定会受到屏蔽中断的影响。然而，参考文档 `07-虚拟地址空间与页表` 里面的设计，它的内核部分，也是按需映射的，这就带来一个巨大的bug。如果有两个核心， A 和 B 都使用 Root vspace 修改内核的页面，A 试图取消一个页面的映射并且发IPI给核心 B ，但是核心 B 关了中断，然后正在使用这个页面，就会因为没有及时的进行tlb flush，产生问题，他可能使用旧的映射，而如果该页面又被分走了，就会发现不知道为什么内容被改掉了。

这件事情在内核侧是非常危险的。实际上，需要考虑的，多个核心需要清理 tlb 时候，需要考虑的页面分为以下几类：

 - 用户部分页面。用户 PTE 的 shootdown 走 mask + IPI（用户态通常开中断）。内核若要碰用户页内容，并不会走用户映射，而是用本核 Map_Handler 槽位映物理页——map handler的槽位是 percpu 的，不受用户 PTE 修改的影响。当然这阻挡不了一个问题，就是如果一个核心 A 释放了用户页面之后，耦合了上面的问题的情况下，核心 B 试图访问一个用户页面，然后这时候核心 A unmap之后的页面被free并被重新分配走了，这会导致核心 B 实际上可能读写一个已经被 A 释放又被分配走的页面，但因为他在cli的关闭中断临界区，无法感知到这个问题，就会出很神秘学的bug。

 - 内核部分的使用kmalloc管理的页面。这部分也是没有问题的。他是恒等映射加percpu。所以一个页面一旦映射走了，只会属于某个核心上的 kmalloc 分配器。该核心可以使用 IPC 的 send，将该页面上的对象，传递给其他核心使用，但是，他不会 remap改页面属性，也不会在其他核心没有通过 MSQ 还回来之前，就 unmap 掉这个页面。所以当他试图 unmap 的时候，一定没有其他核心在使用这个页面，所以只清理该核心上该页面的tlb是安全的。

 - 内核部分不使用kmalloc管理的其他zone的页面。这个，需要在设计其他zone的映射分配机制的时候进行良好的设计。（TODO）

 - 内核部分不属于zone的页面。其中又分为 ensure_kernel_identity_range 函数实现的映射（通常用于生命周期和整个内核一样长的内存部分），和 map_kernel_identity_empty_range （主要用于类似于MMIO等区间进行映射的内存，可能在整个内核生命周期期间，比如类似Linux的insmod和rmmod方式进行更改，但是也属于非常低频率的操作），前者无所谓啊，映射了也不会再去改了。后者内存属于设备嘛，它可能会存在迁移，也就是从A核心到B核心，但是，按照我们的模型，它一定是单 server 在操作这些寄存器，它不会存在同时 AB 两个核心一起使用的情况。所以这样也是没问题的。

 - 其他的启动时候常驻的页面，包括percpu，pmm 元数据，还有map handler自己的窗口，这些虽然是手动构建的，但是根本不会被刷。可以直接忽略。

 - 页表页本身。这个只有map handler里面的窗口tlb要刷，而这是percpu的。其他的情况下只有地址空间被清理掉的时候才清除，所以其实也没啥问题。


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

`tlb_cpu_mask`、切地址空间、以及 `map` 何时本地刷、何时跨核刷，约定在 §4。下面补两件事：硬件实际用什么指令；启动时各核还要不要额外 init。cache 维护和 TLB 不是一回事，附录放在 §6.3。

### 6.1 背景：x86_64 / aarch64 怎样让 TLB 失效旧项

#### x86_64（Intel SDM：TLB 管理 / Multiprocessor）

手册的要求是：改完页表，要用合适的方式把 TLB 里的旧项作废。本实现常用的本地手段是 **`invlpg`**（按线性地址作废一页）。没有 PCID 时，换 **CR3** 会把当前核上这个地址空间相关的用户 TLB 一并刷掉——`arch_tlb_invalidate_all` 一类路径就是重载 CR3。

跨核没有「一条指令让所有核都 `invlpg`」的通用手段，只能靠 **IPI**：本仓库在 `arch_smp_tlb_flush.c` 里给每个 CPU 一条 flush 消息（generation / busy）；发送方先在本地刷，再按 mask 给其他 online 核发软 IPI，然后等对方做到 `done_gen`。APIC 还没就绪时，可能只做本地。文件头注释写明了：**no PCID**。

内核侧的 `arch_tlb_invalidate_kernel_page_all_core` 在源码注释里倾向 **只本地 `invlpg`**（按「内核映射 percpu / 不共享脏翻译」来理解）——以读实现为准，别想当然当成全机广播。

#### aarch64（ARM ARM：TLBI / 屏障）

AArch64 用 **`TLBI`** 系列指令做 TLB 维护，参数里常带 ASID 与页号。本仓库 inline 里能直接对应的有：

| 本仓库 API（概念） | 指令侧 | 含义 |
|--------------------|--------|------------------|
| 本地按页（带 ASID） | `tlbi vae1` | EL1 当前 VMID，按 VA+ASID |
| 跨核按页 | `tlbi vae1is` | 同上，带 **Inner Shareable** 广播 |
| 本地按 ASID 整空间 | `tlbi aside1` | 按 ASID 清 |
| 跨核按 ASID | `tlbi aside1is` | 广播版 |
| 内核页（常不看 ASID） | `tlbi vaale1` / `vaale1is` | 任意 ASID 的该 VA |
| 全清 | `tlbi vmalle1` | 当前 EL1 翻译 |

前后要用 `dsb(ISHST)` / `dsb(ISH)` / `isb` 同步，这是 ARM 规定的 TLBI 上下文同步流程，不是随手加的装饰。`*_all_core` 路径上 **mask 参数常被 `(void)cpu_mask` 丢掉**——硬件广播已经盖住 shareable domain 里的观察者；软件 mask 仍然卡着 `del_vspace` / `vspace_clear_user_mappings(allow_self_use=false)`，避免「还有人可能拿着旧翻译就把 AS 拆了」。

范围失效要用 `rvae1` 一类指令，得有更进的 TLBI 扩展；本实现的 range 路径目前还是会退化成逐页 `vae1`（头文件注释里写明了）。

### 6.2 启动时还要做什么

每核都会走到 `arch_start_core`。里面会 `smp_ipi_init`；x86_64 另外再调 `arch_smp_flush_tlb_init`：给本核准备一条 flush 消息槽，并（全局一次）把 TLB 的软 IPI handler 注册上。aarch64 **没有**对等的「靠 IPI 刷 TLB」的 init——跨核靠 `tlbi *is` 广播，不走这条通道。

### 6.3 附录：内存类型与 cache 维护（尚无通用 API）

树内 **没有** 可移植 `dcache_clean` / `icache_invalidate` 族（aarch64 `sync/cache.h` 为空）。设备 / DMA / 自修改代码接进来之前，先把硬件名词记在这里，免得和 TLB 混为一谈。

#### 6.3.1 x86 内存类型与 PAT

**SDM 卷三表 11-2 的内存类型**（页表 / MTRR / PAT 共同决定最终类型）：

| 类型 | 直观含义 | 写回语义 | 本仓库 |
|------|------|------|--------|
| **UC**（Strong Un-cacheable） | 不缓存，强序 | 不可重排、不可合并 | LAPIC 等 MMIO：`UNCACHED` 编成 `PCD\|PWT` |
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

**缓存拓扑发现：CPUID.02H**（SDM 卷二 CPUID）：EAX=2 一次性枚举 cache / TLB 描述符，调用方按 Intel 公布的查找表解码一字节（例如 `0x0E` = 24 KiB L1 数据 cache、6-way、行 64 B）。core 目前不依赖这些信息——buddy / map_handler 不做 cache-aware 着色；记在这里，供以后做 cache 优化或自修改代码路径时参考。

#### 6.3.2 aarch64 屏障域与 DMB/DSB 参数

ARM 屏障按 **共享域** × **访问类型** 两维取参数。共享域四档：

| 域 | 含义 |
|----|----|
| **nSH**（Non-shareable） | 只当前 PE 访问 |
| **ISH**（Inner Shareable） | 一组 PE 间一致（典型一簇核） |
| **OSH**（Outer Shareable） | 跨外设 / 跨簇一致 |
| **SY**（Full System） | 全局 |

访问类型三档：**LD**（Load-Load / Load-Store，对 Store 后接 Load 不约束）、**ST**（Store-Store）、**SY**（Any-Any，全部约束）。组合出下表（`DMB` 与 `DSB` 共用同一组助记符；`DSB` 额外保证「完成才返回」，`DMB` 只保序）：

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
| `IPAS2E1` | IPA 到 PA 的映射清（EL2 stage-2） |
| `VMALLE1` | 按 VMID 清 EL1&0 |
| `RVA*` / `RVAA*` | Range 失效（ARMv8.4 起；带 `base / num / scale / ttl`） |
| `…L`（如 `VALE1`） | Last level only（只刷最后一级翻译，不刷中间级） |

带 `VM*` 的后缀多与 VMID / 虚拟化翻译相关；RendezvOS v0.1 不开 EL2 stage-2，stage-2 变体不在 inline 里。Range 失效若启用，可把 §7.3 表里 `invalidate_range` 的逐页循环换成单条 `rva*is`，但需要 **FEAT_TLBIRANGE**（ARMv8.4）——目前还是会退化成逐页。

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
| **BS**（bits [3:0]） | 块大小为 2^(BS+2) 字节；BS 最大为 9，对应 2 KiB |

`DC ZVA` 常用于大块用户清零（`memset(p, 0, big)`）：先确认 `DCZID_EL0.DZP==0`，再按块大小（`BS` 算出）逐块清零，比按字节写快得多。core 目前没有这条路径——`map_handler_zero_page` 走普通 `memset`；记在这里，供以后做 zero-on-fault 优化时参考。

改可执行页、加载固件 blob、将来 DMA 缓冲与 CPU 共享时，都需要「先 DC clean 到 PoC / PoU，再 IC invalidate」一类序列——等接进业务路径时再写进 `sync/cache.h`，本篇不假装已经有实现。

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
| 调度切入某用户 AS | 先 **`vs_tlb_cpu_mask_set(vs, cpu)`**，再装 CR3/TTBR（先 set 再装表，避免漏 shootdown） |
| 调度切离 | 先本地 TLBI / 换根，再 **`vs_tlb_cpu_mask_clear`**（先刷再 clear） |
| `map`/`unmap` 改用户 PTE | 按页表篇：首次/更新选本地或 **`arch_tlb_invalidate_page_all_core(asid, va, &vs->tlb_cpu_mask)`** |
| 内核临时窗 / 内核叶 | **`arch_tlb_invalidate_kernel_page`**；`*_all_core` 见下表 ISA 差异 |
| 拆除 / 回收 ASID | 等待 **`vs_tlb_cpu_mask_is_zero`**（mask 空才放行） |
| x86 每核启动 | `arch_start_core` 里调 **`arch_smp_flush_tlb_init()`**（aarch64 无对等 IPI init） |

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

没有单独的 TLB 单元测试；靠 SMP 加上用户 map / unmap / 切换间接覆盖。

---

## 10. 限制与后续

- cache 头文件为空。
- x86 内核 `*_all_core` 是否真广播：以源码为准。
- 与 `32-TLB_shootdown` 篇的分工：本篇讲策略、mask 与硬件对照；那篇写 IPI 握手细节。
- x86 启用 PCID 后，本地 / 远程失效语义会变——属于远期，见 ASID 篇。

---

## 11. 变更记录

- 2026-10-31：§6.1 用户页：槽位路径 + `have_mapped(pin)` / `pmm_free` unpin；去掉「待查 ref」TODO。
- 2026-10-25：全文语言整理：回退 2026-10-04 机械替换造成的生硬（抄录→抄、如何发送→怎么发出去、约束拆除→卡着拆除、位于→在、执行到→走到、失效旧项→把旧项作废、随意添加的装饰→随手加的装饰等），并补顺几处省略过紧的句子，技术内容未动。
- 2026-10-05：§6.3 扩写硬件背景（PAT/CR0/CPUID.02H、DMB 矩阵、TLBI 目录、DC ZVA）。打分前校正：CPUID `0x0E` 按 SDM 为 24 KiB/6-way 而非 64-set×8-way；range TLBI 是 FEAT_TLBIRANGE 不是 TLS；`DC ZVA` 清的是 DCZID 块（2^(BS+2)）不是 cache 行；`ALLE1` 是当前 VMID 的 stage-1 全清。来源：`core/docs/old/cache&tlb.md` 迁移审核。
- 2026-10-04：全文中文表述整理——「再抄一遍」→「再抄录一遍」；「以后另写」→「后续另写」；「对得上」→「保持一致」；「怎么送」→「如何发送」；「留着」→「保留着」；「全机狂刷」→「全机全部刷掉」；「不理」→「忽略」；「卡着」→「约束」；「再写长一遍」→「再详细写一遍」；「落在」→「位于」；「拿着」→「持有」；「不能乱」→「不能打乱」；「人已经跑在…你」→「新线程已经在…本核」；「漏掉你」→「漏掉本核」；「没你了/脏着」→「没有本核/留有脏项」；「留到」→「保留到」；「装上」→「映射上」；「跑过的核都得动到」→「运行过的核都要处理到」；「补两件事」→「补充两件事」；「不是一回事」→「不是同一件事」；「丢掉旧项」→「失效旧项」；「冲掉」→「刷掉」；「靠」→「依赖」；「还没就绪」→「尚未就绪」；「叙事」→「解释」；「想当然当成」→「想当然地当成」；「对上」→「对应」；「点到为止」→「简要说明」；「套路/随便加点装饰」→「流程/随意添加的装饰」；「丢掉」→「丢弃」；「带着」→「持有」；「写了」→「已写明」；「走到」→「执行到」；「注册上」→「注册」；「不走这条通道」→「不经过这条通道」；「接上/混谈」→「接入/混为一谈」；旧稿「记在某处」一类用语改为「记录在此」；「粗控」→「粗粒度控制」；「直观」→「直观含义」；「解成」→「解析为」；「未接」→「未接入」；「整机关缓存」→「整机关闭缓存」；「装固件 blob/接到业务路径时再写进」→「加载固件 blob/接入业务路径时再写入」；「整刷」→「整空间刷」；「单测/靠」→「单元测试/依赖」；「属远期」→「属于远期」；§7「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-10-04：§4.1 补 `vs_tlb_cpu_bitmap_t` / `tlb_cpu_mask` 字段。
- 2026-10-04：§4 只留 mask / map 策略不变量；硬件刷法与 cache 附录迁入 §6.1 / §6.3。
- 2026-10-02：§4.4 附录——x86 内存类型/PAT/CR0.CD·NW；aarch64 PoC/PoU 与 IC/DC（标明尚无 API）。
- 2026-09-27：中文用语整理（teardown→拆除/回收收尾；咬合→衔接；蹲着/揣着→留着/带着）。
- 2026-09-26：§7 全文审阅——各 ISA 的 `tlb.h` / `tlb_cpu_mask.h` 补头文件注释；写清 mask 编排与 x86 IPI vs aarch64 `*is`（含 kernel `*_all_core` 本地-only）。
- 2026-09-25：补 SDM / ARM ARM 硬件对照（`invlpg`、IPI shootdown、`tlbi`/`*is`、屏障）；语言整理；去掉「待加强」备忘。
- 2026-08-29：整篇重做——纠正「仅本 CPU」决策叙事；new/remap；x86 IPI vs aarch64 IS；cache 空头诚实说明。
- 2026-08-26：v0.1 初稿。
