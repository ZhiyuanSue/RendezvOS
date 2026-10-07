# EBR 与线程资源回收

v0.1 · 2026-09-26

本篇覆盖：`kernel/task/ebr.c`、`include/rendezvos/task/ebr.h`、`delete_thread` / `free_thread_ref` / `del_thread_structure` / `thread_release_owned_resources`（`thread.c`），以及与 `schedule` 钩子的交汇。

MSQ 算法与队列内 `ebr_enter` / `exit` 见 `04-IPC/18-无锁队列与EBR设计.md`；消息 / request 何时 put 见 Port 篇；zombie / `EXIT_REQUESTED` 见 TM 篇；`vs` 只 put 不卸硬件见 VSpace 篇；跨核 kfree 排水见 kmalloc 篇（**不是** EBR）。

---

## 1. 概述

这里其实是**两件相关但不同的事**，标题勿读成「一切回收都是 EBR（Epoch-Based Reclamation，基于世代的回收）」。

- **MSQ 节点**（`Message_t` / `Ipc_Request_t`）走 EBR 延迟回收——无锁读路径上有「先看见指针、再被释放」的窗口。
- **Thread_Base 本身**（TCB、kstack、name、ownership `vs`）靠 refcount **同步**释放，不依赖 EBR。

二者在 `schedule` 排水、线程 teardown 排空消息队列、以及 `Ipc_Request` 持有的 thread ref 上交汇（对象表与不变量见 §4）。

---

## 2. 目标与边界

**提供：** 最小 EBR（enter / exit、global epoch、固定 retire 表）；线程回收顺序与禁止二次 drain；schedule 推进 reclaim。

**不做：** 通用 GC；overflow 自动扩容（改 `EBR_RETIRE_SLOTS` 或降 churn）；替上层决定何时 `delete_thread`；把跨核 kfree 算进 EBR。

name / kstack：**直接** `m_free`，不走 EBR。

---

## 3. 分层与调用方

**MSQ / IPC** — 队列 CAS 外包 `ebr_enter` / `exit`；`free_message_ref` / `free_ipc_request` → `ebr_retire_ref(..., *_real)`。

**任意路径** — 间接经 `schedule` → `ebr_try_reclaim`。

**compat / clean** — 仍用 `delete_thread`；勿假设 message 地址在 put 后立刻可复用。观测退出用 `EXIT_REQUESTED` + zombie，不是节点立即可回收。

**调试** — `ebr_dump_stats()`。

---

## 4. 数据结构与不变量

两类对象怎么放、为何不同：

| 对象 | 怎么放 | 为何 |
|------|--------|------|
| **MSQ（Message Queue）暴露的节点**（`Message_t` / `Ipc_Request_t`） | last `ref_put` → `ebr_retire_ref` → 等 epoch 安全再 `*_real` `m_free` | 无锁读 `head/tail/next` 时，即使有 refcount，仍有「先看到指针、再被释放」窗口 |
| **Thread_Base 本身**（TCB（Thread Control Block）、kstack、name、ownership `vs`） | refcount → `del_thread_structure` **同步**释放 | 线程不依赖 EBR；依赖「摘环 + 无人持有」 |

交汇点（与 §6 / 线程回收衔接）：

1. 每次 `schedule`：先 `kalloc_process_cross_cpu_frees()`，再 **`ebr_try_reclaim()`**（idle 重的核也要推进）。两钩子并列，不是同一种排水。
2. 线程回收排空 send / recv 时调 `free_message_ref` → **队列节点仍进 EBR**。
3. `Ipc_Request` 持有 `thread` ref；`free_ipc_request_real` 里 put thread → EBR 延迟可**推迟** TCB 真正释放。

### 4.1 Per-CPU EBR

| 量 | 含义 |
|----|------|
| `ebr_active_flag` / `ebr_nesting_depth` | 本核是否在读临界区 |
| `ebr_local_epoch` | enter 时快照的 global |
| `ebr_retired_recs[EBR_RETIRE_SLOTS]` | 默认 512：ref、free_func、retire_epoch |
| `ebr_global_epoch` | 全局；exit 到深度 0 时自增 |

