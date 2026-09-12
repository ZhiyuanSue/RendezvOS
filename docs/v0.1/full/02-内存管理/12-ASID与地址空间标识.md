# ASID 与地址空间标识

v0.1 · 2026-08-29

本篇覆盖：`kernel/mm/asid.c`、`include/rendezvos/mm/asid.h`、`include/arch/x86_64/mm/asid.h`、`include/arch/aarch64/mm/asid.h`（及 aarch64 boot 里与 TCR.AS 相关的探针）。

TLB mask / shootdown 见 `11-TLB与缓存一致性.md`；调度装根见 `03-任务与调度/15-VSpace所有权与调度切换.md`；`create_vspace` 见页表篇。

---

## 1. 概述

**ASID** 是软件侧给用户地址空间贴的编号，好让切换时不必总把用户 TLB 一锅端。

软件号 ≠ 硬件一定认。

- **aarch64：** 装用户根时把 ASID 打进 **TTBR0**；`tlbi` 也可按 ASID 靶向。boot 若探测到 16-bit ASID，可开 `TCR_EL1.AS`。  
- **x86：** `vs->asid` 仍分配，但装根只写 **CR3**，invalidate **忽略** asid 参数；**PCID 宏在但未用**。号码主要是 API 对称 + 书本字段。

没有 **generation/epoch**：同一数字用完回收后，全靠「mask 清空 + 本地已刷」保证没有人还揣着旧翻译。纪律破了就会静默错译——这是刻意不做 generation 的代价。

---

## 2. 目标与边界

**提供：** 全局 bitmap 分配/释放；`asid_init`；与 create/del/schedule 的耦合说明。

**不做：** x86 PCID 启用；ASID 世代；riscv/loongarch 主线。

---

## 3. 分层与调用方

| 动作 | 谁 |
|------|-----|
| `asid_init` | **仅 BSP** `virt_mm_init`（在 `init_root_vspace` 前） |
| `asid_alloc` | `alloc_user_vs_structure`（create/clone 经此） |
| 装 TTBR/CR3 + asid | `schedule` |
| `asid_free` | `del_vspace` 等拆光之后 |

`root_vspace.asid = 0`（保留，非用户）。clone **不会**继承父 ASID，各自新号。

---

## 4. 数据结构与不变量

- 全局位图，容量与 `arch_asid_supports_16bit()` 有关：真则扫到 `U16_MAX`，否则 `U8_MAX`。  
- **0 永远保留**；分配失败返回 0 → create 失败路径释放结构体。  
- MCS：`asid_lock` + 本核 `asid_mcs_node`。  
- x86 的 `arch_asid_supports_16bit()` 恒真 → 分配器以为有大空间，但 **HW 不用**——别写成「x86 已启用 PCID」。

**回收安全链（无 generation）：**

1. schedule 只在本地刷完旧 AS 后才 clear mask 位；  
2. 仍有 mask 位时，unmap/shootdown / clear 仍罩得住；  
3. `vspace_clear_user_mappings(allow_self_use=false)` / `del_vspace` 要求 mask 空才放行；  
4. 用户→内核可暂时留着 mask+引用 → ASID 还活着，直到之后用户→用户清掉。

in-place exec（`allow_self_use=true`）可保留本 CPU 位，**ASID 不换**。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `asid.c` / `asid.h` | bitmap alloc/free/init |
| `arch/*/mm/asid.h` | `arch_asid_supports_16bit` |
| `vmm.c` | create/del 生命周期 |
| `task_manager.c` | 切换时装根 |
| aarch64 `boot.S` `init_mmu` | TCR.AS 探针 |

---

## 6. 流程

```text
asid_init (BSP)
  → create_vspace / clone → asid_alloc → vs->asid
  → schedule：set mask → 写 TTBR0|ASID 或 CR3 → 刷旧 → clear 旧 mask
  → del_vspace：mask==0 → 清用户映射 → asid_free
```

---

## 7. 公开 API

```c
void asid_init(void);
u16 asid_alloc(void);   /* 0 = 失败 */
void asid_free(u16 asid);
bool arch_asid_supports_16bit(void);
```

---

## 8. 多架构

见 §1。riscv/loongarch 非 v0.1 主线。

---

## 9. 测试

无单独 ASID 测例；随用户 AS 创建/切换/销毁覆盖。本篇未复测。

---

## 10. 限制与后续

- 无 generation：依赖 TLB 篇纪律。  
- 耗尽则无法 create。  
- x86 PCID 属远期，勿在正文写成已支持。

---

## 11. 变更记录

- 2026-08-29：整篇重做——软件号 vs 硬件标签；无 generation 代价；alloc 在 `alloc_user_vs_structure`；x86 大空间但不启用 PCID；与 mask/teardown 链。
- 2026-08-26：v0.1 初稿。
