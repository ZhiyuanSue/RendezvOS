# 线程与 Task_Manager

v0.1 · 2026-09-26

本篇覆盖：`kernel/task/thread.c`、`kernel/task/task_manager.c`、`kernel/task/thread_boot.c`（入队 / `init_proc`）、`kernel/task/id.c`、`include/rendezvos/task/thread.h`、`include/rendezvos/task/id.h`；以及 arch 侧 `switch_to` / `context_switch` / `run_thread` 边界。

`init_proc`、boot / idle 与 `cmain` 编排见模块入口篇（`03`）。用户地址空间在调度里怎么换，见所有权篇（`15`）。ELF / `gen_thread_from_*` / `copy_thread` 见创建篇（`14`）。创建时绑核见 `16`。EBR 与 `del_thread_structure` 见 `17`。IPC 阻塞见 `20`。

---

## 1. 概述

core 的调度对象是 **线程 + 地址空间**，没有一等公民「进程」。需要 pid / wait / fd 这些「个性」的一方，自己挂状态，通常放进 `append_thread_info[]`。

每个可调度实体是 `Thread_Base`，挂在本 CPU 的 **`Task_Manager`（per-CPU）** 环形就绪链表上。`schedule()` 持 `sched_lock` 选出下一个 `thread_status_ready`，再在锁外 `switch_to`。默认策略是环形 next-ready（`round_robin_schedule`）：没有时间片字段、没有优先级——这里的「RR」就是绕环找 ready。

调度故意做得很薄。真正要守住的是骨架，而不是「更好的调度算法」：

- **per-CPU TM**：就绪环、current、锁都在本核。跨核 MCS（Mellor-Crummey & Scott 锁，一种可扩展自旋锁）的 `me`、IPC 阻塞归属、`tlb_cpu_mask` 全都假定「线程不会自己跑到别的核上去」。
- **创建时绑核**：入队时选定 CPU；**没有**运行期迁移 API。
- **status 与 exit flag 分开**：IPC 会改 `block_on_*`；退出意图只能用 `THREAD_FLAG_EXIT_REQUESTED`，不能靠换一个 status 位。
- **append hooks**：上层可用 `append_thread_info[]` 挂私有数据；`schedule` 热路径不读这段。
- **`scheduler` 函数指针**：预留换算法；v0.1 **始终**装 `round_robin_schedule`。

这和「单执行流 server + 无锁 IPC」是同一个思路：难处理的并发都收在少数几个原语上；要换策略就挂 hook、换函数指针，而不是让每个 syscall 各自长出一套锁。完整可插拔的调度器是演进项（evolution E4）。

---

## 2. 目标与边界

**提供：** per-CPU 就绪环；`schedule` 状态机与上下文切换钩子；与 IPC 共享的原子 `status`；exit flag / zombie 切走证明；`add` / `del` / `add_thread_to_cpu`；tid；append 生命周期钩子。

**不做：** CFS / 优先级队列；运行期迁移；sleep / wakeup 定时器子系统；在 `schedule` 里把页表强制切回 `root_vspace`；解释某 OS 的 `fork` / `clone` 标志（兼容层在 `copy_thread` 前准备 `VSpace`）。

---

## 3. 分层与调用方

**内核 / server 线程** — `gen_thread_from_func(..., tm, arg)`，或 `create_thread` 再 `add_thread_to_manager` / `add_thread_to_cpu`。线程体经 `thread_entry` 进入 `run_thread`；返回后 OR `EXIT_REQUESTED` 再 `schedule`。

**用户线程** — `gen_thread_from_elf` / `copy_thread`：须 `THREAD_FLAG_USER` + 独立用户 `VSpace`（见创建篇（`14`））。返回用户时会盖掉内核栈上的 `thread_entry` 帧，所以正常情况下回不到入口尾部那套清理。

**boot** — 不走 `create_thread`：`new_thread_structure` 认领当前执行流；`vs == NULL`；栈 = per-CPU boot 栈。之后 BSP 上跑 `kernel_handle_msg`（模块入口篇（`03`））。

**idle** — `gen_thread_from_func(idle_thread, …)`，死循环 `schedule`；给 RR 兜底，并顺带在「很少 malloc」的核上排水（见 §6.2）。

**跨核创建** — 只在**首次入队**选目标 TM（`add_thread_to_cpu`，或 `gen_thread_from_func(..., task_manager_for_cpu(cpu), …)`）。入队后 `thread->tm->owner_cpu` 固定。

调用方须知：`delete_thread` 若目标仍是 current，会在 owner CPU 上自旋 `schedule`；IPC 临时改 `block_on_*`，**不得**用 status 单独表达 exit。

---

## 4. 数据结构：线程与调度器长什么样

定义以 `thread.h` 为准。

§1 已经交代：调度对象是「线程 + 地址空间」，每核一个 `Task_Manager` 管就绪环。本节按这个骨架展开字段——先 `Thread_Base`，再状态机，再 TM。`send_msg_queue` / `recv_msg_queue` 等 IPC 字段嵌在 TCB 上，但**语义以 Port 篇（`19`）为准**，这里只说明「它们住在线程上」。公开 API 见 §7。

### 4.1 `Thread_Base` / `THREAD_COMMON`

一条可调度执行流对应一个 `Thread_Base`。字段大致分几块：身份与调度、地址空间与栈、引用计数、IPC 相关槽位、挂在 TM 环上的链表节，以及可选的结构体末尾可变长区域（给上层挂私有数据）。

```c
#define THERAD_SCHE_COMMON                           \
        struct {                                     \
                struct list_entry sched_thread_list; \
        }; /* 宏名源码少一个 D，文档不改拼写 */

#define THREAD_COMMON                                               \
        char *name;                                                 \
        tid_t tid;                                                  \
        u64 flags;                                                  \
        VSpace *vs;                                                 \
        Task_Manager *tm;                                           \
        u64 status;                                                 \
        u64 kstack_bottom;                                          \
        u64 kstack_num;                                             \
        Arch_Thread_Context ctx;                                    \
        Thread_Init_Para *init_parameter;                           \
        ref_count_t refcount;                                       \
        ms_queue_t recv_msg_queue;                                  \
        ms_queue_t send_msg_queue;                                  \
        volatile Message_t *send_pending_msg;                       \
        atomic64_t recv_pending_cnt;                                \
        volatile void *port_ptr;                                    \
        struct thread_port_cache port_cache;                        \
        THERAD_SCHE_COMMON

struct Thread_Base {
        THREAD_COMMON
        const thread_append_hooks_t *append_hooks;
        u64 append_thread_info[]; /* flexible array；长度见 append_hooks */
};
```