lazy init：`ebr_inited` 0 / 1 / 2。

### 4.2 安全条件（必须写对）

```text
safe = global_epoch
for each online-ish cpu: if active: safe = min(safe, local_epoch)
reclaim iff rec.retire_epoch < safe     // 是 < 不是 ≤
```

`ebr_try_reclaim` **只扫本 CPU 槽**——节点 retire 在**调用 `ebr_retire_ref` 的那颗核**上；要依赖该核 schedule / exit / 再 retire 推进。

EBR 三阶段（quiet / propagate / reclaim）状态图：

```text
   ┌─────────────────────────────────────────────────────────────────┐
   │  global_epoch = E                                                 │
   │                                                                  │
   │  CPU 0:  active=1, local=E      ── 仍在读临界区 (quiet 未达)      │
   │  CPU 1:  active=0, local=E-1    ── 已退出，但落后一个 epoch        │
   │  CPU 2:  active=0, local=E      ── 已退出且追平                   │
   │                                                                  │
   │  safe = min(E, E-1, E) = E-1                                       │
   │  rec.retire_epoch < E-1 才可 reclaim                              │
   └─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
   ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
   │  Quiet        │──►│  Propagate    │──►│  Reclaim       │
   │  (各核退出)   │   │  (global++    │   │  (本核扫槽:    │
   │  active→0    │   │   传播到所有  │   │   retire<safe  │
   │  local 快照  │   │   active 核)  │   │   → free_func) │
   └───────────────┘   └───────────────┘   └───────────────┘
        ▲                                          │
        └────────── 下次 ebr_enter 重新快照 ◄──────┘
```

关键点：`safe` 取所有 active 核 `local_epoch` 的最小值；只要还有一颗核停留在旧 epoch，retire_epoch ≥ safe 的记录就不能回收。`ebr_exit` 在深度 1→0 时 `global_epoch++`，把「已退出」状态传播给下一次 enter 的快照——这就是 propagate。reclaim 只扫本核槽，因此每颗核必须自己 schedule / exit / retire 来推进自己的回收。

### 4.3 Overflow

槽满：先 reclaim 腾位；仍满 → **leak + 日志**，仍返回 `REND_SUCCESS`。不是「稍后一定 free」。

### 4.4 线程回收不变量

- **禁止**在 `delete_thread` 里、末次 ref 之前调 `thread_release_owned_resources`（二次 drain → dummy 双 put / 死循环）。
- `fini` 必须在 drop `vs` **之前**（compat 可能还读 vs）。
- 空队列 dequeue 已 put dummy；`msq_clean_queue` **禁止**再 put dummy。
- 回收路径 **不**切换当前硬件 AS（只 put ownership）。

### 4.5 与 hazard pointer 对照

EBR 与 hazard pointer（HP）都是无锁回收方案，但代价分布不同：

```text
                       EBR (本篇)                    Hazard Pointer
   ────────────────────────────────────────────────────────────────────
   读侧开销           ebr_enter/exit 一对原子读写     每个 publish 一个指针
                      (per-CPU 计数，热路径极轻)       写全局 HP 数组 (cache 行返流)
   回收时机           批量：epoch 推进后整批 free      个体：HP 清零即可 free
   内存占用           滞留一批节点 (默认 512 槽)       滞留最少 (只差一个 HP 清零)
   跨核协调           依赖 global epoch 间接              依赖 HP 数组直接观测
   适用               高 churn、读远多于写、批回收      回收要快、节点大、稀疏
   本项目选择         MSQ 节点 (churn 大、可批量)        (未用)
```

