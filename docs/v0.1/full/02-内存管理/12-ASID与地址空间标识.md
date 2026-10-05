# ASID 与地址空间标识

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/asid.c`、`include/rendezvos/mm/asid.h`、`include/arch/x86_64/mm/asid.h`、`include/arch/aarch64/mm/asid.h`，以及 aarch64 boot 里与 `TCR_EL1.AS` 相关的探针（见平台启动篇）。

TLB mask / shootdown 见 `11-TLB与缓存一致性.md`；调度时装根见 `03-任务与调度/15-VSpace所有权与调度切换.md`；`create_vspace` 见页表篇。

---

## 1. 概述

**ASID**（Address Space ID）在这里首先是软件给用户地址空间分配的编号：切换时若硬件识别这个标签，就不必把用户 TLB 整段刷掉。

但软件号 **不等于** 硬件一定在用：

- **aarch64：** 装用户根时，把 ASID 写入 **TTBR0_EL1** 的高位；`tlbi` 也可以按 ASID 靶向失效。boot 若从 `ID_AA64MMFR0_EL1` 读到 16-bit ASID，还会置位 `TCR_EL1.AS`（平台启动篇 §4.6 提过）。
- **x86：** `vs->asid` 按 **12-bit PCID** 上界分配，但 `arch_set_current_user_vspace_root_asid` **丢弃** asid 参数，只写 **CR3**；invalidate 路径里也对 asid 做 `(void)asid`。头文件里虽有 `CR3_PCID` / `CR4_PCIDE` 一类宏，**当前未启用 PCID**。号码是为了 API 对称，并为将来启用 PCID 预留同一取值范围。

没有 **generation / epoch**：同一个数字回收后再分配给别人，完全靠「mask 已经清空、且相关核本地已经刷过」保证没有人还持有旧翻译。这条纪律一旦被破坏，就会静默错译——这是刻意不做 generation 的代价。

---

## 2. 目标与边界

提供：全局 bitmap 分配 / 释放；`asid_init`；与 create / del / schedule 的耦合；硬件侧 TTBR /（未用的）PCID 对照。

不做：x86 PCID 启用；ASID 世代号；riscv / loongarch 主线。

---

## 3. 分层与调用方

- `asid_init`：**仅 BSP** 在 `virt_mm_init` 里、`init_root_vspace` 之前调用。
- `asid_alloc`：经过 `alloc_user_vs_structure`（`create_vspace` / `clone_vspace` 都经过此）。
- 装 TTBR0|ASID 或 CR3：在 `schedule` 里，配合 TLB 篇的 mask 顺序。
- `asid_free`：在 `del_vspace` 等把用户映射拆除干净、mask 已空之后。

`root_vspace.asid = 0`（保留号，不是普通用户 AS）。clone **不会**继承父 ASID，父子地址空间各自分配新号。

---

## 4. 数据结构与不变量

### 4.1 软件分配器

全局位图，上界由 `arch_asid_supported_width()` 决定：`asid_max = (1u << width) - 1`（aarch64 为 8→255 或 16→65535；x86 为 12→4095，对齐 PCID 字段宽）。**0 永远保留**；分配失败返回 0，create 路径会释放已建结构体。锁是 MCS：`asid_lock` + 本核 `asid_mcs_node`。

注意 x86 返回 **12** 只表示 PCID 标识位宽；**当前并未启用 PCID**（装 CR3 仍忽略 asid）——不要写成「x86 已经启用 PCID」。

分配器没有单独的公开结构体，状态在 `asid.c` 里：

```c
#define ASID_BITMAP_BITS ((u32)U16_MAX + 1u)
BITMAP_DEFINE_TYPE(asid_bitmap_t, ASID_BITMAP_BITS)