| 字段 | 为什么要有 |
|------|-----------|
| `name` / `tid` | 调试名；全局唯一编号（由 id 管理器发放，用过不回收）。 |
| `flags` | 位标志，与下面的 `status` 分开记。常见：`USER`（用户线程）、`EXIT_REQUESTED`（想退出）、`IPC_PORT_CLOSED`（port 已关）。IPC 会改 status，但**擦不掉**退出意图。 |
| `vs` | 本线程拥有的地址空间；用户线程必须有独立用户地址空间，boot 线程为 NULL。`create_thread` 成功后所有权归线程，调用方不得再放掉这份引用（见 `14` / `15`）。 |
| `tm` | 所属的本核调度器；NULL 表示还没入队或已摘环。跑在哪颗核上 = `tm->owner_cpu`，没有单独的「亲和」字段。 |
| `status` | 调度与 IPC 共用的状态（§4.2）。绕环调度只挑选 `ready`。 |
| `kstack_bottom` / `kstack_num` | 内核栈（高地址为底）和页数；默认两页。 |
| `ctx` | 架构相关的寄存器上下文，切换时保存 / 恢复。 |
| `init_parameter` | 入口函数和整型参数（见 `14`），供 `run_thread` 启动。 |
| `refcount` | 线程对象寿命；IPC 等待请求等可以额外持有。归零后同步拆除（不走 EBR，见 `17`）。 |
| `send_msg_queue` / `recv_msg_queue` / `send_pending_msg` / `recv_pending_cnt` / `port_ptr` / `port_cache` | 每线程消息 MSQ 与 port 查找 cache——**含义见 `19` §4.5**；本篇只说明它们嵌在线程里。MSQ dummy 在 `new_thread_structure` 分配。 |
| `sched_thread_list` | 挂进本核就绪环的链表节。只要还挂在某个 `tm` 上，就应该在环上。 |
| `append_hooks` / `append_thread_info[]` | 可选 append 区给上层；调度热路径不读。何时调用钩子见 §4.5。 |

调用方须守住的约定：

- `add_thread_to_manager` 要求入队前 `status == init`，成功则 CAS 成 `ready`（失败只 `pr_warn`，仍然可能返回 SUCCESS）。
- `create_thread` 成功 ⇒ **caller 不得再** `ref_put` 传入的 `vs`。

### 4.2 status 与 flags

```c
enum thread_status_base {
        thread_status_error = -1,
        thread_status_init = 0,
        thread_status_running,
        thread_status_ready,
        thread_status_zombie,
        thread_status_block_on_send,
        thread_status_block_on_receive,
        thread_status_exit,
};
```

| 状态 | 谁设置 | 调度含义 |
|------|--------|----------|
| `running` / `ready` | `schedule` demote / promote | RR 只选 `ready` |
| `block_on_*` | IPC 路径 | 先改 status 再 `schedule`；demote CAS 失败则保持阻塞 |
| `zombie` | owner CPU 在切走带 `EXIT_REQUESTED` 的 ready 线程时 | 还可以挂在环上；等 clean 那边来看 |
| `exit` | `delete_thread` 入口 | **不是** zombie；拆除结构前的标记 |

状态机示意（箭头上的标签是触发方）：

```text
  init
    │  create_thread
    ▼
  ready ◄──────────────────┐
    │  schedule promote    │ schedule demote
    │  (RR 选中)           │
    ▼                      │
  running ─────────────────┘
    │
    ├─ IPC block_on_* (CAS 成功)
    │       │
    │       ▼
    │   block_on_*
    │       │  schedule（仍阻塞则回到 ready 再等）
    │       └──────────► ready
    │
    └─ owner CPU 切走带 EXIT_REQUESTED 的 ready
            │
            ▼
         zombie
            │  delete_thread → status=exit
            ▼
          exit
            （del_thread_structure）
```

`block_on_*` ↔ `ready` 的 CAS 由 IPC 路径与 `schedule` 共用同一原子位；`zombie` 还可以挂在环上，等 clean / EBR 那边来看（见 `17`）。

三条退出路径不可混淆：

1. **自愿结束（core）：** OR `EXIT_REQUESTED`，再 `schedule`；切走且仍为 `ready` 时变成 **zombie**。
2. **`delete_thread`：** 直接写 `status = exit`，再 `del` + `ref_put`。
3. **兼容层 / clean：** 可强制 zombie；细节见 EXIT / clean 协议，不在本篇展开。

`THREAD_FLAG_EXIT_REQUESTED` = 意图；`zombie` = owner CPU「已切走」证明。IPC 改 status **擦不掉** flag。

默认 `flags = THREAD_FLAG_NONE` 就是内核线程；用户线程**不**是自己显式给 `THREAD_FLAG_USER`，而是走 `thread_enable_user` / `thread_disable_user`（§7.4）：前者先 `fp_alloc` 分配浮点区，成功才 OR 上 `USER`——失败时 flags 原样不动并返回错误码，不会出现「标了用户却没有 FP 区」的半吊子状态。`USER` **不能**配 `&root_vspace`（`schedule` 会按配错处理）。

### 4.3 Task_Manager

每核一个调度器对象：管本核「当前跑谁、环上还有谁 ready」。跨核创建只在**首次入队**时选目标 TM，之后 `owner_cpu` 固定。

```c
#define TASK_MANAGER_SCHE_COMMON                     \
        struct {                                     \
                cas_lock_t sched_lock;               \
                struct list_entry sched_thread_list; \
        };

struct task_manager {
        TASK_MANAGER_SCHE_COMMON
        cpu_id_t owner_cpu;
        Thread_Base *current_thread;
        Thread_Base *(*scheduler)(Task_Manager *tm);
};
```

| 字段 | 为什么要有 |
|------|-----------|
| `sched_lock` | 极短临界区保护环操作（CAS 锁，见 `31`）。 |
| `sched_thread_list` | 双向环的哨兵节点；线程的 `sched_thread_list` 挂在这上面。 |
| `owner_cpu` | 这颗 TM 属于哪颗逻辑 CPU；也是该核上线程的亲和。 |
| `current_thread` | 本核正在跑的线程；RR 从它的 `next` 开始找下一个 `ready`。 |
| `scheduler` | 选下一个线程的函数指针；现行只装 round-robin。 |

每核一个 TM，就绪环是双向链表 + 哨兵节点：
```text
  per-CPU Task_Manager (owner_cpu = c)
  │  sched_lock
  │  sched_thread_list（哨兵）
  │  current_thread
  ▼
  Thread_A (running)
  │
  │  就绪环（双向，从 current 的 next 起找）
  ▼
  哨兵
  │
  ▼
  Thread_B (ready)
  │
  ▼
  Thread_C (ready)
  │
  └──► 回到哨兵

  RR：从 current->next 绕环找下一个 ready
  环上全无 ready → 死循环（靠 idle 兜底）
```

- `DEFINE_PER_CPU(Task_Manager*, core_tm)` — **始终** `percpu(core_tm)` / `per_cpu(core_tm, cpu)`；头文件 `extern Task_Manager* core_tm` 是模板声明，不是全局单例。
- `choose_schedule` 只装 RR，并顺带 `is_print_sche_info = false`。
- `new_task_manager`：alloc 之后立刻 `choose_schedule`（alloc 失败没有判空——已知缺口）。

### 4.4 tid

全局 `Id_Manager tid_manager`；`get_new_id` 用 MCS，`me = &percpu(id_spin_lock)`。从 0 单调递增，到 `INVALID_ID`（`U64_MAX`）后不再自增。**不回收**。

MCS 节点是 `DEFINE_PER_CPU(..., id_spin_lock)`（`id.c`）。`init_core_id_system()` 没有参数，只把 `tid_manager` 的 `id` 置 0、锁指针置空；真正加锁发生在后来的 `get_new_id`。它不枚举 CPU。`cmain` 在 `arch_start_core` 之后、`global_port_init` 和 `init_proc` 之前调用，这样后面建线程时管理器已经就绪。

