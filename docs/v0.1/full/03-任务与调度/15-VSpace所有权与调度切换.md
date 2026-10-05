# VSpace 所有权与调度切换

v0.1 · 2026-09-26

本篇覆盖：`schedule()` 的用户 AS 块（`kernel/task/task_manager.c`）、`thread->vs` 转入 / 放出（`thread.c` / `thread.h`）、`vspace_clear_user_mappings` / `del_vspace` 与 mask 门闩（`vmm.c` / `vmm.h`）、`percpu(current_vspace)`。

Radix / `create` / `clone` 细节见页表与 Radix 篇；mask 跨核怎么刷见 `11-TLB与缓存一致性.md`；ASID 号码见 ASID 篇；RR / zombie 见 `13-线程与Task_Manager.md`；ELF / Path A·B 见创建篇。

---

## 1. 概述

用户 `VSpace` 既是线程身份的一部分，又是硬件页表根 / ASID / TLB 的对象。若「线程释放完就 `del`」，可能此时某核仍缓存着这份翻译，或 mask 还记录该核。

两条引用要分开看：

| 钉子 | 谁持有 | 干什么 |
|------|--------|--------|
| **所有权 ref** | `thread->vs` | 钉住对象生命周期；`create_thread` / `copy_thread` **转入**（无二次 get）；回收时 **只 put**，不卸 CR3 / TTBR0 |
| **schedule CPU 额外 ref** | 本核逻辑 / `current_vspace` 仍关联该 user AS（AS = 地址空间，即 VSpace）时 | 钉住「本核还可能用着这份翻译」；切入 **另一** user（含 root→首个 user、leftover A→B）时 **get 新**；旧非 `&root_vspace` 才 **put 旧**；user→kernel **不** put |

再加 `tlb_cpu_mask`：哪些核**可能**还缓存着该 AS 的翻译。三者一起，才能在 `del` / 回收 ASID 前断定「没人再用」。

**刻意延迟：** 切到 idle / 内核时 **不**把硬件改回 `root_vspace`，也 **不** clear mask / put CPU extra。内核高半区各 AS 共享，内核路径不依赖用户低半正确性；代价是 leftover user AS 可能被某核的 extra + mask 钉住，直到之后某次 **user→user** 才清除。

这和「创建时绑核、不轻易迁核」一致：随意迁核会破坏 percpu `me`、IPC 归属，以及 mask「谁跑过这 AS」的假设。

---

## 2. 目标与边界

**提供：** 所有权转入规则；`schedule` 何时装用户根、六步顺序；user→kernel 滞后语义；`allow_self_use` / `del` 与 mask 门闩；boot / `root` 特例。

**不做：** 展开 IPI / `tlbi`（TLB 篇）；ASID bitmap（ASID 篇）；RR / zombie（TM 篇）；fork 标志解释（compat）；强制进内核时换回 root。

---

## 3. 分层与调用方

**用户 AS** — `create` / `clone` → `register_vspace` → 把 **活引用** 交给 `create_thread` / `copy_thread` / `gen_thread_from_elf`。register 须在线程接管前完成；unregister 在最后 `ref_put` → `free_vspace_ref`。

**内核 / server** — `gen_thread_from_func`：get root → create（不设 USER）→ **永不**走 AS 切换。

**boot** — `new_thread_structure`，`vs == NULL`，不设 USER。

**in-place exec** — `vspace_clear_user_mappings(vs, handler, true)`：远程 mask 须清；本核位可留（见 §6.4）。

常见错误：create 成功后再 put vs；USER + `&root_vspace`；未 quiesce（远端核尚未切走、mask 未清零）就 clear / del；以为切到 idle 就会清 mask。

---

## 4. 数据结构与不变量

### 4.1 VSpace（本篇相关）

- `refcount` — 末 put → `free_vspace_ref` → unregister + `del_vspace`
- `tlb_cpu_mask` + lock — schedule set / clear；unmap / shootdown / clear / del 的门闩
- `vspace_root_addr` / `asid` — 装硬件
- `root_vs` — 用户指向 `&root_vspace`；root 自指

### 4.2 线程与 per-CPU

- `thread->vs` — 所有权；boot 为 NULL
- `THREAD_FLAG_USER` — **门闩**：仅 next 带此位才尝试换用户 AS（不是「有 vs 就换」）
- `percpu(current_vspace)` — 本核**逻辑**关联的 AS（`virt_mm_init` = root）；仅成功切到另一 user 时更新；进内核**不**改回 root

