# TLB 与缓存一致性

v0.1 · 2026-08-29

本篇覆盖：`include/rendezvos/mm/tlb_cpu_mask.h`、`include/arch/x86_64/sync/tlb.h`、`include/arch/aarch64/sync/tlb.h`、`arch/x86_64/mm/arch_smp_tlb_flush.c`；与 schedule / `map_handler` 的交界。软 IPI 协议细节见 `06-SMP与同步/软IPI机制.md` 与 `TLB_shootdown与跨核一致性.md`。

**缓存：** aarch64 `sync/cache.h` 目前为空，树内无对称的 x86 `cache.h`。本篇**不假装**有通用 cache 维护 API；设备/DMA 路径以后另写。标题里的「缓存」仅表示与 TLB 同属「CPU 侧翻译/可见性」话题。

何时在 map/unmap 调哪类 invalidate：页表篇 §6.2 已钉；本篇讲 **mask 含义、跨核怎么送、和调度怎么咬**。

---

## 1. 概述

改了 PTE 或拆了用户映射之后，别的核上的 TLB 可能还记着旧翻译。若不知道「谁跑过这个地址空间」，要么不敢拆 AS，要么只能全机狂刷。

core 的答案分两层：

1. **`VSpace::tlb_cpu_mask`** — 哪些 CPU **可能**还缓存着这个用户 AS 的翻译。  
2. **arch invalidate API** — 本地刷，或跨核；x86 用 **按 mask 的软 IPI + invlpg**，aarch64 用户/跨核路径多用 **`tlbi *is` 广播**（**常忽略 mask**，mask 仍用于 teardown 门闩）。

mask 回答「该通知谁 / 能不能拆」；具体怎么刷是 arch 的事。二者缺一，要么漏刷，要么 ASID 不敢回收。

---

## 2. 目标与边界

**提供：** mask 类型与调度维护约定；与 `map_handler` / schedule 对齐的 invalidate 策略说明；x86 IPI shootdown 入口。

**不做：** 通用 dcache/icache API（头文件空）；把 06 篇的 IPI 握手再抄一遍。

---

## 3. 分层与调用方

| 职责 | 落点 |
|------|------|
| 首次 map vs 改映射调哪类 TLBI | `map_handler.c`（页表篇） |
| 切线程时 set/clear mask + 本地刷旧 AS | `schedule`（VSpace 所有权篇） |
| x86 远程 invlpg | `arch_smp_tlb_flush.c` + soft IPI |
| aarch64 IS 广播 | `arch/.../sync/tlb.h` inline |

---

## 4. 数据结构与不变量

### 4.1 `tlb_cpu_mask`

位图宽度 `RENDEZVOS_MAX_CPU_NUMBER`。在 `tlb_cpu_mask_lock`（CAS）下改。

**schedule（用户→用户，vs 变了）顺序（不可乱）：**

1. `ref_get` 新 vs  
2. **先 set** 本 CPU 在**新** mask 上的位  
3. 装 HW 根（+ ASID）  
4. 本地 invalidate **旧** ASID/空间  
5. **再 clear** 本 CPU 在**旧** mask 上的位  
6. `ref_put` 旧  

先 set 再装表：避免「已跑在新 AS 上但 mask 还没有你」→ 并发 shootdown 漏你。先本地刷再 clear：避免「mask 没你了但本地 TLB 还脏」。

**用户→内核的故意滞后：** 下一线程不是 USER 时，schedule **可以不**清 mask / 不换 HW。位和 CPU 侧引用留到之后某次用户→用户切换。teardown 必须等 mask 空（见所有权 / ASID 篇）。

### 4.2 map 路径：新 vs 改

| 事件 | 内核 VA | 用户 VA |
|------|---------|---------|
| 首次 map（`new_map`） | 本地 kernel invalidate | 本地 `invalidate_page(asid,v)` |
| remap / unmap | `*_kernel_page_all_core` | `*_page_all_core(asid,v,&mask)` |

「是不是只有本 CPU」**不是**决策条件；**是不是第一次装上这个 VA** 才是。首次假定别人还没缓存过；改/拆必须打可能跑过的核。

### 4.3 跨核：x86 vs aarch64

**x86：** 每 CPU 一条 flush 消息（gen/busy）；发送方本地先刷，再按 mask 对其他 online 位发 IPI，等 done。APIC 类型未就绪时可能只做本地。内核 `*_kernel_page_all_core` 注释倾向 **本地 invlpg**（percpu 内核映射叙事）——以源码为准。

**aarch64：** `*_all_core` → `tlbi …is` + DSB/ISB；**mask 参数常不用**。mask 仍卡住 `del_vspace` / `vspace_clear_user_mappings(allow_self_use=false)`。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `tlb_cpu_mask.h` / `VSpace` 字段 | mask 类型 |
| `task_manager.c` `schedule` | set/clear + 本地刷 |
| `map_handler.c` | new vs all-core 调用点 |
| `arch_smp_tlb_flush.c` | x86 IPI shootdown |
| `arch/*/sync/tlb.h` | 本地 / IS API |

---

## 6. 流程

见 §4.1–4.3。BSP 在 `arch_start_core` 里初始化软 IPI；x86 还要 `arch_smp_flush_tlb_init`（每核消息槽 + 全局注册一次 handler）。AP 同样走 `arch_start_core`。aarch64 **没有**对等的 TLB-IPI init。

---

## 7. 公开 API

```c
/* 概念：vs->tlb_cpu_mask；arch_tlb_invalidate_* 见 arch 头 */
void arch_tlb_invalidate_page(u16 asid, vaddr v);
void arch_tlb_invalidate_page_all_core(u16 asid, vaddr v, vs_tlb_cpu_bitmap_t *mask);
/* … kernel_page / vspace_page / range / all 等同族 */
```

以各 arch `tlb.h` 为准。

---

## 8. 多架构

上表。riscv/loongarch 未纳入 v0.1 TLB 主线。

---

## 9. 测试

无单独 TLB 单测；靠 SMP + 用户 map/unmap / 切换间接覆盖。本篇未复测。

---

## 10. 限制与后续

- cache 头文件空。  
- x86 内核 all-core 是否真广播：读实现，勿想当然。  
- 与 06 shootdown 篇分工：本篇策略与 mask；06 写 IPI 握手细节。

---

## 11. 变更记录

- 2026-08-29：整篇重做——纠正「仅本 CPU」决策叙事；对齐 new/remap；x86 IPI vs aarch64 IS；诚实 cache 空头；schedule 顺序不变量。
- 2026-08-26：v0.1 初稿。