### 4.5 append hooks

上层若要在线程上挂私有数据，又不想改 core 固定布局，创建时按 `append_info_len` 多分配（`append_thread_info[]`），再用一张静态 `thread_append_hooks_t` 说明何时 `init` / `copy` / `fini`。

| Hook | 何时调用 | 干什么 |
|------|----------|--------|
| `init` | **仅** `run_elf_program`（带着 `elf_info`）；普通 `create_thread` **不**调 | 首次填写 append 区（例如记 ELF 加载结果） |
| `copy` | `copy_thread` 成功路径末尾 | 从源线程 append 区抄/重建到新线程 |
| `fini` | `del_thread_structure`，在释放 vs / 堆之前（此时 vs 仍有效） | 释放 append 区持有的资源 |

成员可为 NULL。`schedule` 热路径不读 append 区。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `thread.h` | 类型、flags、公开 API、所有权注释 |
| `thread.c` | 分配、`create_thread`、`delete_thread`、`copy_thread`、port cache |
| `thread_boot.c` | `add` / `del`、`init_proc`、boot / idle、`kernel_handle_msg` |
| `task_manager.c` | `schedule`、RR、`new_task_manager` |
| `id.c` | tid |
| `arch/*/task/arch_thread.c`、`arch_switch.S`、`arch_run_thread.S` | `switch_to`、首次 ctx、`run_thread` |
| `fp.c`（`task/fp.h`） | FP 区跨架构生命周期：`fp_alloc` / `fp_free`、`fp_state_save` / `fp_state_restore`、fork 拷贝 `fp_copy_ctx` |
| `arch/x86_64/task/arch_fp.c`；aarch64 `arch_switch.S` 的 FP 段 | `arch_fp_save_to` / `arch_fp_load_from` / `arch_fp_init_area`（`fxsave64`·`fxrstor64` / `stp·ldp q`） |

---

## 6. 流程

### 6.1 入队与首次运行

调用方先 `create_thread`（装 `thread_entry`、可选预留 trap frame），再 `add_thread_to_manager` / `add_thread_to_cpu`（把 status 从 `init` 变成 `ready`）；某次 `schedule` RR 选中后经 `switch_to` 进入 `thread_entry`，再跑 `run_thread(func)`。

用户 `copy_thread` 返回的线程**未入队**；入口是 `run_copied_thread`，再经 `arch_return_to_user` 回用户。

`add_thread_to_manager`：任一指针为 NULL 则**返回 SUCCESS 且不链接**（静默）。已有 `tm` 则返回 `-E_RENDEZVOS`。

`add_thread_to_cpu`：仅允许 `tm==NULL` 且 `status==init`（创建期首次）。

`del_thread_from_manager`：若仍是 current 则返回 `-E_REND_AGAIN`；无 tm 幂等 SUCCESS。

### 6.2 `schedule` 主路径

```text
kalloc_process_cross_cpu_frees()   ← 排水，不是调度算法
ebr_try_reclaim()                  ← 同上
lock sched_lock
  curr: running → ready（已是 block_* 则 CAS 失败，保持阻塞）
  next = scheduler(tm)             ← RR
  若 next 空或 == curr → 恢复 running，解锁，不 switch
  若 next 带 USER → 可能换用户 AS（细节见所有权篇（`15`））
  若 curr 有 EXIT_REQUESTED 且仍 ready → zombie
  next → running
unlock
switch_to(curr → next)
```

本篇只划清边界：仅当**切入**线程带 `THREAD_FLAG_USER` 才尝试换用户 AS；切入内核 / idle **不**强制回 `root_vspace`，所以可能继续跑在上一个用户的页表上（mask / extra ref 的滞后清理，所有权篇（`15`）负责）。

谁调 `schedule`：idle 循环；IPC 阻塞之后；`delete_thread` 重试；从用户态进来的 trap / IRQ 收尾（内核态的 trap **不**自动让出）。

### 6.3 round_robin_schedule

从 `current->sched_thread_list.next` 绕环，跳过哨兵与非 `ready`。若选中自己，上层走 `use_old_thread`。

环上**一个 ready 都没有**时，那个 while 会**死循环**——所以靠 idle 一直留着一个 ready。没有时间片。

### 6.4 delete_thread

先写 `status = exit`，再循环 `del_thread_from_manager`；若得到 `-E_REND_AGAIN` 且本核是 owner，则 `schedule` 后再试，最后 `ref_put`，可能落到 `del_thread_structure`。

### 6.5 `init_proc`（boot ↔ idle）

1. `new_task_manager` 之后建 boot（认领执行流，`running`），再建 idle（入队）
2. 人为：`current = idle`（running），boot = ready
3. `switch_to(boot, idle)`：保存 boot 现场，跳进 idle；之后 idle `schedule` 可切回 boot，`init_proc` 在 **boot** 上返回

boot **没有** `arch_set_new_thread_ctx`；首次保存靠这次 `switch_to`。`init_proc` 自己没有参数。boot 的栈也不是这里分配的：`kstack_bottom` 取 `percpu(boot_stack_bottom)`，该值由前面的 `virt_mm_init` 写成链接栈（BSP）或 `ap_boot_stack_ptr`（AP）。

### 6.6 arch 切换：软件边界与硬件寄存器

`schedule` 解锁之后才调 `switch_to`。C 层先换段表、GS、用户栈指针这类「这颗核现在代表哪个线程」的状态，再进汇编 `context_switch`，保存/恢复一组通用寄存器和内核栈指针。页表根（CR3 / TTBR0）**不在**这里换——那是上面 USER 分支里 `arch_set_current_user_vspace_root_asid` 的事（见所有权篇（`15`）/ ASID 篇（`12`））。

**这次切换存什么、不存什么。** 调用约定里，有些通用寄存器由「被调用的函数」负责保存。`context_switch` 只存这一组，外加内核栈指针：x86 是 `r15`–`r12`、`rbp`、`rbx` 和 `rsp`；aarch64 是 `x19`–`x30`、`sp` 和 `SPSR`。

浮点 / 向量是**另一套**硬件状态（见下节「浮点与向量」）。本仓库在 `switch_to` 里对它们做 **eager** 成对存取：切走前写入 `Arch_Thread_Context`，切入前装回；`arch_ctx_refresh` 在 syscall 路径 fork 前再快照一次（与 user SP/TLS 同类）。不做每个 trap 全量存，也不做懒 `#NM` / FPEN trap。内核 C 仍用 `-mno-sse` / `-mgeneral-regs-only`，热路径不踩用户 FP。

#### 浮点与向量：硬件背景与本仓库约定

下面补的是「这些寄存器从哪来、为什么切换要存、我们存到哪一层」——读实现（`thread_arch.h`、`arch_fp.c`、`arch_thread.c`、`arch_switch.S`、`enable_fp`）之前建议先扫一遍。

##### 为什么和 GPR 分开谈

通用寄存器（x86 的 `rax`…、aarch64 的 `x0`…）由 ABI 分「调用者保存 / 被调用者保存」；内核 `context_switch` 只存 callee-saved 那一组就够，因为内核 C 遵守同一套调用约定。