### 4.3 ref 来源

1. 每条绑定该 AS 的用户线程 ownership（各 1）
2. 每核 schedule extra（user→另一 user 时对旧 put；user→kernel **不** put）
3. `root_vspace`：`ref_init(1)` + 每个内核线程转入一条；永不 `del`

不变量：

- create / copy 成功 ⇒ caller 不得再持传入 vs（copy **失败也会** put，见创建篇）
- USER ⇒ `vs` 非 NULL 且 ≠ `&root_vspace`
- `current_vspace == user_vs` ⇒ 本核通常已持 schedule extra，且 mask 本地位已 set（同 vs 再入不重复 get）
- `del` / `clear(..., false)` ⇒ mask 全零（`allow_self_use` 例外见下）

VSpace refcount 生命周期（以一个用户 AS 绑定两个线程、在两核上跑过为例）：

```text
   refcount 来源        事件                              ref 值   tlb_cpu_mask
   ──────────────────────────────────────────────────────────────────────────
   create_vspace        ref_init(1)                      1        {}
   register_vspace     (无 ref 变化)                     1        {}
   create_thread(T1)   ownership 转入 (无 get)            1        {}
   T1 首次 schedule    schedule extra: get               2        {cpu0}
                       mask_set(cpu0), 装 CR3/TTBR0
   create_thread(T2)   ownership 转入 (无 get)            2        {cpu0}
   T2 在 cpu1 首次跑    schedule extra: get               3        {cpu0,cpu1}
                       mask_set(cpu1), 装根
   T1 exit → zombie     ownership put                     2        {cpu0,cpu1}
   cpu0 切到另一 user   schedule extra: put(T1 所在 vs)    1        {cpu1}
                       mask_clear(cpu0), 本地 invalidate
   T2 exit → zombie     ownership put                     0        {cpu1}
   free_vspace_ref      unregister                        0        {cpu1}
   del_vspace           clear(..., false) 要求 mask 全零  →  -E_REND_RC_UNEQUAL
                       (cpu1 位仍在 → 半死对象陷阱)
   ──────────────────────────────────────────────────────────────────────────
   正常路径下 cpu1 也会先切走、mask 清零，再 del_vspace 才能成功 → asid_free
```

「半死对象」正是 `del_vspace` 在 mask 仍非零时返回 `-E_REND_RC_UNEQUAL` 留下的状态：已 unregister、refcount=0、但 ASID 与根页还没释放，且无公开修复 API——只能靠不变量（所有核都已切走、mask 清零）避免。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `task_manager.c` `schedule` USER 块 | get / set mask / 装根 / 清旧 / put |
| `thread.h` create / copy 注释 | 所有权约定 |
| `thread.c` `thread_release_owned_resources` | 只 put ownership |
| `vmm.c` clear / del / free_vspace_ref | mask 门闩、asid_free |
| `virt_mm_init` | `current_vspace = &root_vspace` |

---

## 6. 流程

### 6.1 所有权转入 / 转出

| 路径 | 行为 |
|------|------|
| `create_thread` | 成功转入；失败未赋值则 caller 仍持有 |
| `copy_thread` | 成功转入；**任意失败 put 传入 vs** |
| `gen_thread_from_func` | 内部 get root 再 create |
| `gen_thread_from_elf` | 内部 create + register；USER 在 create **之后**才 set |
| 回收路径 | `fini`（vs 仍有效）→ put ownership；**不**卸硬件 |
| boot | `vs` 保持 NULL |

### 6.2 `schedule` 决策

仅当 **next** 带 `THREAD_FLAG_USER`：

1. `new_vs` 空或 == root → 报错，`use_old_thread`（不 switch）
2. `old_vs = current_vspace`；`old == new` → **无操作**（不 get、不改 mask、不装根）
3. 否则走 §6.3

| 转移 | 行为 |
|------|------|
| user → kernel / idle | **整段跳过**：硬件 / `current_vspace` / mask / extra **全部滞后** |
| kernel → 同一 user | `old==new`，无操作 |
| kernel → 另一 user B（`current_vspace` 仍为 A） | 按 user→user：get B … put A |
| root → 首个 user | get + set + 装根；**不对 root put** |