static spin_lock asid_lock;
DEFINE_PER_CPU(spin_lock_t, asid_mcs_node);
static asid_t asid_max;
static asid_bitmap_t asid_bitmap;
```

### 4.2 没有 generation 时，怎样保证旧号已经没人再用

本仓库不为 ASID 做世代号：同一个数字回收后再分配给别人，全靠下面这条链保证没有核还持有旧翻译。

1. `schedule` 只在本核刷完旧地址空间之后，才清除本核在旧 `tlb_cpu_mask` 上的位；
2. mask 里还有位时，unmap / 跨核 invalidate / 清用户映射仍然会覆盖那些核；
3. `vspace_clear_user_mappings(allow_self_use=false)` / `del_vspace` 要求 mask 已经为空才放行；
4. 用户线程切到内核线程时，可以暂时保留 mask 和 CPU 侧引用——ASID 仍然有效，直到之后某次用户切到另一个用户地址空间再清除。

原地 exec（`allow_self_use=true`）可以保留本 CPU 这一位，**ASID 不更换**。

软件号怎样写入 TTBR0（aarch64）或为何 x86_64 目前不写 PCID，见 §6.1。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `asid.c` / `asid.h` | bitmap alloc / free / init |
| `arch/*/mm/asid.h` | `arch_asid_supported_width` |
| `arch/*/mm/vmm.h` | `arch_set_current_user_vspace_root_asid` |
| `vmm.c` | create / del 生命周期 |
| `task_manager.c` | 切换时装根 |
| aarch64 `boot.S` `init_mmu` | `TCR.AS` 探针 |

---

## 6. 流程

### 6.1 背景：硬件怎样携带这个软件号（官方对照）

**aarch64（ARM ARM，TTBR / TCR / TLBI）**

- Stage 1 EL1&0 下，用户低半根在 **TTBR0_EL1**。实现里：

  ```c
  ttbr0 = u_root | ((u64)asid << 48);
  msr TTBR0_EL1, ttbr0;
  ```

  也就是把软件 ASID 写入 TTBR0 的高 16 位字段（与常见 AArch64 ASID 打包方式一致；具体位宽还受 `TCR_EL1.AS` 约束）。
- `TCR_EL1.AS`：`ID_AA64MMFR0_EL1.ASIDBits` 为 16-bit 时，`boot.S` 的 `init_mmu` **会**置位；否则按 8-bit。`arch_asid_supported_width()` 读同一字段，把 8/16 告诉分配器。
- 页表叶上的 **nG（非全局）** 位：非全局项才会随 ASID 生效；全局项换 ASID 也不一定清除。这与页表篇里 `PAGE_ENTRY_GLOBAL` → 清/置 `nG` 的编解码对应。
- 失效：`tlbi vae1` / `aside1` 等把 ASID 编进操作数（TLB 篇 §6.1）。

**x86_64（Intel SDM，PCID / CR3）**

- 启用 **PCID** 时，CR3 低 **12** 位可携带 process-context identifier，换地址空间时可以选择不刷光全部 TLB。本实现：**不写 PCID，不置位 `CR4.PCIDE`**；`arch_set_current_user_vspace_root_asid` 直接忽略 asid，只把根物理地址写入 CR3。
- 软件仍按 12-bit 上界分配 `vs->asid`，为将来启用 PCID 预留同一取值范围；跨核仍依赖 mask + IPI `invlpg` / 全清（TLB 篇）。

### 6.2 生命周期时间线

```text
asid_init (BSP)
  → create_vspace / clone → asid_alloc → vs->asid
  → schedule：set mask → 写 TTBR0|ASID 或 CR3 → 刷旧 → clear 旧 mask
  → del_vspace：mask==0 → 清用户映射 → asid_free
