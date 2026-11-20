# TLB shootdown 与跨核一致性

v0.1 · 2026-09-27

本篇覆盖：`arch/x86_64/mm/arch_smp_tlb_flush.c`、x86 / aarch64 `include/arch/*/sync/tlb.h` 中 all_core 路径与软 IPI 的衔接。

**与 TLB 篇（`11`）分工：**

| | TLB 篇（策略 / mask） | **本篇** |
|--|------------------|----------------|
| mask 含义、schedule 里 set / 装根页表 / 刷旧 / clear | 主责 | 一句引用 |
| new_map 本地 vs remap/unmap all_core | 主责 | 引用 |
| x86：按 mask 软 IPI + invlpg | 策略层 | **gen/busy/done 握手** |
| aarch64：`tlbi *is`，**常忽略 mask** | 已写 | **禁止再写「via IPI」** |
| 门铃 / pending | 见软 IPI 篇（`30`） | 只写 TLB 消息槽如何用 IPI |

软 IPI：唯一 in-tree 默认 registrant 是 **x86 TLB**。VSpace teardown 门闩见所有权篇（`15`）。

**官方手册：** Intel SDM — `invlpg`、换 CR3 对 TLB 的影响（无 PCID（Process Context ID，进程上下文标识）时）；ARM ARM — `TLBI …IS`、`DSB`/`ISB`。

---

## 1. 概述

修改用户 PTE（Page Table Entry，页表项）之后，凡 **可能缓存该 AS（Address Space，地址空间）翻译** 的 CPU 必须 invalidate（失效对应 TLB 项）——这一跨核失效过程即所谓 TLB shootdown。`tlb_cpu_mask` 回答「谁可能脏 / 能不能拆」；**如何刷新由 arch 决定**。

x86 通过 IPI 触发刷新；aarch64 通过硬件广播刷新。

| | x86 | aarch64 |
|--|-----|---------|
| all_core | 本地先刷，再按 mask 发软 IPI，并等 done | `tlbi …is` + DSB/ISB；**mask 参数常 `(void)`** |
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
- **重载 CR3**：`mov cr3, reg` 在无 PCID 时**全刷**非 global TLB 项（global 项保留）；有 PCID 时按 PCID 选择性失效。本仓库的全刷与 vspace 级本地失效都用同一惯用法——「**读回写回**」：先 `mov %%cr3, %reg` 再 `mov %reg, %%cr3`，寄存器值原样不动，触发的是「写 CR3」这一动作的失效效果，而非更换页表基址（`arch_tlb_invalidate_all` / `arch_tlb_invalidate_vspace_page`，进而 `arch_smp_flush_all_tlb` 的本地一档）。
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
  - **带不带 `is` 是两条指令**：本地刷与广播刷同根配对——`vae1`/`vae1is`、`vaale1`/`vaale1is`、`aside1`/`aside1is`、`vmalle1`/`vmalle1is`。本仓库 `tlb.h` 的本地 `arch_tlb_invalidate_*` 用无 `is` 版，`*_all_core` 用带 `is` 版。
- **配套屏障**：本仓库每个失效 inline 的固定序列是 **`dsb ishst`**（保证此前的 PTE 写入先对失效操作可见、且排在其前——否则 `tlbi` 可能先于新 PTE 到达执行，之后 walk 又学回旧项）→ `tlbi` → **`dsb ish`**（等失效在内层共享域内完成）→ **`isb`**（刷新取指上下文，确保后续指令用新映射）。`tlbi` 本身只是「发广播」，不等完成；尾部 `dsb` 取 **ISH** 域，与 `…is` 的广播范围恰好对齐，无须 `dsb sy`（全系统，更贵）。

aarch64 因此**不需要** IPI 来跨核刷 TLB——一条 `tlbi …is` 就是硬件广播。代价是广播成本与 mask 解耦（无法只刷部分核）。

---

## 2. 目标与边界

**提供：** x86 shootdown 握手细节；与 soft IPI / mask 的衔接；aarch64「不走 IPI」的边界句。

**不做：** mask 维护全文（见 `11`）；门铃位图协议全文（见 `30`）；cache API（`11` 已声明空）；batching 框架。

---

## 3. 分层与调用方

**MM** — remap / unmap 后 `arch_tlb_invalidate_*_all_core(..., &vs->tlb_cpu_mask)`。
**schedule** — 本地刷旧 AS + set / clear mask（`11` / 所有权篇）。
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
| `busy` | **发送方** 用 `exchange` 写成 1 抢占；成功路径由 **handler** 清 0；**仅** `smp_ipi_send` 失败时发送方自清 | 同目标串行化 |
| `flush_va` / `flush_all` | **发送方**在持有 `busy` 后写 | handler 读后执行 `invlpg` / CR3 全刷 |
| `request_gen` | **仅发送方** `fetch_inc` | handler `load` 作完成标签 `cur` |
| `done_gen` | **仅 handler** 刷完后 `store(cur)` | 发送方自旋至 `done ≥ expect`（`expect=fetch_inc+1`） |

