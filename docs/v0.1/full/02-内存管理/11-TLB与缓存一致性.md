# TLB 与缓存一致性

v0.1 · 2026-09-25

本篇覆盖：`include/rendezvos/mm/tlb_cpu_mask.h`、`include/arch/x86_64/sync/tlb.h`、`include/arch/aarch64/sync/tlb.h`、`arch/x86_64/mm/arch_smp_tlb_flush.c`；以及 `schedule` / `map_handler` 里和它们的交界。软 IPI 的握手细节见 `06-SMP与同步/30-软IPI机制.md` 与 `32-TLB_shootdown与跨核一致性.md`——本篇不把那一套再抄一遍。

关于标题里的「缓存」：aarch64 的 `sync/cache.h` 目前是空的，树里也没有对称的 x86 `cache.h`。本篇**没有**通用 dcache/icache 维护 API；设备 / DMA 路径以后另写。这里说的「一致性」，主要指 **CPU 侧页表翻译（TLB）** 在改 PTE、换地址空间之后还能不能对得上。

何时在 `map` / `unmap` 里调哪一类 invalidate，页表篇 §6.2 已经钉过；本篇把 **mask 是什么意思、跨核怎么送、和调度怎么咬合**，以及两边硬件指令分别对应什么，讲清楚。

---

## 1. 概述

改了 PTE，或者拆了用户映射之后，别的核上的 TLB 里可能还蹲着旧翻译。若不知道「谁跑过这个地址空间」，要么不敢拆 AS，要么只能全机狂刷。

core 的答案分两层：

1. **`VSpace::tlb_cpu_mask`**：哪些 CPU **可能**还缓存着这个用户地址空间的翻译。
2. **arch 的 invalidate API**：本地刷，或者跨核。x86 走 **按 mask 的软 IPI + `invlpg`**；aarch64 在用户 / 跨核路径上多用 **`tlbi …is` 广播**（常常**不理** mask 参数），但 mask 仍然卡着 teardown 能不能放行。

mask 回答「该通知谁、能不能拆」；具体怎么刷是架构的事。少了任一层，要么漏刷，要么 ASID 不敢回收（见 ASID 篇）。

---

## 2. 目标与边界

提供：mask 类型与调度维护约定；与 `map_handler` / `schedule` 对齐的 invalidate 策略；x86 IPI shootdown 入口；两边硬件指令与官方手册的对照。

不做：通用 cache 维护 API；把 SMP 篇里的 IPI 握手再写长一遍。

---

## 3. 分层与调用方

- 首次 map 还是改映射、走本地还是 all-core：落在 `map_handler.c`（页表篇）。
- 切线程时 set / clear mask、本地刷旧 AS：落在 `schedule`（VSpace 所有权篇）。
- x86 远程 `invlpg`：`arch_smp_tlb_flush.c` + 软 IPI。
- aarch64 的 IS 广播：主要在 `arch/aarch64/sync/tlb.h` 的 inline 里。

---

## 4. 数据结构与不变量

### 4.1 `tlb_cpu_mask`

位图宽度是 `RENDEZVOS_MAX_CPU_NUMBER`，改的时候要拿着 `tlb_cpu_mask_lock`（CAS）。

用户线程切到另一个用户地址空间时，`schedule` 里的顺序不能乱：

1. `ref_get` 新的 vs；
2. **先**在**新** mask 上 set 本 CPU 这一位；
3. 装硬件根（aarch64 还会带上 ASID，见下篇）；
4. 本地 invalidate **旧** ASID / 旧空间；
5. **再** clear 本 CPU 在**旧** mask 上的位；
6. `ref_put` 旧的。

先 set 再装表，是为了避免「人已经跑在新 AS 上了，mask 里却还没有你」——这时别人发 shootdown 会漏掉你。先本地刷再 clear，是为了避免「mask 里已经没你了，本地 TLB 却还脏着」。

还有一处故意的滞后：下一线程若不是用户态，schedule **可以不**清 mask、也可以不换硬件根。位和 CPU 侧引用会留到之后某次用户→用户切换。teardown 必须等 mask 空，细节见所有权篇 / ASID 篇。

### 4.2 map 路径：新 vs 改

决策条件**不是**「是不是只有本 CPU」，而是 **「是不是第一次把这个 VA 装上」**：

- 首次 map（`new_map`）：内核 VA 做本地 kernel invalidate；用户 VA 做本地 `invalidate_page(asid, v)`。假定别人还没缓存过这个翻译。
- remap / unmap：内核走 `*_kernel_page_all_core`；用户走 `*_page_all_core(asid, v, &mask)`。改过、拆过的翻译，可能跑过的核都得动到。

### 4.3 硬件怎么刷：x86 与 aarch64

#### x86_64（Intel SDM：TLB 管理 / Multiprocessor）

