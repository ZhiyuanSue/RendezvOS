# 线程与 Task_Manager

v0.1 · 2026-08-29

本篇覆盖：`kernel/task/thread.c`、`kernel/task/task_manager.c`、`kernel/task/thread_boot.c`（入队/`init_proc`）、`kernel/task/id.c`、`include/rendezvos/task/thread.h`、`include/rendezvos/task/id.h`；以及 arch `switch_to` / `run_thread` 边界。

`init_proc`、boot/idle 与 `cmain` 编排见 `01-启动与初始化/03-模块初始化与内核入口.md`；用户 AS 切换细节见 `15-VSpace所有权与调度切换.md`；ELF/`gen_thread_from_*`/`copy_thread` 见 `14-线程创建与ELF加载.md`；创建时绑核见 `16-CPU亲和性-创建时绑核.md`；EBR/`del_thread_structure` 见 `17-EBR与线程资源回收.md`；IPC 阻塞见 `04-IPC/19-阻塞与非阻塞收发.md`。

---

## 1. 概述

core 的调度对象是 **线程 + 地址空间**，没有一等公民「进程 / TCB 进程对象」。需要 pid/wait/fd 的个性层自己挂状态（通常塞进 `append_thread_info[]`）。

每个可调度实体是 `Thread_Base`，挂在本 CPU 的 **`Task_Manager`（per-CPU）** 环形就绪链表上。`schedule()` 持 `sched_lock` 选下一个 `thread_status_ready`，再在锁外 `switch_to`。默认策略是 **环形 next-ready**（`round_robin_schedule`）：没有时间片字段、没有优先级——「RR」在这里就是绕环找 ready。

core 的调度故意做得很薄：不把「更好的调度算法」当贡献点。真正要写死的是骨架：

| 钉子 | 含义 |
|------|------|
| per-CPU TM | 就绪环、current、锁都在本核；跨核 MCS/`me`、IPC 阻塞归属、`tlb_cpu_mask` 都假设「线程不随便跑到别的核」 |
| 创建时绑核 | 入队时选 CPU；**无**运行期迁移 API |
| status vs exit flag | IPC 会改 `block_on_*`；退出意图只能用 `THREAD_FLAG_EXIT_REQUESTED`，不能靠换一个 status 位 |
| append hooks | 兼容层挂元数据；`schedule` 热路径不读 FAM |
| `scheduler` 函数指针 | 预留换算法；v0.1 **永远**装 `round_robin_schedule` |

这和「单执行流 server + 无锁 IPC」是同一哲学：难并发收束在少数原语；策略用 hook / 换指针，而不是在每个 syscall 里长出一套锁。完整可插拔调度器属演进（evolution E4）。

---

## 2. 目标与边界

**提供：** per-CPU 就绪环；`schedule` 状态机与上下文切换钩子；与 IPC 共享的原子 `status`；exit flag / zombie 切走证明；`add`/`del`/`add_thread_to_cpu`；tid；append 生命周期钩子。

**不做：** CFS / 优先级队列；运行期迁移；sleep/wakeup 定时器子系统；在 `schedule` 里把页表强制切回 `root_vspace`；解释某 OS 的 `fork`/`clone` 标志（兼容层在 `copy_thread` 前准备 `VSpace`）。

---

## 3. 分层与调用方

**内核/server 线程** — `gen_thread_from_func(..., tm, arg)` 或 `create_thread` + `add_thread_to_manager` / `add_thread_to_cpu`。体在 `thread_entry` → `run_thread`；返回后 OR `EXIT_REQUESTED` 再 `schedule`。

**用户线程** — `gen_thread_from_elf` / `copy_thread`：须 `THREAD_FLAG_USER` + 独立用户 `VSpace`（见创建篇）。用户返回覆盖内核栈上的 `thread_entry` 帧，正常不落回入口尾部清理。

**boot** — 不走 `create_thread`：`new_thread_structure` 认领当前执行流；`vs == NULL`；栈 = per-CPU boot 栈。之后 BSP 上跑 `kernel_handle_msg`（模块入口篇）。

