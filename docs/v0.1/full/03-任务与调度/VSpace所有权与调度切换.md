# VSpace 所有权与调度切换

v0.1 · 2026-08-29

本篇覆盖：`schedule()` 的用户 AS 块（`kernel/task/task_manager.c`）、`thread->vs` 转入/放出（`thread.c` / `thread.h`）、`vspace_clear_user_mappings` / `del_vspace` 与 mask 门闩（`vmm.c` / `vmm.h`）、`percpu(current_vspace)`。

Radix / `create`/`clone` 细节见页表与 Radix 篇；mask 跨核怎么刷见 `TLB与缓存一致性.md`；ASID 号码见 ASID 篇；RR / zombie 见 `线程与Task_Manager.md`；ELF/Path A·B 见创建篇。

---

## 1. 概述

用户 `VSpace` 既是线程身份的一部分，又是 HW 页表根 / ASID / TLB 的对象。若「线程放完就 `del`」，可能赶上某核还装着这份翻译、或 mask 还记着该核。

两条引用要分开看：

| 钉子 | 谁持有 | 干什么 |
|------|--------|--------|
| **所有权 ref** | `thread->vs` | 钉住对象生命周期；`create_thread`/`copy_thread` **转入**（无二次 get）；teardown **只 put**，不卸 CR3/TTBR |
| **schedule CPU 额外 ref** | 本核逻辑/`current_vspace` 仍关联该 user AS 时 | 钉住「本核还可能用着这份翻译」；**仅** user→**另一** user 时 get 新、put 旧 |

再加 `tlb_cpu_mask`：哪些核**可能**还缓存着该 AS 的翻译。三者一起，才敢在 `del` / 回收 ASID 前说「没人再用」。

**故意懒：** 切到 idle/内核时 **不**把 HW 改回 `root_vspace`，也 **不** clear mask / put CPU extra。内核高半区各 AS 共享，内核路径不依赖用户低半正确性；代价是 leftover user AS 可能被某核的 extra+mask 钉到之后某次 **user→user**。

这和「创建时绑核、不乱迁」咬合：乱迁会打穿 percpu `me`、IPC 归属，以及 mask「谁跑过这 AS」的假设。

---

## 2. 目标与边界

**提供：** 所有权转入规则；`schedule` 何时装用户根、六步顺序；user→kernel 滞后语义；`allow_self_use` / `del` 与 mask 门闩；boot/`root` 特例。

**不做：** 展开 IPI/`tlbi`（TLB 篇）；ASID bitmap（ASID 篇）；RR/zombie（TM 篇）；fork 标志解释（compat）；强制进内核时换回 root。

---

## 3. 分层与调用方

**用户 AS** — `create`/`clone` → `register_vspace` → 把 **live ref** 交给 `create_thread` / `copy_thread` / `gen_thread_from_elf`。register 须在线程接管前完成；unregister 在最后 `ref_put` → `free_vspace_ref`。

**内核/server** — `gen_thread_from_func`：get root → create（不设 USER）→ **永不**走 AS 切换。

**boot** — `new_thread_structure`，`vs == NULL`，不设 USER。

**in-place exec** — `vspace_clear_user_mappings(vs, handler, true)`：远程 mask 须清；本核位可留（见 §6.4）。

常见错误：create 成功后再 put vs；USER + `&root_vspace`；未 quiesce 就 clear/del；以为切到 idle 就会清 mask。

---

## 4. 数据结构与不变量

### 4.1 VSpace（本篇相关）

- `refcount` — 末 put → `free_vspace_ref` → unregister + `del_vspace`  
- `tlb_cpu_mask` + lock — schedule set/clear；unmap/shootdown / clear/del 的门闩  
- `vspace_root_addr` / `asid` — 装 HW  
- `root_vs` — 用户指向 `&root_vspace`；root 自指  

### 4.2 线程与 per-CPU

- `thread->vs` — 所有权；boot 为 NULL  
- `THREAD_FLAG_USER` — **门闩**：仅 next 带此位才尝试换用户 AS（不是「有 vs 就换」）  
- `percpu(current_vspace)` — 本核**逻辑**关联的 AS（`virt_mm_init` = root）；仅成功切到另一 user 时更新；进内核**不**改回 root  

### 4.3 ref 来源

1. 每条绑定该 AS 的用户线程 ownership（各 1）  
2. 每核 schedule extra（user→另一 user 时对旧 put；user→kernel **不** put）  
3. `root_vspace`：`ref_init(1)` + 每个内核线程转入一条；永不 `del`  

**不变量：**

- create/copy 成功 ⇒ caller 不得再持传入 vs（copy **失败也会** put，见创建篇）  
- USER ⇒ `vs` 非 NULL 且 ≠ `&root_vspace`  
- `current_vspace == user_vs` ⇒ 本核通常已持 schedule extra，且 mask 本地位已 set（同 vs 再入不重复 get）  
- `del` / `clear(..., false)` ⇒ mask 全零（`allow_self_use` 例外见下）  

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `task_manager.c` `schedule` USER 块 | get / set mask / 装根 / 清旧 / put |
| `thread.h` create/copy 注释 | 所有权契约 |
| `thread.c` `thread_release_owned_resources` | 只 put ownership |
| `vmm.c` clear / del / free_vspace_ref | mask 门闩、asid_free |
| `virt_mm_init` | `current_vspace = &root_vspace` |