### 6.3 六步顺序（`old != new`，不可乱）

```text
ref_get_not_zero(new)          // 失败 → 回退
vs_tlb_cpu_mask_set(new)       // 先 set 新
arch_set_current_user_vspace_root_asid(new)
percpu(current_vspace) = new
if old != &root_vspace:
    本地 invalidate 旧 AS
    vs_tlb_cpu_mask_clear(old)
    ref_put(old)               // 放 CPU extra；不对 root put
```

为何这样排：

- **先 get 再装表**：避免装上一个已在回收中的 AS。
- **先 set 新 mask 再装硬件**：本核已在新 AS 上运行时，别人发 shootdown 不能漏掉本核（TLB 篇同理）。
- **先刷旧再 clear 再 put**：mask 里已经没本核了，本地 TLB 却还脏着，会漏刷。
- **先装新再释放旧**：中途故障还能退回，不必先把自己变成「无根」。

CR3 / TTBR0 切换时序（六步在 `schedule` 解锁后、`switch_to` 之前完成；`switch_to` 本身不碰页表根）：

```text
   schedule()                arch_set_current_user_vspace_root_asid(new)        switch_to()
   ──────────┬───────────────────────────┬────────────────────────────────────┬──────────►
             │ lock sched_lock           │                                    │
             │ old = percpu(current_vspace)                                   │
             │ old != new:               │ x86_64:  CR3 ← new->vspace_root_addr │
             │   ref_get_not_zero(new)   │         (无 PCID 时冲掉本核 user TLB)│
             │   mask_set(new)           │ aarch64: TTBR0_EL1 ← root|ASID       │
             │   ─────────────────────►  │         (TCR.AS=1，按 ASID 区分)    │
             │   percpu(current_vspace)=new                                   │
             │   if old != root:         │                                    │
             │     本地 invalidate old AS│                                    │
             │     mask_clear(old)       │                                    │
             │     ref_put(old)          │                                    │
             │ unlock                    │                                    │ 保存 callee-saved
             │                           │                                    │ + 内核栈指针
             │                           │                                    │ ret → 新线程
```

页表根寄存器位域示意：

```text
   x86_64 CR3 (Intel SDM Vol.3 §4.10, 64 bit)
   ┌─────────────────────────────────────────────────────────┐
   │ 63      52 51                 12 11           5 4     0  │
   │ ├─ PCID ──┤├── 物理页帧号 (PPN, 40 bit) ──┤├ 保留 ─┤├flags┤│
   │                                  4 KiB 对齐 │          │   │
   └─────────────────────────────────────────────────────────┘
   本实现未开 PCID → 写 CR3 即冲掉本核 user TLB 项（除 global 项）

   aarch64 TTBR0_EL1 (ARM ARM D8.2.3, with TCR.EPD0/AS)
   ┌─────────────────────────────────────────────────────────┐
   │ 63      48 47                                          0 │
   │ ├─ ASID ─┤├──────── BADDR (物理根, 47 bit 对齐) ────────┤│
   │  8 bit    TCR.T0SZ 决定有效位宽；ASID 由 TCR.A1 选 TTBR0/1 │
   └─────────────────────────────────────────────────────────┘
   跨核广播 invalidate 仍走 TLB 篇的 tlbi ...is / asid
```

### 6.4 `allow_self_use` / `del` / ASID

`vspace_clear_user_mappings(vs, handler, allow_self_use)`：

- 保留对象 / ASID / L0 / 内核高半；拆除用户映射与用户 PT。
- 拒绝 root 形态。
- **门闩：**
  - `allow_self_use && current_vspace == vs` → 只允许本 CPU 一位；远端位 → `-E_REND_RC_UNEQUAL`
  - 否则 → mask **全零**
  - `allow_self_use=true` 但 `current_vspace != vs` → **退化成全零要求**

in-place exec：远程须 quiesce；**本核位可留**；ASID **不换**。不是「exec 前 mask 必须全 0」。

`del_vspace`：已 unregister → `clear(..., false)`（mask 全零）→ 拆结构 → **`asid_free`**（asid_free **不**再查 mask；门闩全在 clear）。无 generation：靠 mask 空 + 本地已刷。