浮点 / 向量寄存器**不属于**那套 callee-saved 约定在内核里的用法：用户态可以随便用它们算 `float`/`double`、或跑 SIMD；内核又用编译选项禁止自己碰它们。于是「上一线程留下的值」不会被内核自然冲掉，也**不会**被 `context_switch` 自动换掉——必须在线程边界显式存取，否则线程切回来时，浮点寄存器里是别的线程留下的值，算出来的数就错了。

##### 跨架构共享层：`fp.h` / `fp.c`

`ctx->fp` 的申请与存取**没有**两套 arch 实现：`include/rendezvos/task/fp.h` + `kernel/task/fp.c` 是唯一的生命周期，也是「硬件 ↔ 快照」的唯一通路。arch 只需提供三个函数（`arch_fp_save_to` / `arch_fp_load_from` / `arch_fp_init_area`）和三个宏（`ARCH_FP_SIZE`、`ARCH_FP_ALIGN`、`ARCH_FP_BUF_SIZE` = SIZE+ALIGN），其余两边共用。

| 接口 | 语义 |
|------|------|
| `fp_area(buf)` | `ROUND_UP(buf, ARCH_FP_ALIGN)`——从堆上拿 `ARCH_FP_BUF_SIZE` 后取其中的对齐窗口；arch 层只见窗口头，永远碰不到 malloc 裸返回 |
| `fp_alloc(ctx)` | **幂等**：`ctx->fp` 非空直接返回成功；否则从本核 `percpu(kallocator)` `m_alloc(ARCH_FP_BUF_SIZE)`（分配器清零），随后 `arch_fp_init_area` 写入新用户线程的初始值 |
| `fp_free(ctx)` | NULL 安全；`m_free` 后置 `ctx->fp = NULL` |
| `fp_state_save` / `fp_state_restore` | 薄封装：`fp_area(buf)` 后转 `arch_fp_save_to` / `arch_fp_load_from`；调用者是 `switch_to` 与 `arch_ctx_refresh` |
| `fp_copy_ctx(dst, src)` | 按窗口大小拷 `ARCH_FP_SIZE`，**不分配**；两边**都有** fp 才动，否则空操作——fork 路径的 `arch_ctx_merge_from_src` 靠它 |
| `fp_state_size()` | 返回 `ARCH_FP_BUF_SIZE`；当前无调用者，预留给以后信号帧 / ptrace 问「要多少空间」 |

初始值由 `arch_fp_init_area` 决定，这也是共享层「统一表象」下唯一的实质不对称：x86 往窗口偏移 0 写 `FCW_DEFAULT`、偏移 24 写 `MXCSR_DEFAULT`——两类控制字都把六种浮点异常全部 mask（默认不因此触发 #MF/#XM）、舍入取就近，两个偏移正是 FXSAVE 帧里 FCW / MXCSR 的位置；aarch64 目前是 **no-op**——分配器已清零，FPCR/FPSR 全零即可用。

##### x86：三代叠三代（x87 → SSE/XMM → AVX → AVX-512）

| 代际 | 寄存器（用户可见） | 控制 / 状态 | 典型用途 |
|------|-------------------|-------------|----------|
| **x87** | `ST0`–`ST7`（80 位浮点栈） | FCW / FSW / FTW 等 | 很老的标量浮点；今天 x86-64 上 `long double` 仍可能走这里 |
| **SSE / SSE2** | **`XMM0`–`XMM15`**（各 **128 bit**） | **MXCSR**（舍入、异常掩码、DAZ/FTZ 等） | **x86-64 ABI 基线**：`float`/`double` 参数与返回值走 XMM；标量浮点与 128 位 SIMD |
| **AVX / AVX2** | **`YMM0`–`YMM15`**（各 **256 bit**） | 仍用 MXCSR；另有 `VZEROUPPER` 等惯例 | 更宽的向量；**YMM 的低 128 位就是原来的 XMM** |
| **AVX-512** | **`ZMM0`–`ZMM31`**（各 **512 bit**）+ 掩码 `k0`–`k7` 等 | 更大的状态区 | HPC / 部分发行版优化的 `memcpy` 等；YMM/XMM 又是 ZMM 的低位 |

直觉图（同一编号寄存器在变宽）：

```text
  AVX-512:  ZMM0  [ 512 bit ──────────────────────────── ]
  AVX:      YMM0  [ 256 bit ────────── ]  （高半是 AVX 新增）
  SSE:      XMM0  [ 128 bit ── ]          （再低半，也是 YMM/ZMM 的最低 128）
```

**和 ABI / 编译器的关系（基线）：**

- System V x86-64 **要求** CPU 具备 **SSE2**；`float`/`double` 用 XMM，不是用 x87 栈传参。
- 默认 `-march=x86-64` 只保证生成到 SSE2；**不会**自动生成 AVX，除非 `-march=x86-64-v3` / `native` 或手写 intrinsic。
- **glibc** 常在运行时用 IFUNC，按 CPUID **和** OS 是否在 **`XCR0`** 里打开了对应位，给 `memcpy`/`strcpy` 等选 AVX / AVX-512 实现。OS **不开** AVX 位 → 解析器退回 **SSE2** 路径。

**整包保存指令：FXSAVE vs XSAVE**

| 机制 | 覆盖什么 | 大小 / 对齐 | 本仓库 |
|------|----------|-------------|--------|
| **`FXSAVE` / `FXRSTOR`**（64 位形式 `fxsave64`） | x87 状态 + MXCSR + **XMM0–15** | **固定 512 字节**，至少 16 字节对齐 | CPU **无** XSAVE 时的回退路径 |
| **`XSAVE` / `XRSTOR`**（64 位形式 `xsave64` / `xrstor64`） | 由 **`XCR0`** + 指令 **RFBM** 选择组件（可含 x87、SSE、**AVX**、AVX-512、…） | 大小用 CPUID leaf `0xD`（在**当前 XCR0** 下）查询；对齐常见 **64 字节** | CPU 有 XSAVE 时使用；最多开到 **AVX**，**不开** AVX-512 |

**`xsave64` 是什么（读 `arch_fp_save_to` 前先看这段）**

`XSAVE` 是「按组件位图把扩展状态整包写入内存」的指令族；长模式下 GAS/内联汇编里常见助记符是 **`xsave64`**（操作数是 64 位地址形式）。配对恢复是 **`xrstor64`**。相对 `fxsave64` 的关键差别：

1. **存哪些组件不写死在指令里**，而由两层掩码共同决定：
   - **`XCR0`（XFEATURE 使能掩码）**：OS 在启动时写入，声明「本 OS 会管理哪些扩展状态」。用户态 / glibc 也靠读 XCR0 判断 OS 是否支持 AVX 等（IFUNC）。bit0=x87，bit1=SSE，bit2=AVX，bit5–7 等为 AVX-512 相关。
   - **RFBM（Requested Feature Bit Map）**：每次执行 `xsave`/`xrstor` 时放在 **`EDX:EAX`**（高:低）里的位图，表示**这一次**要存/取哪些组件。有效参与集合是 **`RFBM ∩ XCR0`**（再叠加指令变体自己的规则；我们用的是基础 `XSAVE`，不用 `XSAVEOPT`/`XSAVES`）。位布局与 CPUID leaf `0xD` 输出的 **`EDX:EAX`（supported）** 相同，但是**另一场合**：前者是指令操作数，后者是能力查询。
