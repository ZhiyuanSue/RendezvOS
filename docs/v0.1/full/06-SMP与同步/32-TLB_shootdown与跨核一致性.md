# TLB shootdown 与跨核一致性

v0.1 · 2026-09-27

本篇覆盖：`arch/x86_64/mm/arch_smp_tlb_flush.c`、x86 / aarch64 `include/arch/*/sync/tlb.h` 中 all_core 路径与软 IPI 的衔接。

**与 `02-内存管理/11-TLB与缓存一致性.md` 分工：**

| | 02（策略 / mask） | **本篇（06）** |
|--|------------------|----------------|
| mask 含义、schedule set→装根页表→刷旧→clear | 主责 | 一句引用 |
| new_map 本地 vs remap/unmap all_core | 主责 | 引用 |
| x86：按 mask 软 IPI + invlpg | 策略层 | **gen/busy/done 握手** |
| aarch64：`tlbi *is`，**常忽略 mask** | 已写 | **禁止再写「via IPI」** |
| 门铃 / pending | → 软 IPI 篇 | 只写 TLB 消息槽如何用 IPI |

软 IPI：唯一 in-tree 默认 registrant 是 **x86 TLB**。VSpace teardown 门闩见 VSpace 所有权篇。

**官方手册：** Intel SDM — `invlpg`、换 CR3 对 TLB 的影响（无 PCID（Process Context ID，进程上下文标识）时）；ARM ARM — `TLBI …IS`、`DSB`/`ISB`。

---

## 1. 概述

修改用户 PTE（Page Table Entry，页表项）之后，凡 **可能缓存该 AS（Address Space，地址空间）翻译** 的 CPU 必须 invalidate（失效对应 TLB 项）——这一跨核失效过程即所谓 TLB shootdown。`tlb_cpu_mask` 回答「谁可能脏 / 能不能拆」；**如何刷新由 arch 决定**。

x86 通过 IPI 触发刷新；aarch64 通过硬件广播刷新。

| | x86 | aarch64 |
|--|-----|---------|
| all_core | 本地先刷 → 按 mask 发软 IPI → 等 done | `tlbi …is` + DSB/ISB；**mask 参数常 `(void)`** |
| init | `smp_ipi_init` + **`arch_smp_flush_tlb_init`** | 仅 `smp_ipi_init`（**无** TLB-IPI init） |
| mask 仍用于 | 选谁收 IPI + teardown | **仅** teardown / clear 门闩 |

没有可靠 mask + 刷干净，页表篇「按需 unmap 后应 fault」在多核上无法兑现。

### 1.1 硬件差异为何逼出两套协议

- **x86：** 没有「一条总线广播让所有核 `invlpg`」的通用指令。修改其他核上的 TLB 必须 **通过 IPI 通知对方核自行执行** `invlpg`（或重载 CR3）。门铃 = 软 IPI 向量 `0x30`；参数 = 目标核 per-CPU 消息槽。
- **aarch64：** `TLBI …IS`（Inner Shareable）可让本 shareability domain 内的 PE（Processing Element，处理单元）失效对应项，再配 `DSB`/`ISB`。因此 all_core **不走**软 IPI；mask 仍卡 teardown。

#### 1.1.1 x86 TLB 失效指令

x86 提供 **页级** 与 **地址空间级** 两档失效指令，无 PCID 时全靠它们：

- **`invlpg m`**：失效 TLB 中映射线性地址 `m` 的页（4KB / 大页一并失效）。**只刷当前核**，不广播——跨核要 IPI。
- **`invlpga`**：按 ASID 失效（AMD 扩展，本仓库未用）。
- **重载 CR3**：`mov cr3, reg` 在无 PCID 时**全刷**非 global TLB 项（global 项保留）；有 PCID 时按 PCID 选择性失效。本仓库 `arch_smp_flush_all_tlb` 用 CR3 重载式全刷。
- **`invpcid`**（若 CPUID 支持）：按 PCID 精细失效，本仓库未用。

global 页（PTE G 位）不随 CR3 重载失效——内核映射常用 global，故改用户 PTE 不会误刷内核 TLB。

#### 1.1.2 aarch64 TLB 失效指令

