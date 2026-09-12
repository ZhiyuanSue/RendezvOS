# 物理内存与Buddy分配器

v0.1 · 2026-08-29

本篇覆盖：`kernel/mm/pmm.c`、`kernel/mm/buddy_pmm.c`、`include/rendezvos/mm/pmm.h`、`include/rendezvos/mm/buddy_pmm.h`、`arch/x86_64/mm/pmm.c`、`arch/aarch64/mm/pmm.c` 及对应 `include/arch/*/mm/pmm.h`。

调用时机见 `01-启动与初始化/02-启动流程总览.md`（`phy_mm_init` 在 `virt_mm_init` 之前）。页表 / Map_Handler 见同分区虚拟地址篇；内核堆见 `09-kmalloc与内核堆.md`。

---

## 1. 概述

物理内存子系统在 boot 早期把固件给出的可用区收成 `m_regions`，抠出内核、percpu、PMM 元数据占用，再按 **zone** 切成若干 `MemSection`，每个 zone 挂一个 `struct pmm` 实现（默认 **buddy**）。之后所有「要物理连续页」的路径（页表大页、kmalloc 整页等）都从选定 zone 的 `pmm->pmm_alloc` 来。

**设计意图：**

- **Buddy 而不是单纯 bump** — 需要可释放、可合并的 2ⁿ 连续块；最大 order **10** → 单次最大 **2 MiB**（与大页/注释中的 Linux 习惯对齐）。
- **`Page` 与 `buddy_page` 并行** — 前者管 ref / section / rmap；后者管 free 链与 order。分配先抬 `Page` ref 再改 buddy，失败可回滚，避免「半分配」。
- **Zone 可扩展但默认单 NORMAL** — weak `configure_pmm_zones_hook`；非法配置**回退**到单 `ZONE_NORMAL`+buddy（不是硬失败）。多 zone 的 manage 元数据打包在源码里仍是骨架（各 zone 可能共用同一 manage 窗口）——文档如实写，不假装 NUMA 已完工（E1）。
- **Reclaim 钩子预留** — OOM 时放锁调 hook；树内**尚无注册者**。

---

## 2. 目标与边界

**提供：** 区域表、reserve、zone 切分、buddy alloc/free、可选 reclaim、arch 的 memmap/RSDP 或 DTB memory 入口。

**不做：** 完整 NUMA/多 zone 产品策略；页面换出；自动跨 zone 借页（调用方必须选对 `zone->pmm`）。

**失败语义（须记清）：**

| 情况 | 典型返回 |
|------|----------|
| 请求页数 `> 2^MAXORDER` | `-E_RENDEZVOS`（不进 reclaim） |
| 无空闲合适 order，且无 hook / 重试耗尽 | `-E_REND_RETRY` |
| hook 返回 false | `-E_REND_NO_MEM` |
| `page_number==0` | `0`（成功空操作） |
| 内部 cursor / 双重 free 等 | `-E_RENDEZVOS` |

---

## 3. 分层与调用方

- **arch `*_init_pmm` / `arch_get_memory_regions`** — 填 `m_regions`；x86 顺带 RSDP 保留；aarch64 打印 mem_rsvmap **但不 reserve**。
- **portable `phy_mm_init`** — percpu 保留、PMM blob 布局、zone 配置、buddy init。
- **调用方** — 持 `struct pmm *`（通常 `mem_zones[ZONE_NORMAL].pmm`）调方法；勿假设「NORMAL 失败自动试其他 zone」。

---

## 4. 数据结构与不变量

### 4.1 区域与 zone

- `m_regions`：最多 128 槽；reserve 用切分/掏空，删除置零槽（不紧凑）。
- `ZONE_NR_MAX=16`；枚举目前实质使用 `ZONE_NORMAL`。每个 zone：`lower/upper`、`struct pmm *`、section 链。
- **不变量：** 内核 + percpu 物理范围须落在**同一 1 GiB** 窗口，否则 `phy_mm_init` 失败（boot L2 映射约束）。

### 4.2 PMM blob 布局（物理）

```
[pmm_start …)     扩展 boot L2 表页
[…)               各 zone 的 MemSection + Page[]
[…)               buddy_page[] 管理元数据（多 zone 时偏移累加未完成）
```

其后把 `[pmm_end, ROUND_UP(..., 2MiB))` 再 reserve，避免 buddy 把仍映在 2 MiB boot 映射里的尾页分出去。

### 4.3 Buddy

- `MAXORDER` 对应最大 2¹⁰ 页 = 2 MiB。
- 每 zone 一把 MCS：`lock_mcs(..., &percpu(pmm_spin_lock[zone_id]))`——`me` 必须是本核 slot。
- free：仅当 `Page.ref_count` 降到 0 才回链；合并要求 buddy 索引合法且同 section（跨洞不合并）。
- `invalid_ppn(ppn)`：`ppn <= 0` 视为非法（**PPN 0 不可用**）。注意 `phy_Page_ppn` 一类命名可能返回的是 paddr，调用时核对头文件。