- 单槽 + `busy`：同目标 CPU 上并发 shootdown **串行化**；无 batching。
- **无 PCID**：页级=`invlpg`；vspace 级为 reload CR3 / 全刷；asid 参数 `(void)`。

### 4.2 mask 与并发

map/unmap 读 mask 发 IPI 时只持 `vs->vspace_lock`（MCS），**不**持 `tlb_cpu_mask_lock`（`map_handler.c` 的 `unmap()` 在 `unlock_mcs` 之前调 `arch_tlb_invalidate_*_all_core(..., &vs->tlb_cpu_mask)`）。正确性靠 `11` 已述的 mask 纪律，其代码锚点在 `task_manager.c` 的 `schedule()`：**进 AS 前先 set**——`vs_tlb_cpu_mask_set(new_vs, ...)` 在 `arch_set_current_user_vspace_root_asid()` 装根页表**之前**、于 `tlb_cpu_mask_lock` 下完成；**切走后先刷再 clear**——先 `arch_tlb_invalidate_vspace_page(old_vs->asid, 0)` 本地刷掉旧 AS，再在锁下 `vs_tlb_cpu_mask_clear()`。由此可得 shootdown 需要的归纳不变量：mask 说「核 X 可能脏」时，X 要么已在该 AS 内跑（收得到并会执行 IPI 刷新），要么尚未进入（其下次取翻译必然重新 walk 页表，不会用陈旧项）。

teardown 仍在 CAS 下查 mask：`vmm.c` 的 `vspace_clear_check_tlb()` 持 `tlb_cpu_mask_lock` 要求 mask **全零**才放行（`del_vspace` 传 `allow_self_use=false`；仅当发起核自己仍在使用该 AS 时允许传 `true` 并容忍 self 位），否则返回 `-E_REND_RC_UNEQUAL` 拒绝拆解——页表页在「任何核不再可能脏」之前不得释放。

### 4.3 aarch64

`*_all_core` 落到 `tlbi vae1is` / `aside1is` / `vaale1is`；mask 显式 `(void)` 忽略。ASID 进 `tlbi` 操作数（`tmp = (asid << 48) | (VA >> 12)`），与 x86 的装饰性对称不同——x86 无 PCID，asid 参数纯占位。另有一条边界：`aside1`/`aside1is`/`arch_tlb_invalidate_range` 的 ASID 操作数字段仅 16 bit，代码对 `asid >= 2^16` **静默 return**（见 §10）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `arch_smp_tlb_flush.c` | x86 握手、init、register |
| `arch/*/sync/tlb.h` | invalidate API |
| `kernel/smp/ipi.c` | 门铃 |
| `map_handler` / schedule | 调用点（`11`） |

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

Init：x86 `boot/start_arch.c` 的 `arch_start_core()` 每核执行，依次 `smp_ipi_init()` 与 **`arch_smp_flush_tlb_init()`**——后者重初始化**本核**消息槽（三个 `atomic64_init` + 字段清零），而 `smp_ipi_register` 由 `smp_tlb_ipi_ready` 门闩保证**全局只注册一次**（首调者完成）。槽重初始化无并发窗口：发送方只会触碰 mask 内且 `cpu_is_online()` 的核，而新核要到**首次切入某用户地址空间**（`schedule()` 里 set mask）才可能进入任何 mask——它自己 init 槽的那一刻，不可能有在途 shootdown 指向该槽。

### 6.2 aarch64

调用 `*_all_core` 即 IS 广播；无消息槽、无等 done。mask 仍卡 teardown 门闩：`del_vspace` → `vspace_clear_user_mappings(..., allow_self_use=false)` → `vspace_clear_check_tlb`（§4.2）——硬件广播替 mask 里的核刷了，但「能不能拆」的判断照旧。

### 6.3 内核 `*_kernel_page_all_core`（x86）

x86 的 `arch_tlb_invalidate_kernel_page_all_core` 名为 all_core，实现是**仅本地 `invlpg`**——`tlb.h` 源码注释对这一「名实不符」给出了完整理由，此处忠实转述：