ARM 上 TLB 失效靠 **`TLBI`** 指令族，按广播范围与目标粒度分类：

- **广播范围后缀**：
  - `…nsh`：Non-shareable，仅本 PE。
  - `…is`：Inner Shareable，广播到本 inner 共享域（典型 SMP 簇）——本仓库 `*_all_core` 走这条。
  - `…os`：Outer Shareable。
  - （无后缀）：仅本 PE（与 `nsh` 等价）。
- **目标粒度**：
  - `tlbi vae1is`：按 VA（Virtual Address，虚拟地址）失效当前 ASID 的页。
  - `tlbi aside1is`：按指定 ASID（Address Space ID，地址空间标识）失效所有 VA（vspace 级）。
  - `tlbi alle1is`：失效所有 user ASID。
  - `tlbi vmalle1is`：失效所有 VA + ASID + stage1。
- **配套屏障**：`tlbi` 后必须 **`dsb sy`**（等失效对全系统完成）再 **`isb`**（刷新取指上下文，确保后续指令用新映射）。`tlbi` 本身只是「发广播」，不等完成。

aarch64 因此**不需要** IPI 来跨核刷 TLB——一条 `tlbi …is` 就是硬件广播。代价是广播成本与 mask 解耦（无法只刷部分核）。

---

## 2. 目标与边界

**提供：** x86 shootdown 握手细节；与 soft IPI / mask 的衔接；aarch64「不走 IPI」的边界句。

**不做：** mask 维护全文（02）；门铃位图协议全文（软 IPI）；cache API（02 已声明空）；batching 框架。

---

## 3. 分层与调用方

**MM** — remap / unmap 后 `arch_tlb_invalidate_*_all_core(..., &vs->tlb_cpu_mask)`。
**schedule** — 本地刷旧 AS + set / clear mask（02 / VSpace）。
**规则：** 改 PTE 持适当锁；IPI handler **只刷**，不锁、不 sleep。

---

## 4. 数据结构与不变量

### 4.1 x86 每核消息槽（字段所有权）

```c
struct smp_tlb_flush_msg {
        atomic64_t request_gen;
        atomic64_t done_gen;
        atomic64_t busy;
        vaddr flush_va;
        bool flush_all;
};
```

| 字段 | 谁写 | 谁读 / 约定 |
|------|------|-------------|
| `busy` | **发送方** `exchange→1` 抢占；成功路径由 **handler** 清 0；**仅** `smp_ipi_send` 失败时发送方自清 | 同目标串行化 |
| `flush_va` / `flush_all` | **发送方**在持有 `busy` 后写 | handler 读后执行 `invlpg` / CR3 全刷 |
| `request_gen` | **仅发送方** `fetch_inc` | handler `load` 作完成标签 `cur` |
| `done_gen` | **仅 handler** 刷完后 `store(cur)` | 发送方自旋至 `done ≥ expect`（`expect=fetch_inc+1`） |

- 单槽 + `busy`：同目标 CPU 上并发 shootdown **串行化**；无 batching。
- **无 PCID**：页级=`invlpg`；vspace 级为 reload CR3 / 全刷；asid 参数 `(void)`。

### 4.2 mask 与并发

unmap 读 mask 发 IPI 时 **不**再持有 `tlb_cpu_mask_lock`——正确性靠 02 的「先 set 再进 AS / 先刷再 clear」。teardown 仍在 CAS 下查 mask。

### 4.3 aarch64

`*_all_core` → `tlbi vae1is` / `aside1is` / …；mask 显式忽略。ASID 进 `tlbi`（与 x86 装饰对称不同）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `arch_smp_tlb_flush.c` | x86 握手、init、register |
| `arch/*/sync/tlb.h` | invalidate API |
| `kernel/smp/ipi.c` | 门铃 |
| `map_handler` / schedule | 调用点（02） |

---

## 6. 流程

### 6.1 x86 shootdown（发送方）