**idle** — `gen_thread_from_func(idle_thread, …)`，死循环 `schedule`；给 RR 兜底，并顺带在「很少 malloc」的核上排水（见 §6.2）。

**跨核创建** — 只在**首次入队**选目标 TM（`add_thread_to_cpu` / `gen_thread_from_func(..., task_manager_for_cpu(cpu), …)`）。入队后 `thread->tm->owner_cpu` 固定。

调用方须知：`delete_thread` 若目标仍是 current，会在 owner CPU 上自旋 `schedule`；IPC 临时改 `block_on_*`，**不得**用 status 单独表达 exit。

---

## 4. 数据结构与不变量

### 4.1 Thread_Base

定义于 `thread.h`（`THREAD_COMMON` + append FAM）：

| 字段 | 含义 |
|------|------|
| `sched_thread_list` | 嵌入 TM 环 |
| `tm` | 所属 per-CPU TM；NULL = 未入队或已 detach |
| `status` | 原子调度/IPC 状态 |
| `flags` | `USER` / `EXIT_REQUESTED` / `IPC_PORT_CLOSED` |
| `vs` | AS 所有权 ref；boot 为 NULL |
| `ctx` | `Arch_Thread_Context` |
| `kstack_bottom` / `kstack_num` | 内核栈顶（高地址）与页数；默认 `thread_kstack_page_num == 2` |
| `init_parameter` | 入口函数 + ABI 整型参数 |
| `refcount` | TCB 生命周期；IPC 请求等可额外 hold |
| `send_msg_queue` / `recv_msg_queue` | per-thread MSQ（dummy 在 `new_thread_structure` 分配） |
| `port_ptr` / `port_cache` | 阻塞 port；按名 LRU（最多 16，不 pin port） |
| `append_hooks` + `append_thread_info[]` | 可选尾区 |

**不变量：**