**陷阱：** `free_vspace_ref` 先 unregister 再 del；若 del 因 mask 失败，会留下已注销、refcount 0 的半死对象。正常情况下靠不变量避免——**不是**可重试 API。失败码是 `-E_REND_RC_UNEQUAL`，不是旧稿里的 `-E_REND_AGAIN`。

### 6.5 合法但容易被当作 bug 的状态

- 本核在跑 idle，但 `current_vspace` 仍是已退出进程的 vs。
- 末线程 ownership put 后对象未必立刻 `del`——常被某核 schedule extra 钉住。
- 内核线程硬件仍指向上一用户低半——只用而不访问低半即可。

---

## 7. 公开 API

本篇**不**单独导出新符号；写清的是「所有权转入 / `schedule` 换根六步 / clear·del 门闩」这套调用约定。符号说明以 `vmm.h` / `thread.h` 的 Doxygen 为准（已按 `.c` 补强本篇关心的部分）。生命周期工厂签名见 `07`；`schedule` 全貌见 `13`；create/copy 转入见 `14`。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序 |
|------|------|
| 用户 AS 交给线程 | `create_vspace` / `clone_vspace` → **`register_vspace`** → 把 **活引用** 交给 `create_thread` / `copy_thread`（**无二次 get**） |
| 调度切入另一用户 AS | 仅当 next 带 `THREAD_FLAG_USER` 且 `old≠new`：见 §7.3 六步 |
| user → kernel / idle | **整段跳过**换根：硬件 / `current_vspace` / mask / CPU extra **滞后** |
| 原地 exec clear | 远程 mask 先 quiesce → **`vspace_clear_user_mappings(vs, h, true)`**（本核位可留）→ load；**不换 ASID** |
| 末次 ownership put | **`free_vspace_ref`**：`unregister` → `del_vspace`（`clear(..., false)` 要求 mask **全零**）→ `asid_free` |

### 7.2 所有权转入（`thread.h`；细节 `13`/`14`）

| 接口 | 本篇约定 |
|------|----------|
| `create_thread` | 成功：`thread->vs = vs`（接管活引用）；失败且尚未赋值时，caller 仍持有 |
| `copy_thread` | 成功转入；**任意失败路径都会 put 传入的 vs** |
| `gen_thread_from_func` | 内部 get root 再 create（转入 root） |
| `gen_thread_from_elf` | 内部 create+register；`THREAD_FLAG_USER` 在 create **之后**才设 |
| 回收路径 | append `fini` 时 vs 仍有效 → put ownership；**不**卸 CR3/TTBR |
| boot | `vs` 保持 NULL |

### 7.3 `schedule` 用户 AS 块（`task_manager.c`；API 见 `13`）

仅当 **next** 带 `THREAD_FLAG_USER`：

1. `new_vs` 空或 `== &root_vspace` → 报错，不 switch
2. `old_vs == new_vs` → **无操作**（不 get / 不改 mask / 不装根）
3. 否则六步（**不可乱序**，详见 §6.3 的代码与「为何这样排」）

| 转移 | 行为 |
|------|------|
| user → kernel/idle | 跳过整段（滞后） |
| root → 首个 user | get + set + 装根；**不对 root put** |
| kernel → 另一 user（`current_vspace` 仍为 A） | 按 user→user：get B … put A |

装根硬件见 §8；IPI/`tlbi` 见 `11`；ASID 号码见 `12`。

### 7.4 clear / del / ref（`vmm.h`；工厂见 `07`）

```c
error_t vspace_clear_user_mappings(VSpace *vs, struct map_handler *h,
                                   bool allow_self_use);
error_t free_vspace_ref(ref_count_t *refcount);
error_t unregister_vspace(VSpace *vs);
error_t del_vspace(VSpace **vs);
extern VSpace *current_vspace; /* per-CPU；用 percpu() */
```

| 接口 | 说明 |
|------|------|
| `vspace_clear_user_mappings` | 清用户低半，保留对象 / ASID / 根帧 / 内核半。禁止对 root。门闩：`allow_self_use && current_vspace==vs` → 只许本核一位；否则（含 allow 但 current≠vs）→ mask **全零**；失败返回 `-E_REND_RC_UNEQUAL`（**不是** `-E_REND_AGAIN`）。 |
| `del_vspace` | 须已 unregister；`clear(..., false)` → 销毁 radix → 释放根页 → **`asid_free`**（不再查 mask）→ 释放结构；`*vs=NULL`。仍 registered → `-E_IN_PARAM`。 |
| `free_vspace_ref` | **先 unregister 再 del**。若 del 因 mask 失败，会留下已注销、refcount 为 0 的半死对象；**没有公开的重试 / 修复接口**（靠不变量避免）。 |
| `percpu(current_vspace)` | 逻辑上的当前 AS；user→kernel **不会**清除。 |