```text
1. 本地先刷（invlpg 或全刷）
2. mask 空 / !smp_tlb_ipi_ready / 非 xAPIC|x2APIC → 只本地，返回
3. 对 mask 每位（跳过 self、非 online）：
     自旋抢 busy
     写 flush_all / flush_va
     expect = fetch_inc(request_gen) + 1
     smp_ipi_send(cpu, smp_tlb_ipi_id)
       失败 → 清除 busy，返回（不保证远端已刷新）
     自旋直到 done_gen >= expect
4. Handler：读 request_gen → 刷 → done_gen=cur → busy=0
```

#### 6.1.1 跨核 shootdown 时序

```text
  发送方 CPU A                      CPU B (mask 内)        CPU C (mask 内)
  ─────────────                     ─────────────          ─────────────
  ① 本地 invlpg / 全刷
  ② 抢 B 的 busy (exchange→1)
     写 B.flush_va
     expect_B = fetch_inc(B.req_gen)+1
  ③ smp_ipi_send(B, tlb_id)
     └──▶ ICR (dest=B, vec=0x30) ──▶ ④ IRQ
                                       ⑤ 读 B.req_gen=cur
                                       ⑥ invlpg B.flush_va
                                       ⑦ B.done_gen = cur
                                       ⑧ B.busy = 0
  ⑨ 自旋等 B.done_gen ≥ expect_B ◀─── (释放 busy)
  ⑩ 抢 C 的 busy ...（串行逐核）
```

要点：A 对每个 mask 内的核**串行**处理（单槽，无 batching）；B 的 handler 只读消息槽、刷、回写 done，**不锁、不 sleep**。A 等 done 才继续下一核，故 A 返回时所有 mask 内核都已刷完。门铃失败（如目标核离线）则 A 清除 busy 返回，**不保证**远端已刷——调用方需知此限制。

Init：`arch_start_core` → `smp_ipi_init` + **`arch_smp_flush_tlb_init`**（每核槽；**全局一次** `smp_ipi_register`）。

### 6.2 aarch64

调用 `*_all_core` 即 IS 广播；无消息槽、无等 done。mask 仍卡 `vspace_clear(..., false)` / `del_vspace`。

### 6.3 内核 `*_kernel_page_all_core`（x86）

倾向于 **本地 invlpg**（percpu 内核映射语境）——勿暗示按 mask 广播。以源码为准（02 已据实说明）。

---

## 7. 公开 API

本篇涉及的接口分布在：x86 **shootdown 握手**——`arch_smp_flush_tlb_init` / `arch_smp_flush_page_tlb` / `arch_smp_flush_all_tlb`，以及 per-CPU `smp_tlb_flush_msg`（gen/busy/done）字段约定。说明改写自 `arch/x86_64/sync/tlb.h` Doxygen + `arch_smp_tlb_flush.c`（已核对）。

**本篇不涉及：** `tlb_cpu_mask` 维护 / schedule set→刷→clear → `11`；可移植 `arch_tlb_invalidate_*` 命名与 aarch64 `tlbi *is` 细节 → `11`；软 IPI pending 位图 → `30`。

aarch64：**无**本篇专属 shootdown API——`*_all_core` 即硬件广播（见 `11`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| x86 每核 | `smp_ipi_init` → **`arch_smp_flush_tlb_init`**（首调者全局 `smp_ipi_register`） |
| MM remap/unmap 用户页 | 改 PTE 后 `arch_tlb_invalidate_page_all_core(…, &mask)` → 内部调用 **`arch_smp_flush_page_tlb`** |
| vspace 级 | `…_vspace_page_all_core` → **`arch_smp_flush_all_tlb(mask)`** |
| 发送路径 | 本地先刷 → 按 mask 对 remote：抢 busy → 写参 → `smp_ipi_send` → **等 done_gen** |

IPI handler **只刷**；不拿 `tlb_cpu_mask_lock`、不 sleep。

### 7.2 x86 shootdown API

```c
void arch_smp_flush_tlb_init(void);
void arch_smp_flush_page_tlb(vaddr addr, const vs_tlb_cpu_bitmap_t *cpu_mask);
void arch_smp_flush_all_tlb(const vs_tlb_cpu_bitmap_t *cpu_mask);
```

