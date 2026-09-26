# ASID 与地址空间标识

v0.1 · 2026-09-25

本篇覆盖：`kernel/mm/asid.c`、`include/rendezvos/mm/asid.h`、`include/arch/x86_64/mm/asid.h`、`include/arch/aarch64/mm/asid.h`，以及 aarch64 boot 里与 `TCR_EL1.AS` 相关的探针（见平台启动篇）。

TLB mask / shootdown 见 `11-TLB与缓存一致性.md`；调度时装根见 `03-任务与调度/15-VSpace所有权与调度切换.md`；`create_vspace` 见页表篇。

---

## 1. 概述

**ASID**（Address Space ID）在这里首先是软件给用户地址空间贴的编号：切换时若硬件认得这个标签，就不必把用户 TLB 一锅端。

但软件号 **不等于** 硬件一定在用：

- **aarch64：** 装用户根时，把 ASID 打进 **TTBR0_EL1** 的高位；`tlbi` 也可以按 ASID 靶向失效。boot 若从 `ID_AA64MMFR0_EL1` 读到 16-bit ASID，还会打开 `TCR_EL1.AS`（平台启动篇 §4.6 提过）。
- **x86：** `vs->asid` 照样分配，但 `arch_set_current_user_vspace_root_asid` **丢掉** asid 参数，只写 **CR3**；invalidate 路径里也对 asid 做 `(void)asid`。头文件里虽有 `CR3_PCID` / `CR4_PCIDE` 一类宏，**当前未启用 PCID**。号码主要是为了 API 对称，以及将来若开 PCID 时字段已经在。

没有 **generation / epoch**：同一个数字回收后再发给别人，全靠「mask 已经清空、且相关核本地已经刷过」保证没有人还揣着旧翻译。这条纪律破了，就会静默错译——这是刻意不做 generation 换来的代价。

---

## 2. 目标与边界

提供：全局 bitmap 分配 / 释放；`asid_init`；与 create / del / schedule 的耦合；硬件侧 TTBR /（未用的）PCID 对照。

不做：x86 PCID 启用；ASID 世代号；riscv / loongarch 主线。

---

## 3. 分层与调用方

- `asid_init`：**仅 BSP** 在 `virt_mm_init` 里、`init_root_vspace` 之前调用。
- `asid_alloc`：走 `alloc_user_vs_structure`（`create_vspace` / `clone_vspace` 都经此）。
- 装 TTBR0|ASID 或 CR3：在 `schedule` 里，配合 TLB 篇的 mask 顺序。
- `asid_free`：在 `del_vspace` 等把用户映射拆干净、mask 已空之后。

`root_vspace.asid = 0`（保留号，不是普通用户 AS）。clone **不会**继承父 ASID，两边各自新号。

---

## 4. 数据结构与不变量

### 4.1 软件分配器

全局位图，容量跟 `arch_asid_supports_16bit()` 走：为真则扫到 `U16_MAX`，否则到 `U8_MAX`。**0 永远保留**；分配失败返回 0，create 路径会释放已建结构体。锁是 MCS：`asid_lock` + 本核 `asid_mcs_node`。

注意 x86 的 `arch_asid_supports_16bit()` **恒为 true**，所以分配器以为自己有很大空间，但硬件根本不用这些号——不要写成「x86 已经启用了 16-bit ASID/PCID」。

### 4.2 硬件怎么带 ASID（官方对照）

**aarch64（ARM ARM，TTBR / TCR / TLBI）**

- Stage 1 EL1&0 下，用户低半根在 **TTBR0_EL1**。实现里：

  ```c
  ttbr0 = u_root | ((u64)asid << 48);
  msr TTBR0_EL1, ttbr0;
  ```

  也就是把软件 ASID 放进 TTBR0 的高 16 位字段（与常见 AArch64 ASID 打包方式一致；具体位宽还受 `TCR_EL1.AS` 约束）。
- `TCR_EL1.AS`：硬件宣称支持 16-bit ASID 时，boot 的 `init_mmu` 可以置上，否则按 8-bit 理解更稳妥。本仓库用 `ID_AA64MMFR0_EL1` 的 ASIDBits 字段探测。
- 页表叶上的 **nG（非全局）** 位：非全局项才会跟着 ASID 走；全局项换 ASID 也不一定清掉。这和页表篇里 `PAGE_ENTRY_GLOBAL` → 清/置 `nG` 的编解码是对上的。
- 失效：`tlbi vae1` / `aside1` 等把 ASID 编进操作数（TLB 篇 §4.3）。

**x86_64（Intel SDM，PCID / CR3）**

- 开启 **PCID** 时，CR3 低 12 位可带 process-context identifier，换地址空间时可以选择不冲光全部 TLB。本实现：**不写 PCID，不置 `CR4.PCIDE`**；`arch_set_current_user_vspace_root_asid` 直接忽略 asid，只把根物理地址写入 CR3。
- 因此当前 x86 路径上，软件 ASID 更像「预留字段」；跨核仍靠 mask + IPI `invlpg` / 全清（TLB 篇）。

### 4.3 无 generation 时的回收安全链

1. schedule 只在本地刷完旧 AS 之后，才 clear mask 位；
2. mask 里还有位时，unmap / shootdown / clear 仍然罩得住那些核；
3. `vspace_clear_user_mappings(allow_self_use=false)` / `del_vspace` 要求 mask 空才放行；
4. 用户→内核可以暂时留着 mask 和引用——ASID 还活着，直到之后某次用户→用户切换清掉。

in-place exec（`allow_self_use=true`）可以保留本 CPU 位，**ASID 不换号**。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `asid.c` / `asid.h` | bitmap alloc / free / init |
| `arch/*/mm/asid.h` | `arch_asid_supports_16bit` |
| `arch/*/mm/vmm.h` | `arch_set_current_user_vspace_root_asid` |
| `vmm.c` | create / del 生命周期 |
| `task_manager.c` | 切换时装根 |
| aarch64 `boot.S` `init_mmu` | `TCR.AS` 探针 |

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
asid_t asid_alloc(void);   /* 0 = 失败 */
void asid_free(asid_t asid);
asid_t asid_get_max(void);
bool arch_asid_supports_16bit(void);
```

类型以头文件为准（`asid_t` 为 u16）。

---

## 8. 多架构

见 §1 与 §4.2。riscv / loongarch 不是 v0.1 主线。

---

## 9. 测试

没有单独的 ASID 测例；随用户 AS 的创建 / 切换 / 销毁间接覆盖。本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- 无 generation：完全依赖 TLB 篇的 mask / 刷序纪律。
- 号码耗尽则无法 create。
- x86 PCID 属远期；正文不要写成已支持。

---

## 11. 变更记录

- 2026-09-25：补 TTBR0 ASID 打包、`TCR.AS` 探测、x86「无 PCID / 忽略 asid」与 SDM 对照；语言整理；去掉「待加强」备忘。
- 2026-08-29：整篇重做——软件号 vs 硬件标签；无 generation 代价；alloc 落点；与 mask / teardown 链。
- 2026-08-26：v0.1 初稿。