`create` / `clone` / `register` 签名与一般说明见 `07` §7.4；本篇只钉「何时 register、何时转入、何时敢 clear/del」。

---

## 8. 多架构：装根时硬件在做什么

`arch_set_current_user_vspace_root_asid(root, asid)` 是六步里唯一写页表根寄存器的地方；`switch_to` **不**碰它。

- **x86_64（Intel SDM：CR3、TLB）：** 把用户页表物理根写入 **CR3**。当前未开 PCID 时，换 CR3 会冲掉本核与该地址空间相关的用户 TLB 项；`asid` 参数可被忽略（软件 ASID 仍在 `VSpace` 里记账，见 ASID 篇）。本地再刷旧 AS 时走 `arch_tlb_invalidate_vspace_page` 一类路径。
- **aarch64（ARM ARM：TTBR0_EL1、TCR.AS）：** 把根与 **ASID** 一并打进 **TTBR0_EL1**（具体位域见 ASID 篇）；随后对本核旧 ASID 做本地 invalidate。跨核广播仍归 TLB 篇的 `tlbi …is`。

内核高半区共享：换用户根只影响低半翻译；这是「切到内核可不换回 root」能站得住的硬件前提。

---

## 9. 测试

无单独「所有权」测试用例；随用户 create / 切换 / exec clear / 回收路径与 SMP unmap 覆盖。

---

## 10. 限制与后续

- user→kernel 滞后延长 leftover AS 寿命。
- 无运行期迁移；mask 假设绑核。
- del 失败半死对象靠不变量，无公开修复 API。
- 远期迁移须重做 mask / 归属模型（evolution E3）。

---

## 11. 变更记录

- 2026-10-05：任务 1/3/5 精读——§9「测例」→「测试用例」；§1「放完就 del」→「释放完就 del」、「装着这份翻译」→「缓存着这份翻译」、「记着该核」→「记录该核」、「才敢…说」→「才能…断定」、「咬合」→「一致」、「乱迁会打穿」→「随意迁核会破坏」；§1 表首次出现 AS 补说明（= 地址空间，即 VSpace）；§3「未 quiesce」补说明（远端核尚未切走、mask 未清零）；§6.3「先装新再丢旧」→「先装新再释放旧」、`ref_put(old)` 补「放 CPU extra；不对 root put」注释；§6.4「拆用户映射」→「拆除用户映射」；§6.5「不碰低半」→「不访问低半」；§7.4「毁 radix / 放根页 / 放结构」→「销毁 radix / 释放根页 / 释放结构」、「清掉」→「清除」；§7.3 重复的六步代码块改为交叉引用 §6.3。
- 2026-10-04：补硬件/架构知识——§6.3 加 CR3 / TTBR0 切换时序图与寄存器位域示意（Intel SDM Vol.3 §4.10、ARM ARM D8.2.3）；§4.3 加 VSpace refcount 生命周期图，演示 ownership / schedule extra / tlb_cpu_mask 三者咬合与「半死对象」陷阱。语言润色：不乱迁→不轻易迁核、钉到→钉住。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：对照 `schedule` USER 块——纠正 §1「仅 user→另一 user 才 get/put」：实为切入另一 user 即 get 新，旧非 root 才 put；user→kernel 仍不 put。
- 2026-09-26：§7 全文审阅——补强 `free_vspace_ref` / clear 门闩 / `current_vspace` Doxygen；写清转入·六步·回收编排与半死对象陷阱。
- 2026-09-25：语言整理；§6.3 补「为何」；§8 扩写 CR3 / TTBR0 装根与「内核可不换根」的硬件前提。
- 2026-08-29：整篇重做——双引用叙述；六步顺序；user→kernel 滞后写死；`allow_self_use` 精确条件；失败码 `-E_REND_RC_UNEQUAL`；与 TLB / ASID / TM 分工。
- 2026-08-26：初稿。
- 2026-10-05：最终词句顺畅。