EBR 的优势在读侧几乎零开销（只读写本核 active/epoch），代价是回收延迟到下一个 quiet + propagate 周期。MSQ 节点 churn 大、可批量回收，正合 EBR 的甜区。HP 在「单节点要尽快回收」更合适，但读侧要 publish/unpublish 两次全局数组访问，热路径更重——本项目未采用。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `kernel/task/ebr.c` / `ebr.h` | enter / exit / retire / reclaim / stats |
| `ms_queue.h` | 读侧 enter / exit |
| `kernel/ipc/message.c` / `ipc.c` | retire 入口（`free_message_ref` / `free_ipc_request`）；request 钉 thread |
| `kernel/task/thread.c` | delete / del_structure / release_owned |
| `kernel/task/task_manager.c` `schedule` | kfree 排水 + `ebr_try_reclaim` |

---

## 6. 流程

### 6.1 EBR 生命周期

```text
ebr_enter          // 0→1：local=global，active
  … 读 MSQ …
ebr_exit           // 1→0：清 active，global++，try_reclaim
ebr_retire_ref     // 记入本核槽（可先 reclaim；满则 leak）
ebr_try_reclaim    // 本核：retire_epoch < safe → free_func
```

### 6.2 `delete_thread` → 结构体释放

```text
status = exit
loop: del_thread_from_manager
  if -E_REND_AGAIN && 本核是 owner → schedule(tm)
ref_put → free_thread_ref → del_thread_structure:
  del_from_manager（防御）
  append_hooks->fini          // vs 仍有效
  thread_release_owned_resources:
    exchange send_pending_msg
    msq_clean_queue(send/recv, free_message_ref)  // 节点进 EBR
    m_free(name), m_free(kstack)
    清 port_ptr / port_cache
    ref_put(vs)               // 只 put
  del_init_parameter
  m_free(thread)
```

创建失败路径可直接 `del_thread_structure`，不经 `delete_thread`。

### 6.3 与 IPC 的钉子

`Ipc_Request` 持有 thread → reclaim `free_ipc_request_real` 才 put thread → TCB 可能活过「业务以为已 delete」的时刻。clean / 观测应用 status / flag，勿抢占地址复用。

---

## 7. 公开 API

本篇涉及的接口分布在：`ebr.h` 全套；线程回收路径上的 `delete_thread` / `free_thread_ref` / `del_thread_structure`（`thread.h`；与 `13` 交叉，本篇钉回收顺序）。说明改写自头文件 Doxygen，并已与 `.c` 核对。

**本篇不涉及：** MSQ inline 算法 → `18`；`kalloc_process_cross_cpu_frees` → kmalloc 篇（与 `ebr_try_reclaim` **并列**于 `schedule`，不是同一种排水）；zombie / `EXIT_REQUESTED` 语义 → `13`。

`thread_release_owned_resources`：**内部 static**；勿在末次 ref 前手调。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序 |
|------|------|
| MSQ 读临界区 | **`ebr_enter`** → 遍历指针 → **`ebr_exit`**（`ms_queue.h` 已包） |
| 节点末次 put | `free_message_ref` / `free_ipc_request` → **`ebr_retire_ref(..., *_real)`** → 稍后本核 reclaim |
| 推进 reclaim | 本核 `ebr_exit`(深度 1→0)、再 `retire`、或任意 **`schedule`**（先排跨核 kfree，再 `ebr_try_reclaim`） |
| 删线程 | **`delete_thread`**：`status=exit` → 摘环（owner 上遇 AGAIN 则 schedule）→ `ref_put` |
| 末次线程 ref | **`free_thread_ref` → `del_thread_structure`**：fini（vs 仍有效）→ release（消息进 EBR；name/kstack 直接 free；只 put vs）→ free TCB |

### 7.2 EBR（`ebr.h`）

```c
void ebr_enter(void);
void ebr_exit(void);
void ebr_try_reclaim(void);
error_t ebr_retire_ref(ref_count_t *ref, error_t (*free_func)(ref_count_t *));
void ebr_dump_stats(void);
```

