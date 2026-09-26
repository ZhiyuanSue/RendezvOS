# EBR 与线程资源回收

v0.1 · 2026-09-25

本篇覆盖：`kernel/task/ebr.c`、`include/rendezvos/task/ebr.h`、`delete_thread` / `free_thread_ref` / `del_thread_structure` / `thread_release_owned_resources`（`thread.c`），以及与 `schedule` 钩子的交汇。

MSQ 算法与队列内 `ebr_enter` / `exit` 见 `04-IPC/22-无锁队列与EBR设计.md`；消息 / request 何时 put 见 Port 篇；zombie / `EXIT_REQUESTED` 见 TM 篇；`vs` 只 put 不卸硬件见 VSpace 篇；跨核 kfree 排水见 kmalloc 篇（**不是** EBR）。

---

## 1. 概述

这里其实是**两件相关但不同的事**，标题别读成「一切回收都是 EBR」。

| 对象 | 怎么放 | 为何 |
|------|--------|------|
| **MSQ 暴露的节点**（`Message_t` / `Ipc_Request_t`） | last `ref_put` → `ebr_retire_ref` → 等 epoch 安全再 `*_real` `m_free` | 无锁读 `head/tail/next` 时，即使有 refcount，仍有「先看见指针、再被释放」窗口 |
| **`Thread_Base` 本身**（TCB、kstack、name、ownership `vs`） | refcount → `del_thread_structure` **同步**释放 | 线程不靠 EBR；靠「摘环 + 无人 hold」 |

交汇点：

1. 每次 `schedule`：先 `kalloc_process_cross_cpu_frees()`，再 **`ebr_try_reclaim()`**（idle 重的核也要推进）。两钩子并列，不是同一种排水。
2. 线程 teardown 排空 send / recv 时调 `free_message_ref` → **队列节点仍进 EBR**。
3. `Ipc_Request` 持有 `thread` ref；`free_ipc_request_real` 里 put thread → EBR 延迟可**推迟** TCB 真正释放。

---

## 2. 目标与边界

**提供：** minimal EBR（enter / exit、global epoch、固定 retire 表）；线程 teardown 顺序与禁止二次 drain；schedule 推进 reclaim。

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

`ebr_try_reclaim` **只扫本 CPU 槽**——节点 retire 在**调用 `ebr_retire_ref` 的那颗核**上；要靠该核 schedule / exit / 再 retire 推进。

### 4.3 Overflow

槽满：先 reclaim 腾位；仍满 → **leak + 日志**，仍返回 `REND_SUCCESS`。不是「稍后一定 free」。

### 4.4 线程 teardown 不变量

- **禁止**在 `delete_thread` 里、last ref 之前调 `thread_release_owned_resources`（二次 drain → dummy 双 put / 死转）。
- `fini` 必须在 drop `vs` **之前**（compat 可能还读 vs）。
- 空队列 dequeue 已 put dummy；`msq_clean_queue` **禁止**再 put dummy。
- teardown **不**切换当前硬件 AS（只 put ownership）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `ebr.c` / `ebr.h` | enter / exit / retire / reclaim / stats |
| `ms_queue.h` | 读侧 enter / exit |
| `message.c` / `ipc.c` | retire 入口；request 钉 thread |
| `thread.c` | delete / del_structure / release_owned |
| `task_manager.c` `schedule` | kfree 排水 + `ebr_try_reclaim` |

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

`Ipc_Request` hold thread → reclaim `free_ipc_request_real` 才 put thread → TCB 可能活过「业务以为已 delete」的时刻。clean / 观测应用 status / flag，别抢地址复用。

---

## 7. 公开 API

```c
void ebr_enter(void);
void ebr_exit(void);
void ebr_try_reclaim(void);
error_t ebr_retire_ref(ref_count_t *ref, error_t (*free_func)(ref_count_t *));
void ebr_dump_stats(void);

error_t delete_thread(Thread_Base *thread);
error_t free_thread_ref(ref_count_t *ref_count_ptr);
void del_thread_structure(Thread_Base *thread);
/* thread_release_owned_resources：内部；勿在 last ref 前手调 */
```

---

## 8. 多架构

与 ISA 无关；per-CPU 槽随 `NR_CPU`。没有专用硬件原语——靠软件 epoch 与调度钩子推进。

---

## 9. 测试

间接：高 churn IPC / `smp_test`；`ebr_dump_stats` 看 overflow。本篇未复测。

---

## 10. 限制与后续

- 固定槽；overflow 故意 leak。
- reclaim 不跨核扫别人的槽。
- 线程不同步走 EBR，但可被 request 间接推迟。
- 与 kmalloc 跨核 free 并列，勿合并。
- 更大 slot / 水位见编译选项与 evolution（若有）。

---

## 11. 变更记录

- 2026-09-25：语言整理；强调与跨核 kfree 排水并列、非同一种机制。
- 2026-08-29：整篇重做——拆开「节点 EBR vs 线程同步 teardown」叙述；`<` 安全条件；overflow leak；schedule 双钩子；request 钉 thread；禁止二次 drain。
- 2026-08-27：初稿。