---

## 6. 流程

### 6.1 所有权转入/转出

| 路径 | 行为 |
|------|------|
| `create_thread` | 成功转入；失败未赋值则 caller 仍持有 |
| `copy_thread` | 成功转入；**任意失败 put 传入 vs** |
| `gen_thread_from_func` | 内部 get root 再 create |
| `gen_thread_from_elf` | 内部 create+register；USER 在 create **之后**才 set |
| teardown | `fini`（vs 仍有效）→ put ownership；**不**卸 HW |
| boot | `vs` 保持 NULL |

### 6.2 `schedule` 决策

仅当 **next** 带 `THREAD_FLAG_USER`：

1. `new_vs` 空或 == root → 报错，`use_old_thread`（不 switch）  
2. `old_vs = current_vspace`；`old == new` → **无操作**（不 get、不改 mask、不装根）  
3. 否则走 §6.3  

| 转移 | 行为 |
|------|------|
| user → kernel/idle | **整段跳过**：HW/`current_vspace`/mask/extra **全部滞后** |
| kernel → 同一 user | `old==new`，无操作 |
| kernel → 另一 user B（`current_vspace` 仍为 A） | 按 user→user：get B … put A |
| root → 首个 user | get+set+装根；**不对 root put** |

### 6.3 六步顺序（`old != new`，不可乱）

```text
ref_get_not_zero(new)          // 失败 → 回退
vs_tlb_cpu_mask_set(new)       // 先 set 新
arch_set_current_user_vspace_root_asid(new)
percpu(current_vspace) = new
if old != &root_vspace:
    本地 invalidate 旧 AS
    vs_tlb_cpu_mask_clear(old)
    ref_put(old)
```

- 先 get 再装表；先 set 新 mask 再装 HW；先刷旧再 clear 再 put；先装新再丢旧。  
- 细节与 TLB 篇一致；本篇钉「为何」与「何时」。

### 6.4 `allow_self_use` / `del` / ASID

`vspace_clear_user_mappings(vs, handler, allow_self_use)`：

- 保留对象 / ASID / L0 / 内核高半；拆用户映射与用户 PT。  
- 拒绝 root 形态。  
- **门闩：**  
  - `allow_self_use && current_vspace == vs` → 只允许本 CPU 一位；远端位 → `-E_REND_RC_UNEQUAL`  
  - 否则 → mask **全零**  
  - `allow_self_use=true` 但 `current_vspace != vs` → **退化成全零要求**  

in-place exec：远程须 quiesce；**本核位可留**；ASID **不换**。不是「exec 前 mask 必须全 0」。

`del_vspace`：已 unregister → `clear(..., false)`（mask 全零）→ 拆结构 → **`asid_free`**（asid_free **不**再查 mask；门闩全在 clear）。无 generation：靠 mask 空 + 本地已刷。

**陷阱：** `free_vspace_ref` 先 unregister 再 del；若 del 因 mask 失败，会留下已注销、refcount 0 的半死对象。正常靠不变量避免——**不是**可重试 API。失败码是 `-E_REND_RC_UNEQUAL`，不是旧稿里的 `-E_REND_AGAIN`。

### 6.5 合法但容易当 bug 的状态

- 本核在跑 idle，但 `current_vspace` 仍是已退出进程的 vs。  
- 末线程 ownership put 后对象未必立刻 `del`——常被某核 schedule extra 钉住。  
- 内核线程 HW 仍指向上一用户低半——只用不碰低半即可。

---

## 7. 公开 API（本篇语义）

| API | 本篇关心的契约 |
|-----|----------------|
| `create_thread` / `copy_thread` | vs 转入；copy 失败也 put |
| `schedule` | USER 门闩 + 六步 + 滞后 |
| `vspace_clear_user_mappings` | `allow_self_use` 精确条件 |
| `del_vspace` / `free_vspace_ref` | mask 全零；先 unregister |
| `percpu(current_vspace)` | 逻辑当前 AS |

装根 / invalidate 符号见 arch 与 TLB 篇。

---

## 8. 多架构

`arch_set_current_user_vspace_root_asid`：aarch64 打 ASID 进 TTBR；x86 装 CR3，asid 参数可被忽略（PCID 未用）——一句即可，细节归 ASID 篇。

---

## 9. 测试

无单独「所有权」测例；随用户 create/切换/exec clear/teardown 与 SMP unmap 覆盖。本篇未复测。

---

## 10. 限制与后续

- user→kernel 滞后延长 leftover AS 寿命。  
- 无运行期迁移；mask 假设绑核。  
- del 失败半死对象靠不变量，无公开修复 API。  
- 远期迁移须重做 mask/归属模型（evolution E3）。

---

## 11. 变更记录

- 2026-08-29：整篇重做——双引用叙述；六步顺序；user→kernel 滞后写死；`allow_self_use` 精确条件；失败码 `-E_REND_RC_UNEQUAL`；与 TLB/ASID/TM 分工。
- 2026-08-26：初稿。