手册里，改完页表之后要用合适的方式让 TLB 丢掉旧项。本实现常用的本地手段是 **`invlpg`**（按线性地址失效一页）。没有 PCID 时，换 **CR3** 会把当前核上与该地址空间相关的用户 TLB 一并冲掉——`arch_tlb_invalidate_all` 一类路径就是靠重载 CR3 这类办法。

跨核没有「一条总线广播让所有核 `invlpg`」的通用指令可用，所以要靠 **IPI**：本仓库在 `arch_smp_tlb_flush.c` 里，每个 CPU 一条 flush 消息（generation / busy）；发送方先在本地刷，再按 mask 给其他 online 核发软 IPI，等到 `done_gen`。APIC 类型还没就绪时，可能只做本地。文件头注释写明：**no PCID**。

内核侧的 `arch_tlb_invalidate_kernel_page_all_core` 在源码注释里倾向 **只本地 `invlpg`**（按「内核映射 percpu / 不共享脏翻译」来叙事）——读实现，不要想当然当成全机广播。

#### aarch64（ARM ARM：TLBI / 屏障）

AArch64 用 **`TLBI`** 系列指令做 TLB 维护，参数里常带 ASID 与页号。本仓库 inline 里能直接对上的有：

| 本仓库 API（概念） | 指令侧 | 含义（点到为止） |
|--------------------|--------|------------------|
| 本地按页（带 ASID） | `tlbi vae1` | EL1 当前 VMID，按 VA+ASID |
| 跨核按页 | `tlbi vae1is` | 同上，带 **Inner Shareable** 广播 |
| 本地按 ASID 整空间 | `tlbi aside1` | 按 ASID 清 |
| 跨核按 ASID | `tlbi aside1is` | 广播版 |
| 内核页（常不看 ASID） | `tlbi vaale1` / `vaale1is` | 任意 ASID 的该 VA |
| 全清 | `tlbi vmalle1` | 当前 EL1 翻译 |

前后用 `dsb(ISHST)` / `dsb(ISH)` / `isb` 做同步，这是 ARM 要求的 TLBI 上下文同步套路，不是随便加点装饰。`*_all_core` 路径上 **mask 参数常被 `(void)cpu_mask` 丢掉**——硬件广播已经覆盖 shareable domain 里的观察者；软件 mask 仍然用来卡住 `del_vspace` / `vspace_clear_user_mappings(allow_self_use=false)`，避免「还有人可能揣着旧翻译就拆 AS」。

范围失效若要用 `rvae1` 一类，需要更新的 TLBI 扩展；本实现的 range 路径目前仍可能退化为逐页 `vae1`（头文件注释写了）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `tlb_cpu_mask.h` / `VSpace` 字段 | mask 类型 |
| `task_manager.c` 里的 `schedule` | set / clear + 本地刷 |
| `map_handler.c` | new vs all-core 的调用点 |
| `arch_smp_tlb_flush.c` | x86 IPI shootdown |
| `arch/*/sync/tlb.h` | 本地 / IS API |

---

## 6. 流程

§4.1–4.3 已经是主路径。补充一点启动：BSP 在 `arch_start_core` 里初始化软 IPI；x86 还要 `arch_smp_flush_tlb_init`（每核消息槽，并全局注册一次 handler）。AP 同样走 `arch_start_core`。aarch64 **没有**对等的「TLB-IPI init」——它靠 `tlbi *is`，不靠这条 IPI 通道。

---

## 7. 公开 API

```c
/* 概念：vs->tlb_cpu_mask；具体名字见各 arch 的 tlb.h */
void arch_tlb_invalidate_page(u64 asid, vaddr v);
void arch_tlb_invalidate_page_all_core(u64 asid, vaddr v,
                                       const vs_tlb_cpu_bitmap_t *mask);
/* kernel_page / vspace_page / range / all 等同族 */
```

以各 arch 头文件为准。x86 侧许多函数会 `(void)asid`——没有 PCID 时硬件不用这个参数。

---

## 8. 多架构

见 §4.3。riscv / loongarch 未纳入 v0.1 的 TLB 主线。

---

## 9. 测试

没有单独的 TLB 单测；靠 SMP 加上用户 map / unmap / 切换间接覆盖。本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- cache 头文件为空。
- x86 内核 `*_all_core` 是否真广播：以源码为准。
- 与 `32-TLB_shootdown` 篇的分工：本篇讲策略、mask 与硬件对照；那篇写 IPI 握手细节。
- x86 启用 PCID 后，本地 / 远程失效语义会变——属远期，见 ASID 篇。

---

## 11. 变更记录

- 2026-09-25：补 SDM / ARM ARM 硬件对照（`invlpg`、IPI shootdown、`tlbi`/`*is`、屏障）；语言整理；去掉「待加强」备忘。
- 2026-08-29：整篇重做——纠正「仅本 CPU」决策叙事；new/remap；x86 IPI vs aarch64 IS；cache 空头诚实说明。
- 2026-08-26：v0.1 初稿。