| 接口 | 说明 |
|------|------|
| `ebr_enter` | 深度 0→1：把 global 快照到 local，置 active。可嵌套。 |
| `ebr_exit` | 深度 1→0：清 active，`global++`，再对本核做 `ebr_try_reclaim`。 |
| `ebr_try_reclaim` | 只扫**本核**槽；`retire_epoch < safe`（**严格小于**）才调 `free_func`。 |
| `ebr_retire_ref` | 记入本核表（默认 512 槽）；前后会尝试 reclaim。满则 **leak，但仍返回 SUCCESS**（防 UAF，use-after-free）。`ref` / `free_func` 为空 → `-E_IN_PARAM`。 |
| `ebr_dump_stats` | 打印每核的 retire / reclaim / overflow 统计。 |

`Thread_Base` **不**走 EBR；但 `Ipc_Request` 可钉住 thread ref，从而间接推迟 `del_thread_structure`。

### 7.3 线程回收（`thread.h`）

```c
error_t delete_thread(Thread_Base *thread);
error_t free_thread_ref(ref_count_t *ref_count_ptr);
void del_thread_structure(Thread_Base *thread);
```

| 接口 | 说明 |
|------|------|
| `delete_thread` | 写 `exit`；循环摘环（owner 上遇 AGAIN 则 `schedule`）；再 `ref_put`。成功路径**不会**在 put 前 drain。 |
| `free_thread_ref` | 末次引用的析构入口 → `del_thread_structure`。 |
| `del_thread_structure` | 见 §7.1 / §6.2。创建失败时可同步直调。name / kstack **直接** `m_free`。 |

禁止：在 `delete_thread` 的末次 ref **之前**再调 release（二次 drain → dummy 双 put / 死循环）。

---

## 8. 多架构

与 ISA 无关；per-CPU 槽随 `NR_CPU`。没有专用硬件原语——依赖软件 epoch 与调度钩子推进。

---

## 9. 测试

间接：高 churn IPC / `smp_test`；`ebr_dump_stats` 看 overflow。

---

## 10. 限制与后续

- 固定槽；overflow 故意 leak。
- reclaim 不跨核扫别人的槽。
- 线程不同步走 EBR，但可被 request 间接推迟。
- 与 kmalloc 跨核 free 并列，勿合并。
- 更大 slot / 水位见编译选项与 evolution（若有）。

---

## 11. 变更记录

- 2026-10-07：§1 概述瘦身——对象分类表与交汇点迁入 §4 开头并与不变量合并。
- 2026-10-05：任务 1/3/5 精读——§1 首次出现 EBR 补全称（Epoch-Based Reclamation，基于世代的回收）、MSQ 补说明（Message Queue，无锁消息队列）、TCB 补全称（Thread Control Block）；§4.2/§4.5/§8 残留的「靠」→「依赖」（前轮漏改的三处）；§7.2 首次出现 UAF 补说明（use-after-free）。
- 2026-10-04：补硬件/架构知识——§4.2 加 EBR 三阶段（quiet / propagate / reclaim）状态图与 `safe = min(active local_epoch)` 推导；§4.5 新增 EBR 与 hazard pointer 对照表（读侧开销 / 回收时机 / 内存占用 / 适用场景）。语言润色：靠→依赖、hold→持有、抢地址复用→抢占地址复用、死转→死循环。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：§5 路径纠正——retire 入口在 `kernel/ipc/message.c` / `ipc.c`（非 `kernel/task/`）。
- 2026-09-26：§7 全文审阅——`ebr.h` / delete·del_structure Doxygen；写清 enter/exit/retire/reclaim 编排、`<` 安全条件、overflow leak、与 kfree 排水并列。
- 2026-09-25：语言整理；强调与跨核 kfree 排水并列、非同一种机制。
- 2026-08-29：整篇重做——拆开「节点 EBR vs 线程同步回收」叙述；`<` 安全条件；overflow leak；schedule 双钩子；request 钉 thread；禁止二次 drain。
- 2026-08-27：初稿。
- 2026-10-05：最终词句顺畅。