2. **CPUID leaf `0xD` subleaf 0**（在谈 size 时必读）：
   - **`EDX:EAX`**：CPU 允许写入 XCR0 的组件位图（supported）
   - **`EBX`**：**当前 XCR0 已启用** 组件所需的 XSAVE 区字节数
   - **`ECX`**：若把所有 supported 位都打开，所需的最大字节数（本仓库未用）
3. **内存布局**：前 **512 字节 legacy 区**与 FXSAVE **同布局**（FCW/MXCSR/ST/XMM…）；其后是 **XSAVE header**，再往后是各扩展组件（例如 AVX 的 YMM 高 128 位）。总长度随 XCR0 变化，故不能写死 512；**须先写好 XCR0，再读 EBX**。
4. **对齐**：XSAVE 区通常要求 **64 字节**对齐（FXSAVE 只需 16）。本仓库 `ARCH_FP_ALIGN=64`，`fp_area()` 在堆缓冲里取对齐窗口。
5. **使能顺序**：须先 `CR4.OSXSAVE=1`，再写 `XCR0`，再执行 `xsave64`；否则 `#UD`。

本仓库用法（`arch/x86_64/task/arch_fp.c`，已审定）：

- 静态 **`fp_cfg`**：`use_xsave` + `xcr0`；后者开机后不变，并**兼作**每次 save/restore 的 RFBM（简单策略：RFBM == 已启用的 XCR0 子集）。
- **`ARCH_FP_XCR0_ALLOWED`** = `X87|SSE|AVX`；**`ARCH_FP_XCR0_DENIED`** = AVX-512 三件套（OPMASK / ZMM_HI256 / HI16_ZMM）。扩展点是改允许集，不是再开第三条启动路径。
- **`arch_fp_cpu_init`**（每核，内联两段）：SSE 基线 CR → detect（CPUID ∩ ALLOWED → `fp_cfg`）→ enable（写 XCR0、读 EBX 定 `fp_payload_size`/`fp_buf_size`；FXSAVE 保持静态默认 512）→ 一行 `pr_info` 打印最终参数。
- **`arch_fp_save_to` / `load_from`**：`xsave64`/`xrstor64`（`"a"`/`"d"` = RFBM 低/高 32 位）或回退 `fxsave64`/`fxrstor64`。

Intel 手册：SDM Vol.2 `XSAVE` / `XRSTOR`；Vol.1 扩展状态管理与 XCR0。

**512 字节不是「XMM 加起来」：** `XMM0`–`XMM15` 是 **16** 个寄存器（下标 0…15），每个 128 bit = 16 B，合计 **256 B**。FXSAVE（以及 XSAVE 的 legacy 区）里还有 x87 的 ST/MM 槽、MXCSR 和控制头，再留 reserved，凑成固定 **512 B**。内核把整块当黑盒，一般不解析字段；布局如下（Intel SDM Vol.1 / Vol.2 `FXSAVE`）：

```text
偏移    大小     内容
0       2        FCW（x87 控制字）
2       2        FSW
4       1        FTW（FXSAVE 压缩后的 tag）
5       1        保留
6       2        FOP
8       8        上次 x87 IP（或 CS:IP 相关字段，视模式）
16      8        上次 x87 DP
24      4        MXCSR
28      4        MXCSR_MASK
32      128      ST0–ST7 / MM0–MM7：8 × 16 B
                 （每槽放 80-bit 扩展精度 + 填充到 16 B；与 MMX 别名同一物理寄存器）
160     256      XMM0–XMM15：16 × 16 B
416     96       保留（凑满 512）
```

因此：`fxsave64`（以及 `xsave64` 的 legacy 前半）**已经把 ST0–ST7 写进偏移 32 起的那 128 字节**；不是「只存了 XMM、x87 丢了」。整块状态区整包进出；有 AVX 时 XSAVE 在 512B 之后再跟 YMM 高半等扩展区。

`CR4.OSFXSR`：允许用 FXSAVE/SSE。`CR4.OSXSAVE` + `XCR0`：允许 XSAVE，并声明「OS 会保存哪些扩展」。**只开硬件、不在切换里存对应组件 = 状态丢失。**

**本仓库 x86 约定（E13 已关闭的那一档）：**

1. `arch_fp_cpu_init`（每核）：如上；有 XSAVE 则最多 **AVX**，**不开** AVX-512；否则 FXSAVE。`arch_fp_state_size()` / `arch_fp_payload_size()` 为运行时值（全局，各核写入同一结果）。
2. **仅 user 线程**拥有 FP：`ctx->fp` 堆缓冲长度为 `arch_fp_state_size()`（payload+`ARCH_FP_ALIGN`，ALIGN=64）；`fp_area()` 取对齐窗口。创建用 `thread_enable_user`（→ `fp_alloc`）。kernel `fp == NULL`。fork：先 enable，再 merge **只拷贝**；teardown 走 `thread_disable_user`。
3. **不做** AVX-512 / AMX；有 AVX 的 CPU 上 XCR0/保存范围与 glibc IFUNC 一致。

##### aarch64：一套 FPSIMD（相对整齐）

ARM 没有「x87 / SSE / AVX」这种叠名史；EL0/EL1 上的标量浮点与 **AdvSIMD（NEON）** 共用同一组宽寄存器：

| 名称 | 含义 |
|------|------|
| **`V0`–`V31`** | 32 个向量寄存器；汇编里按宽度写成 `S`（32）、`D`（64）、**`Q`（128）** 等——**同一寄存器的不同视图** |
| **`FPCR` / `FPSR`** | 浮点控制与状态（舍入、异常标志等），类似 x86 的 MXCSR + 部分 x87 控制语义 |

启动时 `CPACR_EL1.FPEN`（`boot.S` 的 `enable_fp`）打开访问；否则执行浮点/向量会陷入。

**SVE（Scalable Vector Extension）** 是另一套**可变长度**向量（另有 `ZEN` 等控制），状态更大、布局依赖向量长度。本仓库 **不启用、不保存** SVE；保持陷阱即可。

**本仓库 aarch64 约定：**

1. `Arch_Thread_Context::fp`：user 线程堆上 **`ARCH_FP_BUF_SIZE`**（payload `Q0`–`Q31` + `FPCR`/`FPSR`，ALIGN=16）；kernel 为 `NULL`。对齐窗口用 common 的 `fp_area()`，与 x86 同公式。
2. `arch_switch.S` 的 `arch_fp_save_to` / `arch_fp_load_from` 用 `stp`/`ldp q` + `mrs`/`msr`；`switch_to` 经 `fp_state_save`/`restore`（内部 `fp_area`）仅在 `fp != NULL` 时调用。
3. 生命周期与 x86 相同（`thread_enable_user` / `thread_disable_user` → `fp_alloc` / `fp_free`）；`arch_ctx_merge_from_src` 保留 dst 自己的 `fp` 指针，`fp_copy_ctx` 按窗口拷贝内容（不申请）。

##### 两边对照（实现时记住什么）