- `tm != NULL` ⇒ 节点在对应环上（`del` 校验 current）。
- `add_thread_to_manager` 期望入队前 `status == init`，成功 CAS → `ready`（失败只 `pr_warn`，仍返回 SUCCESS）。
- `create_thread` 成功 ⇒ **caller 不得再** `ref_put` 传入的 `vs`。
- **没有**线程上的 affinity 字段；亲和 = `thread->tm->owner_cpu`。
- 宏名 `THERAD_SCHE_COMMON` 少一个 D——以源码为准，勿「纠正」成文档里的拼写。

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
        thread_status_suspend,
        thread_status_exit,
};
```

| 状态 | 谁设置 | 调度含义 |
|------|--------|----------|
| `running` / `ready` | `schedule` demote/promote | RR 只选 `ready` |
| `block_on_*` | IPC 路径 | 先改 status 再 `schedule`；demote CAS 失败 → 保持阻塞 |
| `zombie` | owner CPU 在切走带 `EXIT_REQUESTED` 的 ready 线程时 | 仍可挂在环上；clean 观测 |
| `exit` | `delete_thread` 入口 | **不是** zombie；逻辑拆结构前的标 |
| `suspend` | 测例等 | 效果=非 ready；无完整 wait 子系统 |

**三条退出路径（别混）：**

1. **自愿结束（core）：** OR `EXIT_REQUESTED` → `schedule` → 切走且仍 `ready` → **zombie**。  
2. **`delete_thread`：** 直接写 `status = exit`，再 `del` + `ref_put`。  
3. **compat/clean：** 可强制 zombie；细节见 EXIT/clean 协议，不在本篇展开。

`THREAD_FLAG_EXIT_REQUESTED` = 意图；`zombie` = owner CPU「已切走」证明。IPC 改 status **擦不掉** flag。

默认 `flags = THREAD_FLAG_NONE` → 内核线程；用户须显式 `THREAD_FLAG_USER`。`USER` **不得**搭配 `&root_vspace`（`schedule` 当 misconfig）。

### 4.3 Task_Manager

```c
struct task_manager {
        cas_lock_t sched_lock;
        struct list_entry sched_thread_list;
        cpu_id_t owner_cpu;
        Thread_Base* current_thread;
        Thread_Base* (*scheduler)(Task_Manager* tm);
};
```

- `DEFINE_PER_CPU(Task_Manager*, core_tm)` — **始终** `percpu(core_tm)` / `per_cpu(core_tm, cpu)`；头文件 `extern Task_Manager* core_tm` 是模板声明，不是全局单例。
- `choose_schedule` 只装 RR，并顺带 `is_print_sche_info = false`。
- `new_task_manager`：alloc 后立刻 `choose_schedule`（alloc 失败未判空——已知缺口）。

### 4.4 tid

全局 `Id_Manager tid_manager`；`get_new_id` 用 MCS，`me = &percpu(id_spin_lock)`。从 0 单调递增，到 `INVALID_ID`（`U64_MAX`）后不再自增。**不回收**。

`id.h` 里的 `extern spin_lock_t tid_spin_lock` 是**死声明**（全树无定义）；真锁是 percpu `id_spin_lock`。`init_core_id_system` 在 `cmain` 里、`init_proc` 前调用。

### 4.5 append hooks

| Hook | 何时调用 |
|------|----------|
| `init` | **仅** `run_elf_program`（有 `elf_info`）；`create_thread` **不**调 |
| `copy` | `copy_thread` 成功路径末尾 |
| `fini` | `del_thread_structure`，在释放 vs/堆之前（fini 时 vs 仍有效） |

分配长度 = `sizeof(Thread_Base) + append_info_len`。钩子表通常静态共享；成员可为 NULL。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `thread.h` | 类型、flags、公开 API、所有权注释 |
| `thread.c` | 分配、`create_thread`、`delete_thread`、`copy_thread`、port cache |
| `thread_boot.c` | `add`/`del`、`init_proc`、boot/idle、`kernel_handle_msg` |
| `task_manager.c` | `schedule`、RR、`new_task_manager` |
| `id.c` | tid |
| `arch/*/task/arch_thread.c`、`arch_switch.S`、`arch_run_thread.S` | `switch_to`、首次 ctx、`run_thread` |

---

## 6. 流程

### 6.1 入队与首次运行

调用方 `create_thread`（装 `thread_entry`、可选预留 trap frame）→ `add_thread_to_manager` / `add_thread_to_cpu`（`init→ready`）→ 某次 `schedule` RR 选中 → `switch_to` → `thread_entry` → `run_thread(func)`。

用户 `copy_thread` 返回的线程**未入队**；入口是 `run_copied_thread` → `arch_return_to_user`。

`add_thread_to_manager`：任一指针 NULL → **返回 SUCCESS 且不链接**（静默）。已有 `tm` → `-E_RENDEZVOS`。

`add_thread_to_cpu`：仅允许 `tm==NULL` 且 `status==init`（创建期首次）。

`del_thread_from_manager`：仍是 current → `-E_REND_AGAIN`；无 tm 幂等 SUCCESS。

### 6.2 `schedule` 主路径

```text
kalloc_process_cross_cpu_frees()   ← 排水，不是调度算法
ebr_try_reclaim()                  ← 同上
lock sched_lock
  curr: running → ready（已是 block_* 则 CAS 失败，保持阻塞）
  next = scheduler(tm)             ← RR
  若 next 空或 == curr → 恢复 running，解锁，不 switch
  若 next 带 USER → 可能换用户 AS（细节见 VSpace 篇）
  若 curr 有 EXIT_REQUESTED 且仍 ready → zombie
  next → running
unlock
switch_to(curr → next)
```

**本篇只钉边界：** 仅当**切入**线程带 `THREAD_FLAG_USER` 才尝试换用户 AS；切入内核/idle **不**强制回 `root_vspace`，可能继续跑在上一用户页表上（mask/extra ref 滞后清理由 VSpace 篇管）。

谁调 `schedule`：idle 循环；IPC 阻塞后；`delete_thread` 重试；用户态进入的 trap/IRQ 末尾（内核态 trap **不**自动让出）。

### 6.3 round_robin_schedule

从 `current->sched_thread_list.next` 绕环，跳过哨兵与非 `ready`。选中自己 → 上层走 `use_old_thread`。

环上**全无 ready** 时 while **死循环**——惯例靠 idle 永不永久非 ready。无时间片。

### 6.4 delete_thread

写 `status = exit` → 循环 `del_thread_from_manager`；`-E_REND_AGAIN` 且本核是 owner → `schedule` → 再试 → `ref_put` → 可能 `del_thread_structure`。

### 6.5 `init_proc`（boot ↔ idle）

1. `new_task_manager` → boot（认领执行流，`running`）→ idle（入队）  
2. 人为：`current = idle`（running），boot = ready  
3. `switch_to(boot → idle)`：保存 boot 现场，跳进 idle；之后 idle `schedule` 可切回 boot，`init_proc` 在 **boot** 上返回  

boot **没有** `arch_set_new_thread_ctx`；首次保存靠这次 `switch_to`。

### 6.6 arch 切换边界

- `switch_to`（C）：TLS / 用户 SP 等，再调汇编 `context_switch`。  
- x86：TSS RSP0、KERNEL_GS/FS_BASE、`user_rsp_scratch`。  
- aarch64：TPIDR_EL0、SP_EL0、DAIF；回旧线程时恢复 old DAIF。  
- 用户线程 `reserve_trap_frame=true`（栈顶预留 trap frame）；内核线程 `false`。aarch64 预留后强制 16B 对齐。

---

## 7. 公开 API

| API | 摘要 |
|-----|------|
| `schedule` / `round_robin_schedule` / `choose_schedule` | 调度与默认 RR |
| `new_task_manager` / `del_task_manager_structure` | TM 结构体 |
| `new_thread_structure` / `create_thread` / `delete_thread` | TCB 生命周期；create **吞 vs** |
| `add_thread_to_manager` / `del_thread_from_manager` / `add_thread_to_cpu` | 环 |
| `get/set_cpu_current_thread` | 本核 current |
| `thread_get/set_status(_with_expect)` / `thread_set/or_flags` | 状态与 flags |
| `thread_lookup_port` / `thread_set_name_with_copy` | port 缓存；名字堆拷贝 |
| `copy_thread` / `run_copied_thread` | 用户复制（创建篇详述） |
| `cpu_id_is_online` / `task_manager_for_cpu` / `thread_owner_cpu` | 亲和辅助 |
| `get_new_id` / `init_core_id_system` | tid |

`gen_thread_from_func` / `gen_thread_from_elf` → 创建篇；`init_proc` / boot IPC → 模块初始化篇。

---

## 8. 多架构

`Arch_Thread_Context`、`switch_to`、`run_thread`、`arch_set_new_thread_ctx` 分 arch。v0.1 bring-up 以 x86_64、aarch64 为准；riscv/loongarch 头文件在，未进主线验收。

---

## 9. 测试

- `smp_test` — 多核调度/IPC  
- `thread_affinity_test`（`smp_test[]`）— 创建时绑核  
- 大量测例经 `gen_thread_from_func` 造内核线程；`RENDEZVOS_TEST` 可将 boot 标 `suspend` 使之不再被 RR 选中  

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

本篇未复测。

---

## 10. 限制与后续

- 仅 RR；指针预留，无第二套算法。  
- 无运行期迁移；阻塞线程仍属 owner CPU。  
- `thread_status_suspend` 无完整 wait 子系统。  
- RR 无时间片：长占 CPU 的内核线程饿同核 ready。  
- 环上无 ready → RR 死循环（靠 idle）。  
- `new_task_manager` 缺 alloc NULL 检查；`tid_spin_lock` 死声明。  
- 远期：迁移 / 可插拔调度见 evolution E3、E4。

---

## 11. 变更记录

- 2026-08-29：整篇重做——薄调度叙述；status/flag/zombie/exit 三分；内核切入不换根；tid 真锁；append 调用点；`init_proc` 切 idle；RR 死循环与排水副作用；纠正旧稿 `tid_spin_lock`。
- 2026-08-27：初稿；08-29 曾定点补丁「薄 RR」。