| 接口 | 说明 |
|------|------|
| `arch_smp_flush_tlb_init` | 初始化本核消息槽；非 APIC 直接 return。`smp_tlb_ipi_ready` 只置一次。 |
| `arch_smp_flush_page_tlb` | 本地 `invlpg` + 对 mask 内 online 核发页级 IPI。 |
| `arch_smp_flush_all_tlb` | 本地 CR3 重载式全刷 + remote `flush_all`。 |

前置：`smp_tlb_ipi_ready` 且 `arch_irq_type` 为 xAPIC/x2APIC；否则仅本地。

### 7.3 消息槽约定（`smp_tlb_flush_msg`；所有权见 §4.1）

```c
struct smp_tlb_flush_msg {
        atomic64_t request_gen, done_gen, busy;
        vaddr flush_va;
        bool flush_all;
};
DEFINE_PER_CPU(..., smp_tlb_flush_message);
```

| 字段 / 步骤 | 说明 |
|-------------|------|
| `busy` | 发送方占用；handler 成功清 0；**发送方只在门铃失败时清除**——发送方与 handler 勿随意双方写入。 |
| `request_gen` | **仅发送方** `fetch_inc`；handler 只 load。 |
| `done_gen` | **仅 handler** store；发送方只 wait。 |
| `flush_*` | 发送方在持 busy 后写；handler 只读。 |
| send 失败 | 发送方清除 `busy` 并 return——**不保证**远端已刷（`request_gen` 已递增，无对应 done）。 |
| 无 PCID | 页级=`invlpg`；全刷为 reload CR3；`asid` 在可移植包装里被忽略。 |

### 7.4 与 `11` 的对照（勿重复维护）

| 可移植名（`11`） | x86 落到 | aarch64 |
|------------------|----------|---------|
| `…_page_all_core` | `arch_smp_flush_page_tlb` | `tlbi vae1is`（忽略 mask） |
| `…_vspace_page_all_core` | `arch_smp_flush_all_tlb` | `tlbi aside1is` |
| `kernel_page_all_core` | **仅本地** invlpg | `tlbi vaale1is` |

---

## 8. 多架构

上表。riscv / loongarch 非主线。

---

## 9. 测试

间接：SMP + 用户 map / unmap。

---

## 10. 限制与后续

- x86 send 失败无重试。
- 单槽串行；无 batching。
- aarch64 广播成本与 mask 解耦。
- 无 PCID。

---

## 11. 变更记录

- 2026-10-04：§1.1.1/1.1.2 补 x86 TLB 失效指令（invlpg、invlpga、CR3 重载、invpcid、global 页）与 aarch64 TLBI 指令族（nsh/is/os 广播范围、vae1is/aside1is/alle1is/vmalle1is 粒度、dsb+isb 配套）；§6.1.1 加跨核 shootdown 时序图（A 串行处理 mask 内每核、B handler 只刷不锁、门铃失败不保证远端刷到）。
- 2026-09-27：中文措辞整理——「写死 / 契约 / 真源」改为分工表与「约定」；保留字段所有权语义。
- 2026-09-26：对照 `tlb.h` Doxygen / `arch_smp_tlb_flush.c`——补 **字段所有权**（busy/request_gen/done_gen/flush_* 谁写谁读）；send 失败时 `request_gen` 已递增无 done。
- 2026-09-26：§7 全文审阅——强化 `arch_smp_flush_*` Doxygen（gen/busy/done）；划清拥有（握手）vs `11`（mask / 可移植名）vs `30`（门铃）。
- 2026-09-25：语言整理；§1.1 补「为何 x86 必须 IPI、aarch64 用 tlbi is」。
- 2026-08-29：整篇重做——删「aarch64 via IPI」；x86 gen/busy/done；与 02 / 软 IPI 边界；send 失败与单槽限制。
- 2026-08-27：初稿（误写 aarch64 IPI）。
- 2026-10-05：任务 1/3/5 精读——补全 shootdown/PTE/AS/ASID/PCID/PE/VA 缩写首现释义；「改了/敲门刷/IPI 过去让对方自己执行/拿锁/刷到/清 busy/叙事/诚实/内调/claim/双边写/≈」等口语与符号改正式中文。
- 2026-10-05：最终词句顺畅。