| | x86_64（当前） | aarch64（当前） |
|--|----------------|-----------------|
| 用户可见 FP/SIMD | x87 + XMM + MXCSR | Q0–31 + FPCR/FPSR |
| 存盘 | FXSAVE 或 XSAVE（±AVX，`arch_fp_*`） | 汇编存 Q + FPCR/FPSR |
| ctx / size | `arch_fp_state_size()` 运行时；ALIGN=64 | 固定 `ARCH_FP_*`；ALIGN=16 |
| 启动时使能 | `arch_fp_cpu_init` | `enable_fp`（FPEN）；`arch_fp_cpu_init` 空 |
| 明确不做 | AVX-512 | SVE |
| 内核编译 | `-mno-sse` 等 | `-mgeneral-regs-only` |

#### 线程栈布局

用户线程有 kstack + ustack，内核线程只有 kstack：

```text
   高地址
   ┌───────────────────────┐  USER_SPACE_TOP
   │   user stack (R/W)    │  thread_ustack_page_num = 8 页 = 32 KiB
   │                       │  generate_user_stack 映射；SP 初值 = 高地址 − 8
   ├───────────────────────┤
   │   (guard / gap)        │
   ├───────────────────────┤  KSTACK_TOP
   │   kernel stack (R/W)   │  thread_kstack_page_num = 2 页 = 8 KiB
   │                       │
   │   ┌─ trap frame ────┐ │  reserve_trap_frame=true 时栈顶预留
   │   │ user PC/SP/...  │ │  x86_64: rcx(rip)/rsp/rflags/rax...
   │   │                 │ │  aarch64: ELR/SP/x0/SPSR...
   │   └─────────────────┘ │
   │   通用寄存器保存区   │  context_switch 保存 r15–r12/rbp/rbx (x86_64)
   │                       │  或 x19–x30/sp/SPSR (aarch64)
   └───────────────────────┘  kstack_bottom（高地址）
```

#### x86_64（Intel SDM：TSS、SYSCALL / SWAPGS、FS/GS base）

`arch/x86_64/task/arch_thread.c` 的 `switch_to`：

1. **TSS.RSP0**：x86_64 的 TSS（Task State Segment，见 Intel SDM Vol.3 §7.2）在 64 位模式下不再承担「硬件任务切换」，但仍是 ring 3→0 栈切换的来源——CPU 在 `SYSCALL` / 中断 / 异常从 CPL=3 进入 CPL=0 时，把 RSP 替换为 `TSS.RSP0`。本核每切一个线程，就把 `TSS.RSP0` 改成新线程的内核栈底，好让下一次 ring 3→0 落到正确的内核栈上。旧线程的 RSP0 则读进 `old_context->stack_bottom`，这样下次切回它时能还原。
2. **`MSR_KERNEL_GS_BASE`（0xC0000102）与 `MSR_FS_BASE`（0xC0000100）**：x86_64 的 SWAPGS 约定下，内核入口 `SWAPGS` 把 `IA32_GS_BASE` 与 `IA32_KERNEL_GS_BASE` 互换，使内核侧 GS 指向 per-CPU 数据，而 `KERNEL_GS_BASE` 保存用户 GS 基址；`FS_BASE` 在用户态作 TLS 段基址。切换线程时成对读写这两个 MSR，避免下一线程继承上一线程的 TLS / per-CPU GS。
3. **`user_rsp_scratch`（per-CPU）**：syscall 入口把用户 RSP 暂存在这里；切换时一并换掉，与 trap frame 里的用户返回现场对齐。
4. **FP 状态**：`fp_state_save(old->fp)` / `fp_state_restore(new->fp)`（内部 `arch_fp_save_to`/`load_from` → `fp_area`；x86 为 `xsave64`/`xrstor64` 或回退 `fxsave64`/`fxrstor64`）；见上节「浮点与向量」。
5. 然后 `context_switch`（`arch_switch.S`）：`pushf` + 保存 `rsp/r15–r12/rbp/rbx`，加载新线程同组寄存器，`popf` 后 `ret`——返回地址在新栈上，等于跳进新线程上次离开的地方。

首次进用户见创建篇（`14`）的 Path A / B；本篇只负责「内核态线程之间」换栈、TLS 与 FP。

#### aarch64（ARM ARM：SP_EL0、TPIDR_EL0、DAIF、SPSR_EL1）

`arch/aarch64/task/arch_thread.c` 的 `switch_to`：

1. **`TPIDR_EL0`**：用户 TLS；先 mrs 旧、msr 新。
2. **`SP_EL0`**：EL0 栈指针；trap 入口约定下与用户 SP 绑定（`tf->SP` 另存内核 trap save-area）。
3. **`DAIF`**：保存旧线程的中断屏蔽状态；`context_switch` **返回到旧线程之后**再 `msr DAIF` + `isb`——因为「从 switch 返回」意味着又回到了 old 上下文，要恢复它离开时的屏蔽位，而不是新线程的。
4. **FP**：`fp_state_save` / `fp_state_restore`（底层 `arch_fp_save_to` / `load_from` ↔ `fp_area`）；见上节「浮点与向量」。
5. `context_switch`（`arch_switch.S`）：保存 `x19`–`x30`、`sp`、`SPSR_EL1`，再加载新线程对应槽；`ret` 经新栈上的 LR 继续。

用户线程创建时 `reserve_trap_frame=true`（栈顶预留 trap frame）；内核线程 `false`。aarch64 预留后强制 16B 对齐。

`switch_to` 时序（x86_64 为例；aarch64 类似，仅寄存器名不同）：

```text
   schedule()                     switch_to(old, new)            context_switch (asm)
   ────────────┬──────────────────────┬───────────────────────────┬──────────────►
               │ lock sched_lock      │                           │
               │ curr: running→ready │ TSS.RSP0 = new_kstack     │
               │ next = RR pick       │ KERNEL_GS_BASE / FS_BASE  │
               │ next→running         │ user_rsp_scratch = new    │
               │ unlock               │ FP save/restore (xsave/fxsave) │ pushf; push rsp
               │                      │                           │ push r15..r12
               │                      │                           │ push rbp; push rbx
               │                      │                           │ load new ctx
               │                      │                           │ pop rbx; pop rbp
               │                      │                           │ pop r12..r15
               │                      │                           │ pop rsp; popf
               │                      │                           │ ret  ──► 跳到新线程上次离开处
```

页表根（CR3 / TTBR0）**不**在此图里——它由 USER 分支里的 `arch_set_current_user_vspace_root_asid` 负责，见 `15`。

---

## 7. 公开 API

接口分布在：`thread.h` / `id.h` 上的调度与 TCB / 入队 / tid，以及 arch `thread_arch.h` 的 `switch_to` / `context_switch`。说明改写自头文件 Doxygen，并已与 `.c` / `.S` 核对。

**本篇不涉及（只链过去）：** `gen_thread_from_*` / ELF 加载见 `14`；`kernel_port_*` 见 `03`；亲和性深挖见 `16`；EBR 槽与 `ebr_*` 见 `17`（`delete_thread` / `del_thread_structure` 的生命周期侧仍在本篇列出）。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序 |
|------|------|
| 每核任务子系统起来 | 先 `init_core_id_system`（`cmain`，早于 `init_proc`），再 **`init_proc`**，……之后才可 `create` / `schedule` |
| `init_proc` 内部 | `new_task_manager`，再建 boot、idle，再 `switch_to(boot, idle)`；返回时已在 **boot** |
| 普通内核线程 | `create_thread`（或 `gen_thread_from_func`），再 `add_*`；某次 **`schedule`** 后经 `switch_to` 进入 `thread_entry`，再跑 `run_thread` |
| 退出 | OR `EXIT_REQUESTED`（或 `delete_thread` 写 `exit`），在 owner 上 `schedule` 切走，再经 zombie / 摘环，最后 `ref_put` |