1. **为什么不敢依赖 IPI**：x86 shootdown 靠软 IPI，而 IPI 会被 `cli` 屏蔽。若核 A unmap 时核 B 恰在 `cli` 区间内，B 收不到门铃，A 也就等不到它的 done；随后该物理页若被重新分配并改作他用，B 仍可能经由旧翻译访问，造成跨核静默破坏。
2. **为什么仅本地也够**：靠的是 kmalloc 的 **percpu 生命周期纪律**，而非硬件。需要被 unmap 的内核页只来自 percpu kmalloc——对象由哪个核分配，释放（进而 unmap）就只能由该核执行，且必须确认无其他核仍持有；释放后经 **MSQ**（`ms_queue_t`，`kmalloc.c` 的每核回收队列，`kfree` 把对象 `msq_enqueue` 回 **owner** 分配器的队列）回到分配核手里。于是「A 执行 unmap 的那一刻」可归纳出：没有任何核还持有该内核对象——**访问**并不受限（指针可经 IPC 传给 B 用），受约束的是**生命周期**（谁 alloc 谁 free）。B 既然不持有，就不会再触碰那条翻译；A 自己的旧项已被本地 `invlpg` 刷掉。

源码注释里那句 "this percpu is used just for emmmm, lifecycle" 就是这个意思：percpu 属性不是用来限制访问的，是用来锚定释放权的。另注：aarch64 的同名函数是**真广播**（`tlbi vaale1is`）——两 arch 对同一可移植名给出不同强度，是 per-arch 实现选择，勿当矛盾读（对照表见 §7.4；调用侧的适用语境见 `11`）。

---

## 7. 公开 API

接口分布在：x86 **shootdown 握手**——`arch_smp_flush_tlb_init` / `arch_smp_flush_page_tlb` / `arch_smp_flush_all_tlb`，以及 per-CPU `smp_tlb_flush_msg`（gen/busy/done）字段约定。说明改写自 `arch/x86_64/sync/tlb.h` Doxygen + `arch_smp_tlb_flush.c`（已核对）。

**本篇不涉及：** `tlb_cpu_mask` 维护 / schedule 的 set、刷、clear（见 `11`）；可移植 `arch_tlb_invalidate_*` 命名与 aarch64 `tlbi *is` 细节（见 `11`）；软 IPI pending 位图（见 `30`）。

aarch64：**无**本篇专属 shootdown API——`*_all_core` 即硬件广播（见 `11`）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| x86 每核 | `smp_ipi_init` 之后 **`arch_smp_flush_tlb_init`**（首调者全局 `smp_ipi_register`） |
| MM remap/unmap 用户页 | 改 PTE 后调 `arch_tlb_invalidate_page_all_core(…, &mask)`，内部再调 **`arch_smp_flush_page_tlb`** |
| vspace 级 | `…_vspace_page_all_core` 落到 **`arch_smp_flush_all_tlb(mask)`** |
| 发送路径 | 本地先刷；再按 mask 对 remote 抢 busy、写参、`smp_ipi_send`，并 **等 done_gen** |

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
| `kernel_page_all_core` | **仅本地** invlpg（§6.3） | `tlbi vaale1is` |

### 7.5 不走 shootdown 的本地 API（`tlb.h`）

除 §7.2 的握手三件套外，`tlb.h` 还提供纯本地接口——只刷当前核，供 new_map、schedule 切出等「本核刚改完本核要用」的场合：

| 接口 | x86 | aarch64 | 现役调用点 |
|------|-----|---------|-----------|
| `arch_tlb_invalidate_page(asid, addr)` | `invlpg`（asid 忽略） | `tlbi vae1` | new_map 本地档（`map_handler.c`） |
| `arch_tlb_invalidate_kernel_page(addr)` | `invlpg` | `tlbi vaale1` | 内核 new_map / handler 页回收 |
| `arch_tlb_invalidate_vspace_page(asid, addr)` | CR3 读回写回（两参忽略） | `tlbi aside1`（addr 忽略） | `schedule()` 刷旧 AS（§4.2） |
| `arch_tlb_invalidate_all` | CR3 读回写回 | `tlbi vmalle1` | IPI handler 的 `flush_all` 档 |
| `arch_tlb_invalidate_range(asid, start, end)` | 逐页 `invlpg` 循环 | 逐页 `tlbi vae1` 循环 | **全树暂无调用者** |

两点补充：

- `range` 是批量接口雏形：x86 版逐页 `invlpg` 本就无广播，逐条执行没有额外代价；aarch64 版把 N 条 `tlbi vae1` 夹在**一对** `dsb ish`/`isb` 之间——多条失效共享一次屏障，这是它与逐页 `page` 版（每条自带一对屏障）在屏障开销上的实质差异。其代码注释注明 `rvae1`（ARMv8.4 range TLBI 扩展）未启用，故仍为逐页循环。
- x86 本地版全部是裸 `invlpg` / CR3 写，**不带**任何 `mfence` 类屏障（x86 TSO 下 `mov` 写 PTE 与 `invlpg` 天然有序）；aarch64 则每个入口都自带 §1.1.2 的四拍序列。

---

## 8. 多架构

上表。riscv / loongarch 非主线。

