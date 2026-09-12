# TLB shootdown 与跨核一致性

v0.1 · 2026-08-29

本篇覆盖：`arch/x86_64/mm/arch_smp_tlb_flush.c`、x86/aarch64 `include/arch/*/sync/tlb.h` 中 all_core 路径与软 IPI 的衔接。

**与 `02-内存管理/11-TLB与缓存一致性.md` 分工（写死）：**

| | 02（策略 / mask） | **本篇（06）** |
|--|------------------|----------------|
| mask 含义、schedule set→装根→刷旧→clear | 主责 | 一句引用 |
| new_map 本地 vs remap/unmap all_core | 主责 | 引用 |
| x86：按 mask 软 IPI + invlpg | 策略层 | **gen/busy/done 握手** |
| aarch64：`tlbi *is`，**常忽略 mask** | 已写 | **禁止再写「via IPI」** |
| 门铃 / pending | → 软 IPI 篇 | 只写 TLB 消息槽如何用 IPI |

软 IPI：唯一 in-tree 默认 registrant 是 **x86 TLB**。VSpace teardown 门闩见 VSpace 所有权篇。

---

## 1. 概述

改了用户 PTE 之后，凡 **可能缓存该 AS 翻译** 的 CPU 必须 invalidate。`tlb_cpu_mask` 回答「谁可能脏 / 能不能拆」；**怎么刷是 arch 的事**。

x86 敲门刷；aarch64 广播刷。

| | x86 | aarch64 |
|--|-----|---------|
| all_core | 本地先刷 → 按 mask 发软 IPI → 等 done | `tlbi …is` + DSB/ISB；**mask 参数常 `(void)`** |
| init | `smp_ipi_init` + **`arch_smp_flush_tlb_init`** | 仅 `smp_ipi_init`（**无** TLB-IPI init） |
| mask 仍用于 | 选谁收 IPI + teardown | **仅** teardown / clear 门闩 |

没有可靠 mask + 刷干净，页表篇「按需 unmap 后应 fault」在多核上兑不了现。

---

## 2. 目标与边界

**提供：** x86 shootdown 握手细节；与 soft IPI / mask 的衔接；aarch64「不走 IPI」的边界句。

**不做：** mask 维护全文（02）；门铃位图协议全文（软 IPI）；cache API（02 已声明空）；batching 框架。

---

## 3. 分层与调用方

**MM** — remap/unmap 后 `arch_tlb_invalidate_*_all_core(..., &vs->tlb_cpu_mask)`。  
**schedule** — 本地刷旧 AS + set/clear mask（02 / VSpace）。  
**规则：** 改 PTE 持适当锁；IPI handler **只刷**，不锁、不 sleep。

---

## 4. 数据结构与不变量

### 4.1 x86 每核消息槽

```c
struct smp_tlb_flush_msg {
        atomic64_t request_gen;
        atomic64_t done_gen;
        atomic64_t busy;
        vaddr flush_va;
        bool flush_all;
};
```

- 单槽 + `busy`：同目标 CPU 上并发 shootdown **串行化**；无 batching。  
- **无 PCID**：页级=`invlpg`；vspace 级≈ reload CR3 / 全刷；asid 参数 `(void)`。

### 4.2 mask 与并发

unmap 读 mask 发 IPI 时 **不**再拿 `tlb_cpu_mask_lock`——正确性靠 02 的「先 set 再进 AS / 先刷再 clear」。teardown 仍在 CAS 下查 mask。

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
       失败 → 清 busy，返回（不保证远端刷到）
     自旋直到 done_gen >= expect
4. Handler：读 request_gen → 刷 → done_gen=cur → busy=0
```

Init：`arch_start_core` → `smp_ipi_init` + **`arch_smp_flush_tlb_init`**（每核槽；**全局一次** `smp_ipi_register`）。

### 6.2 aarch64

调用 `*_all_core` 即 IS 广播；无消息槽、无等 done。mask 仍卡 `vspace_clear(..., false)` / `del_vspace`。

### 6.3 内核 `*_kernel_page_all_core`（x86）

倾向 **本地 invlpg**（percpu 内核映射叙事）——勿暗示按 mask 广播。以源码为准（02 已诚实）。

---

## 7. 公开 API

```c
void arch_tlb_invalidate_page_all_core(u16 asid, vaddr v,
                                       vs_tlb_cpu_bitmap_t *mask);
/* page / kernel_page / vspace_page / range / all 等同族 — 见 arch tlb.h */

/* x86 */
void arch_smp_flush_tlb_init(void);
void arch_smp_flush_page_tlb(...);
void arch_smp_flush_all_tlb(...);
```

---

## 8. 多架构

上表。riscv/loongarch 非主线。

---

## 9. 测试

间接：SMP + 用户 map/unmap。本篇未复测。

---

## 10. 限制与后续

- x86 send 失败无重试。  
- 单槽串行；无 batching。  
- aarch64 广播成本与 mask 解耦。  
- 无 PCID。

---

## 11. 变更记录

- 2026-08-29：整篇重做——删「aarch64 via IPI」；x86 gen/busy/done；与 02/软 IPI 边界；send 失败与单槽限制。
- 2026-08-27：初稿（误写 aarch64 IPI）。