`schedule` 内部七步见 `thread.h` `@note` 与 §6.2。

### 7.2 调度与 Task_Manager

```c
void schedule(Task_Manager *tm);
Thread_Base *round_robin_schedule(Task_Manager *tm);
void choose_schedule(Task_Manager *tm);
Task_Manager *new_task_manager(void);
void del_task_manager_structure(Task_Manager *tm);
Task_Manager *init_proc(void);
```

| 接口 | 说明 |
|------|------|
| `schedule` | 先排空跨核 kalloc free，再 `ebr_try_reclaim`；持 `sched_lock` 选出 next；仅切入 USER 时换用户页表根；解锁后 `switch_to`。`tm==NULL` 直接返回。切入内核线程时**不会**强制切回 `root_vspace`。 |
| `round_robin_schedule` | 从 `current` 起沿环找下一个 `thread_status_ready`；环上全无 ready 则死循环（靠 idle 兜底）。`tm` 或 current 为 NULL 时返回 NULL。 |
| `choose_schedule` | 挂上 RR，并关掉 `is_print_sche_info`。v0.1 **只走**这条路径。 |
| `new_task_manager` | 分配后挂 RR、初始化 CAS 锁与空环，并把 `owner_cpu` 设为本核。**不**检查 alloc 是否为 NULL（已知缺口）。 |
| `del_task_manager_structure` | 只 `m_free` 结构体本身；调用方须先拆解环上的线程等资源。 |
| `init_proc` | 见 §7.1 / §6.5；失败返回 NULL，并做部分回滚。 |

### 7.3 TCB 生命周期与入队

```c
Thread_Base *new_thread_structure(struct allocator *, const thread_append_hooks_t *);
error_t free_thread_ref(ref_count_t *);
void del_thread_structure(Thread_Base *);
Thread_Init_Para *new_init_parameter_structure(void);
void del_init_parameter_structure(Thread_Init_Para *);
Thread_Base *create_thread(void *__func, const thread_append_hooks_t *,
                           VSpace *vs, bool reserve_trap_frame,
                           int nr_parameter, ...);
error_t delete_thread(Thread_Base *);
error_t add_thread_to_manager(Task_Manager *, Thread_Base *);
error_t del_thread_from_manager(Thread_Base *);
error_t add_thread_to_cpu(Thread_Base *, cpu_id_t);
```

| 接口 | 说明 |
|------|------|
| `new_thread_structure` | 分配 TCB（含可选 append 尾）、init_parameter 与 IPC dummy；不入队、不发 tid。 |
| `create_thread` | `vs` **必须非 NULL**；成功则**接管**调用方对 `vs` 的活引用（含 `&root_vspace`）。装好 `thread_entry`、kstack、`arch_set_new_thread_ctx`、参数与 tid。**不**入队；**不**调 append `init`。失败不接管 `vs`。便捷封装见 `14`。 |
| `delete_thread` | 写 `status=exit`；循环 `del_thread_from_manager`，若返回 `-E_REND_AGAIN` 且本核是 owner 则先 `schedule` 再试；最后 `ref_put`，可能落到 `del_thread_structure`。 |
| `del_thread_structure` / `free_thread_ref` | 末次引用路径：摘环、释放 vs、排空 IPC、释放 kstack / name，并调 append `fini`（与 `17` 交叉）。 |
| `add_thread_to_manager` | 挂环、设 `tm`、尝试把 status 从 init 变成 ready；若已非 init 只打印 warn，仍返回成功。任一侧为 NULL 则返回成功但不挂。已有 `tm` 则返回 `-E_RENDEZVOS`。 |
| `del_thread_from_manager` | 幂等摘环；若仍是 `current` 则返回 `-E_REND_AGAIN`。 |
| `add_thread_to_cpu` | 首次入队时选核：要求 `tm==NULL` 且 status 为 `init`；否则 `-E_RENDEZVOS` / `-E_IN_PARAM`。细节见 `16`。 |

### 7.4 current / status / flags / 辅助

```c
Thread_Base *get_cpu_current_thread(void);
void set_cpu_current_thread(Thread_Base *);
void thread_set_flags(Thread_Base *, u64);
void thread_or_flags(Thread_Base *, u64);
void thread_clear_flags(Thread_Base *, u64);
error_t thread_enable_user(Thread_Base *);
error_t thread_disable_user(Thread_Base *);
u64 thread_get_status(Thread_Base *);
u64 thread_set_status(Thread_Base *, u64);
bool thread_set_status_with_expect(Thread_Base *, u64 expect, u64 target);
Message_Port_t *thread_lookup_port(const char *name);
void thread_set_name_with_copy(const char *name, Thread_Base *);
bool cpu_id_is_online(cpu_id_t);
Task_Manager *task_manager_for_cpu(cpu_id_t);
cpu_id_t thread_owner_cpu(const Thread_Base *);
```

| 接口 | 说明 |
|------|------|
| `get/set_cpu_current_thread` | 读 / 写 `percpu(core_tm)->current_thread`；尚无 TM 时分别返回 NULL / 空操作。 |
| `thread_set_flags` / `thread_or_flags` | 整字赋值 / 按位 OR（`thread_or_flags` 现走 `set_mask_u64`，语义不变）；表达退出意图只用 `THREAD_FLAG_EXIT_REQUESTED`。 |
| `thread_clear_flags` | 按位清零（`clear_mask_u64`），NULL 安全。 |
| `thread_enable_user` / `thread_disable_user` | 用户线程启用 / 收拆：启用 = 先 `fp_alloc`，成功才 OR `THREAD_FLAG_USER`（失败 flags 不动，返回 `-E_IN_PARAM` / `-E_REND_NO_MEM`）；收拆 = 先清 `USER` flag 再 `fp_free`。启用入口是 `copy_thread`（fork）与 `gen_thread_from_elf`，收拆入口是 `thread_release_owned_resources`。 |
| `thread_get/set_status*` | 原子读 / 交换 / CAS；IPC 与 `schedule` 共用同一套状态位。 |
| `thread_lookup_port` | 查本线程 LRU 缓存；返回的 port **必须**再 `ref_put`。 |
| `thread_set_name_with_copy` | 堆上 `strncpy` 拷贝名字；失败则保留旧名。 |
| `cpu_id_is_online` / `task_manager_for_cpu` / `thread_owner_cpu` | 选首次入队目标、查所属核；**不是**运行期迁核 API（见 `16`）。 |

### 7.5 tid（`id.h`）

```c
void init_core_id_system(void);
void init_id_manager(Id_Manager *idmng);
id_t get_new_id(Id_Manager *idmng);
```

| 接口 | 说明 |
|------|------|
| `init_core_id_system` | 只做 `init_id_manager(&tid_manager)`；无参数，也不扫 CPU。 |
| `get_new_id` | 用 MCS（`idmng->spin_ptr` + 本核 `id_spin_lock`）；靠单例 `tid_manager` 保证全局唯一。 |

### 7.6 arch 切换边界