---

## 9. 测试

无专用测试用例（`modules/test` 无对 `smp_tlb_flush_*` / `*_all_core` 的直接调用）。间接覆盖：x86 SMP 下用户 map / unmap 会走一遍 §6.1 全链路（抢 busy → 写参 → 门铃 → 等 done）。send 失败的 busy 自清、done 自旋、teardown 全零门闩无定向测量。

---

## 10. 限制与后续

- x86 send 失败无重试。
- 单槽串行；无 batching。
- aarch64 广播成本与 mask 解耦。
- 无 PCID。
- aarch64 对 `asid >= 2^16` 的 vspace 级失效与 range 静默 return（`tlbi` 操作数 ASID 字段 16 bit），依赖 ASID 分配器不产出越界值。
- `arch_tlb_invalidate_range` 两 arch 均已定义但**未接线**；且 x86 版本质仍是逐页 `invlpg`，并不比逐页调用省指令，接线前只是占位。

---

## 11. 变更记录
- 2026-11-19：底子首评批次——§7 opener；§9 测例口径。事实沿用 11-15。
- 2026-11-15：Maintainer 复审前可读性通读——修旧目录编号残留：分工表列头与 §2/§3/§5 各处裸「02」「06」实为旧目录号（`02-内存管理/`、`06-SMP与同步/`），易与篇 `02`（启动流程总览）混淆，统一改 `11` / `30` 显式编号；篇名引用长文件名缩为「TLB 篇（`11`）」；「VSpace 所有权篇」按全仓先例归一为「所有权篇（`15`）」；§9 按测例树实况扩写（无直接测例，链路由用户 map/unmap 间接覆盖）。技术内容未动（busy/gen/done 所有权、teardown 门闩、`asid >= 2^16` 静默 return 均按当前代码复验）。

- 2026-11-14：全文对照代码复核与扩写——§1.1.2 屏障序列按 `tlb.h` 实况修正（`dsb ishst`→`tlbi`→`dsb ish`→`isb`，ISH 域与 `…is` 广播对齐；原文误作 `dsb sy`），并补本地/广播助记符配对；§1.1.1 CR3 失效为「读回写回」惯用法；§4.2 补代码锚点（unmap 持 `vspace_lock` 而非 `tlb_cpu_mask_lock`、`schedule()` 先 set 后装根/先刷后 clear 的顺序、`vspace_clear_check_tlb` 的 mask 全零门闩与 `-E_REND_RC_UNEQUAL`）；§4.3 补 `asid >= 2^16` 静默 no-op；§6.1 补 init 边界（每核槽重初始化无并发窗口的论证）；§6.2 更正 teardown 函数名；§6.3 依源码注释扩写 x86 内核页「仅本地刷」理由（`cli` 可屏蔽 IPI + percpu kmalloc 生命周期纪律 + MSQ 回投分配核）；新增 §7.5（本地 API 一览与 `range`——两 arch 已定义、全树无调用者）；§10 补两条限制。
- 2026-10-05：任务 1/3/5 精读——补全 shootdown/PTE/AS/ASID/PCID/PE/VA 缩写首现释义；「改了/敲门刷/IPI 过去让对方自己执行/拿锁/刷到/清 busy/叙事/诚实/内调/claim/双边写/≈」等口语与符号改正式中文。
- 2026-10-05：最终词句顺畅。
- 2026-10-04：§1.1.1/1.1.2 补 x86 TLB 失效指令（invlpg、invlpga、CR3 重载、invpcid、global 页）与 aarch64 TLBI 指令族（nsh/is/os 广播范围、vae1is/aside1is/alle1is/vmalle1is 粒度、dsb+isb 配套）；§6.1.1 加跨核 shootdown 时序图（A 串行处理 mask 内每核、B handler 只刷不锁、门铃失败不保证远端刷到）。
- 2026-09-27：中文措辞整理——「写死 / 契约 / 真源」改为分工表与「约定」；保留字段所有权语义。
- 2026-09-26：对照 `tlb.h` Doxygen / `arch_smp_tlb_flush.c`——补 **字段所有权**（busy/request_gen/done_gen/flush_* 谁写谁读）；send 失败时 `request_gen` 已递增无 done。
- 2026-09-26：§7 全文审阅——强化 `arch_smp_flush_*` Doxygen（gen/busy/done）；划清拥有（握手）vs `11`（mask / 可移植名）vs `30`（门铃）。
- 2026-09-25：语言整理；§1.1 补「为何 x86 必须 IPI、aarch64 用 tlbi is」。
- 2026-08-29：整篇重做——删「aarch64 via IPI」；x86 gen/busy/done；与 02 / 软 IPI 边界；send 失败与单槽限制。
- 2026-08-27：初稿（误写 aarch64 IPI）。