### 4.4 Reclaim

```c
typedef bool (*pmm_reclaim_fn_t)(struct pmm *pmm, u64 need_pages, int attempts);
```

调用时 **zone 锁已释放**；hook 可对本 `pmm` `pmm_free`，**禁止 `pmm_alloc`**（会死锁/重入）。最多约 64 次尝试。树内无 `pmm_set_reclaim_hook` 调用方。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/mm/pmm.c` | `phy_mm_init`、regions、zone 切分、Page 生成、弱 hook |
| `kernel/mm/buddy_pmm.c` | buddy 算法、reclaim 循环 |
| `arch/x86_64/mm/pmm.c` | Multiboot memmap、ACPI 保留、boot 映射辅助 |
| `arch/aarch64/mm/pmm.c` | DTB `/memory`、以 `map_end` 为内核后游标 |
| `pmm.h` / `buddy_pmm.h` | `struct pmm`、`Page`、`buddy_page`、API |

riscv/loongarch 的 `pmm.h` 为空桩，无实现。

---

## 6. 流程

### 6.1 `phy_mm_init`（精确骨架）

1. `arch_init_pmm` → `m_regions` + `per_cpu_phy_start`  
2. `reserve_per_cpu_region`；整段 `[kernel, percpu_end)` reserve  
3. 校验同 1 GiB；`arch_map_percpu_data_space` + clean  
4. 算可用窗口；`pmm_configure_zones`（非法 → 默认 NORMAL）  
5. `split_pmm_zones`；估算 Page/section/manage/L2 页数  
6. `reserve_region_with_length` 拿 PMM blob；2 MiB 对齐 pad  
7. `arch_map_pmm_data_space`；`generate_zone_data`；每 zone `pmm_init`

### 6.2 alloc / free

- alloc：持锁找 ≥order 块 → 分裂 → 抬 `Page` ref → 填 ppn/页数（**向上取整到 2ⁿ**）。  
- free：降 ref；为 0 则合并回链。  
- 空闲总量够但无连续 2ⁿ → 走 reclaim（若已注册）。

### 6.3 与上层咬合

`virt_mm_init` / Map_Handler / kmalloc 依赖本阶段已可分配物理页；平台 ACPI/PCI 映射也用 NORMAL pmm。因果链：本篇 → 虚拟地址篇 → kmalloc 篇。

---

## 7. 公开 API

```c
error_t phy_mm_init(struct setup_info *);

/* 方法在 struct pmm 上；默认实现为 buddy_pmm */
error_t pmm_alloc(struct pmm *, u64 page_number, u64 *alloced, paddr *out);
error_t pmm_free(struct pmm *, paddr p, u64 page_number);
void pmm_set_reclaim_hook(struct pmm *, pmm_reclaim_fn_t);

error_t memory_regions_reserve_region(paddr start, paddr end);
/* arch */
error_t arch_init_pmm(struct setup_info *, paddr *per_cpu_phy_start);
```

zone 表：`mem_zones[]` / `nr_mem_zones`（以头文件为准）。

---

## 8. 多架构

| | x86_64 | aarch64 |
|--|--------|---------|
| 来源 | Multiboot mmap（跳过 ≤1 MiB） | DTB `/memory` |
| 额外保留 | RSDP/RSDT 窗口（rev0） | rsvmap 仅打印 |
| 内核后游标 | `PHY(_end)` | `PHY(map_end_virt_addr)` |

算法本体与 ISA 无关。

---

## 9. 测试

`modules/test/` 下 pmm/single 相关测例；`make ARCH=x86_64 config && make all && make run`（需 `RENDEZVOS_TEST`）。注意旧测例若假定 OOM 一定是 `-E_RENDEZVOS`，与现今 `-E_REND_RETRY` 路径可能不一致——以源码与测例更新为准。本篇未复测。

---

## 10. 限制与后续

- 多 zone manage 偏移未累加；NUMA 真源见 **E1**。  
- reclaim 未接线。  
- `reserve_region_with_length` 对区域长度更新存疑（以代码审查为准，可疑处可记 TODO）。  
- aarch64 firmware reserved 未从 buddy 挖掉。  
- `pmm_show_info` 打印桶可能不含最大 order。

---

## 11. 变更记录

- 2026-08-29：整篇重做——完整 init/PMM 布局；纠正「非法 zone 无回退」；errno 矩阵与 reclaim 契约；多 zone 骨架诚实说明；arch 差异与已知坑。
- 2026-08-26：v0.1 初稿。