```c
void switch_to(Arch_Thread_Context *old, Arch_Thread_Context *new);
extern void context_switch(Arch_Thread_Context *old, Arch_Thread_Context *new);
extern void run_thread(Thread_Init_Para *para); /* asm；见 thread.h */
```

| 接口 | x86_64 | aarch64 |
|------|--------|---------|
| `switch_to` | 先写 TSS.RSP0、KERNEL_GS、FS、`user_rsp_scratch`，再 `context_switch` | TPIDR_EL0 / SP_EL0；保存 DAIF，返回旧上下文后再恢复 DAIF+ISB |
| `context_switch` | 保存被调用者负责的那组通用寄存器和 `rsp`（`arch_switch.S`） | 保存 `x19`–`x30`、`sp`、`SPSR`（`arch_switch.S`） |
| FP / SIMD（在 `switch_to`） | `fp_state_save`/`restore` → `arch_fp_*`（`xsave64` 或 `fxsave64`） | `fp_state_save`/`restore` → `arch_fp_save_to`/`load_from` |
| `run_thread` | SysV（System V ABI）：按 `int_para[]` 调 `thread_func_ptr` | AAPCS64（ARM 64 位过程调用标准）同理 |

页表根**不**在 `switch_to` 里换。`copy_thread` / `run_copied_thread` 见 `14`。

---

## 8. 多架构

`Arch_Thread_Context`、`switch_to`、`run_thread`、`arch_set_new_thread_ctx` 按 arch 分实现。v0.1 主线启动验收以 x86_64、aarch64 为准；riscv / loongarch 头文件在，但未进主线验收。硬件细节见 §6.6；页表 / ASID 换根不在本篇重复。

---

## 9. 测试

- `smp_test[]` 表内多项（多核调度 / IPC）；其中 `thread_affinity_test` 覆盖创建时绑核
- 多数测试用例经 `gen_thread_from_func` 造内核线程；`RENDEZVOS_TEST` 下 boot 进了 `kernel_handle_msg` 之后会卡在 `recv_msg` 上，RR 不会再选中它

```bash
cd core && make ARCH=x86_64 config && make all && make run
```


---

## 10. 限制与后续

- 仅 RR；指针预留，无第二套算法。
- 无运行期迁移；阻塞线程仍属 owner CPU。
- RR 无时间片：长占 CPU 的内核线程饿死同核 ready 线程。
- 环上无 ready 则 RR 死循环（靠 idle）。
- `new_task_manager` 缺 alloc NULL 检查。
- 远期：迁移 / 可插拔调度见 evolution E3、E4。
- （已关闭）线程切换不保存浮点/向量——现已 eager 保存 SSE/AVX（x86）与 FPSIMD（aarch64）；AVX-512/SVE 仍不支持。

---

## 11. 变更记录

- 2026-11-19：底子重评批次——篇首引用缩号；VSpace/创建/模块入口裸昵称补号；§7 opener；§9「测例」→「测试用例」、厘清 `smp_test[]`。
- 2026-11-15：§6.6 对齐审定后的 `arch_fp.c`——`fp_cfg`、ALLOWED/DENIED、`EDX:EAX`、leaf `0xD` 字段表；去掉独立 detect/enable 函数与旧 `start_*` 表述。
- 2026-11-15：§6.6 补 **`xsave64` 说明**（XCR0 / RFBM / legacy 512B / leaf 0xD size）；更正表内「XSAVE 未做」。
- 2026-11-14：命名与分层对齐代码——`fp_area` / `fp_state_*` / `fp_alloc`；BUF=`SIZE+ALIGN`；去掉旧 `arch_fp_area` / scratch 表述。
- 2026-11-15：Maintainer 复审前可读性通读：「线程栈布局」图从「浮点与向量 → 两边对照」小节下独立为 `#### 线程栈布局` 节；§1 / §6.6 / §9 三处词句理顺（「要…的一方」补谓语、「切回来数就乱了」改明指、`大量测试用例`→`多数测例`）。裸反引号篇号（`14` / `16` / `31` 等）与全仓惯例一致，未动；技术内容未动。
- 2026-11-15：FP（E13）跟进复核——§4.2 用户线程约定从「显式给 `USER`」更新为 `thread_enable_user` / `thread_disable_user` 契约（FP 区先、flag 后；失败不动）；§6.6 新增「跨架构共享层」小节（`fp.h` / `fp.c` 接口表、`fp_alloc` 幂等、x86 `arch_fp_init_area` 写 FCW/MXCSR 默认值 vs aarch64 no-op、`fp_state_size` 暂无调用者）；§7.4 补 `thread_clear_flags` 与 enable / disable 两接口及调用入口；§5 代码对应补 `fp.c` 与 `arch_fp.c`；删除变更记录末尾错位的重复行「2026-10-05：最终词句顺畅」。
- 2026-11-13：§6.6 FXSAVE 布局表——澄清 XMM0–15 共 16×16B=256B；ST0–7 在偏移 32 的 8×16B 槽；512B 含头+ST+XMM+保留。
- 2026-11-13：§6.6 新增「浮点与向量」专节——x87 / XMM / AVX / AVX-512、FXSAVE vs XSAVE、aarch64 Q+FPCR/FPSR 与 SVE、本仓库 eager 边界；`switch_to` 步骤与时序图补上 FP。
- 2026-10-25：全文语言整理：回退上一轮的书面化替换，顺掉「在这里首先是」一类直译句和生硬量词，技术内容未动。
- 2026-10-05：最终词句顺畅；§6.6 / §7 写清 `context_switch` 只存通用寄存器；浮点/向量是另一套（x86 XMM，aarch64 `v0`–`v31`），保存为 **E13**。
- 2026-10-04：补硬件/架构知识——§4.2 加线程状态机 ASCII 图；§4.3 加 per-CPU TM 就绪环结构图；§6.6 加线程栈布局图与 `switch_to` 时序图，并扩写 TSS.RSP0 在 64 位模式下的角色（Intel SDM Vol.3 §7.2）与 SWAPGS / KERNEL_GS_BASE / FS_BASE 的语义。语言润色：塞→放入、随便跑→轻易跑、永远→始终、属演进→属于演进。
- 2026-10-03：去掉未接线的 `thread_status_suspend`（原仅 `cmain` 测例路径写一次，且会被 `recv_msg` 覆盖；RR 只认 `ready`）。
- 2026-10-02：删除已无定义的死声明 `tid_spin_lock`；tid MCS 仅 `percpu(id_spin_lock)`。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：§7 全文审阅——按 `thread.h` / `id.h` / arch `switch_to` Doxygen 补接口说明与编排；纠正 `init_proc`「idle→boot」误述、`new_task_manager` 伪 NULL 返回；写清 `add_thread_to_manager` 非 init 仅 warn。
- 2026-09-25：语言整理；§6.6 扩写 x86 TSS/GS/FS 与 aarch64 SP_EL0/TPIDR/DAIF 对照实现；日期对齐本轮。
- 2026-08-29：整篇重做——薄调度叙述；status / flag / zombie / exit 三分；内核切入不换根；tid 真锁；append 调用点；`init_proc` 切 idle；RR 死循环与排水副作用；纠正旧稿 `tid_spin_lock`。
- 2026-08-27：初稿；08-29 曾定点补丁「薄 RR」。