```

---

## 7. 公开 API

本篇涉及的接口分布在：`include/rendezvos/mm/asid.h`（实现 `kernel/mm/asid.c`）与
`include/arch/<isa>/mm/asid.h` 的 `arch_asid_supported_width`。装根时如何把
ASID 写入硬件，见各 ISA 的 `arch/*/mm/vmm.h` 的
`arch_set_current_user_vspace_root_asid`（调用方在 schedule / 15 篇）。
`asid_t` = `u16`（`common/types.h`）。

### 7.1 编排顺序（与源码一致）

| 阶段 | 顺序 |
|------|------|
| BSP | `virt_mm_init` → **`asid_init()`** → `init_root_vspace`（`root_vspace.asid = 0`） |
| 用户 AS 创建 | `alloc_user_vs_structure` / create·clone → **`asid_alloc()`** → 写入 `vs->asid`；返回 **0** 则失败回滚 |
| 调度切入 | `vs_tlb_cpu_mask_set` → **`arch_set_current_user_vspace_root_asid(root, vs->asid)`** → …（见 11 / 15） |
| 销毁 | `tlb_cpu_mask` 已空 → 清用户映射 → **`asid_free(vs->asid)`**（在 `del_vspace` 路径） |

clone **不**继承父 ASID，各自 `asid_alloc`。无 generation：回收后再分配全靠 mask/刷序纪律。

### 7.2 分配器（`asid.h`）

```c
void asid_init(void);
asid_t asid_alloc(void);     /* 成功 ∈ [1, asid_max]；失败 0 */
void asid_free(asid_t asid); /* asid==0 或 >max：no-op；不刷 TLB */
asid_t asid_get_max(void);   /* init 后 = (1u << width) - 1 */
```

| 接口 | 说明 |
|------|------|
| `asid_init` | 清 bitmap，保留 0；`asid_max = (1u << arch_asid_supported_width()) - 1`。仅 BSP。 |
| `asid_alloc` | MCS（`asid_lock` + 本核 `asid_mcs_node`）；线性扫描空位。耗尽返回 0。 |
| `asid_free` | 清对应 bit；**不**做 TLBI。 |
| `asid_get_max` | 当前分配上界。 |

### 7.3 架构探针与装根

```c
u32 arch_asid_supported_width(void);  /* arch/*/mm/asid.h */

void arch_set_current_user_vspace_root_asid(paddr u_root, asid_t asid);
void arch_set_current_user_vspace_root(paddr u_root); /* 兼容；无 ASID 标签 */
```

| 接口 | aarch64 | x86_64 |
|------|---------|--------|
| `arch_asid_supported_width` | 读 `ID_AA64MMFR0_EL1` ASIDBITS → **8 或 16** | **12**（PCID 字段宽；**不**表示已启用 PCID） |
| `…_root_asid` | `TTBR0 = u_root \| (asid<<48)` + ISB | **忽略 asid**，只写 CR3 |
| `…_root` | 只写 TTBR0 低半根 | 写 CR3 |

`TCR_EL1.AS` 在 boot 探测后可能置位（平台启动篇）；本篇不重复寄存器步骤。

---

## 8. 多架构

见 §1 与 §6.1。riscv / loongarch 不是 v0.1 主线。

---

## 9. 测试

没有单独的 ASID 测试用例；随用户 AS 的创建 / 切换 / 销毁间接覆盖。

---

## 10. 限制与后续

- 无 generation：完全依赖 TLB 篇的 mask / 刷序纪律。
- 号码耗尽则无法 create。
- x86 PCID 属于远期；正文不要写成已支持。

---

## 11. 变更记录

- 2026-10-04：中文用语整理——分配给别人/持有/清除/覆盖/保留/有效/不更换、写入/置位/随 ASID 生效/依赖、本篇涉及的接口分布在、扫描空位、已启用、测试用例、属于远期；§1 概述补「分配的编号/识别/刷掉/写入/丢弃/分配给别人/持有/一旦被破坏」。
- 2026-10-04：§4.1 补 `asid.c` 里的 bitmap / 锁。
- 2026-10-04：§4 只留分配器与「没有 generation 时怎样保证旧号没人再用」；硬件 TTBR/PCID 对照迁入 §6.1。
- 2026-10-01：`arch_asid_supports_16bit` → `arch_asid_supported_width`（返回位宽）；x86 按 PCID 报 12；分配器用 `(1<<width)-1`。
- 2026-09-27：中文用语整理（一锅端/揣着→整段冲掉/带着；变更记录 teardown→拆除收尾）。
- 2026-09-26：§7 全文审阅——`asid.h` / arch 探针 / 装根头文件注释；写清 init→alloc→schedule→free 编排与 x86「忽略 asid」事实。
- 2026-09-25：补 TTBR0 ASID 打包、`TCR.AS` 探测、x86「无 PCID / 忽略 asid」与 SDM 对照；语言整理；去掉「待加强」备忘。
- 2026-08-29：整篇重做——软件号 vs 硬件标签；无 generation 代价；alloc 落点；与 mask / 拆除收尾链。
- 2026-08-26：v0.1 初稿。
