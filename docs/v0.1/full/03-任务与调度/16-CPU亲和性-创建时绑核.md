# CPU 亲和性：创建时绑核

v0.1 · 2026-09-25

本篇覆盖：`add_thread_to_cpu` / `cpu_id_is_online` / `task_manager_for_cpu` / `thread_owner_cpu`（`thread.c` / `thread.h`）、`modules/test/thread_affinity_test.c`。

调度环见 `13-线程与Task_Manager.md`；`create_thread` / `gen_thread_from_*` 见创建篇；`NR_CPU` / SMP 拓扑见 `06-SMP与同步/`。IRQ / GIC / IOAPIC 的中断亲和 **不是**本篇——那是「中断送到哪颗核」，和「线程挂在哪条就绪环」是两套东西。

---

## 1. 概述

每核一个 `Task_Manager`、一套就绪环。调度、IPC 阻塞归属、`tlb_cpu_mask`、percpu MCS `me` 都按「线程只属于一个 owner CPU」来写。

v0.1 的「亲和」就是**首次入队选哪条环**，不是 Linux 那种 `cpumask`，也不是运行期迁核。

没有线程上的 affinity 字段。入队成功后，亲和性就是 `thread->tm->owner_cpu`。头文件写明：**无运行期迁移**；running / blocked / exiting 禁止再 `add_thread_to_cpu`。

用途：per-CPU server 复制、跨核探针、避免「创建者以为在 A 核跑、其实还挂在创建者环上」。

---

## 2. 目标与边界

**提供：** 判断 CPU 是否已有 `core_tm`；取目标 TM；首次入队到指定核；查询 owner。

**不做：** `migrate_thread_to_cpu`；负载均衡 / work stealing；按 IRQ 亲和自动绑核；保证「创建调用所在核」==「首次运行核」（首次运行只保证在 **owner**；创建可来自任意核）。

compat 若要 `sched_setaffinity`：在自有 proc 记期望 CPU，**创建时**映射到本 API；已存在线程改不了。

---

## 3. 分层与调用方

| 场景 | 用法 |
|------|------|
| 同核 server | `gen_thread_from_func(..., percpu(core_tm), ...)` |
| per-CPU 复制 | `for cpu: gen_thread_from_func(..., task_manager_for_cpu(cpu), ...)`（先确认 tm 非 NULL） |
| 分步 | `create_thread`（`init`，`tm=NULL`）→ `add_thread_to_cpu(t, j)` |
| 跨核 probe | 测例：核 `i` 往 `(i+1)%NR_CPU` 入队 |

错误用法：已有 `tm` 再 add；`status != init`；offline CPU；把 GIC / MPIDR / APIC 中断亲和跟线程绑核混谈。

`gen_thread_from_elf` **只**挂本核 `percpu(core_tm)`——用户 ELF harness 无跨核 helper。

---

## 4. 数据结构与不变量

- `Task_Manager::owner_cpu` — `new_task_manager` 时 = 创建该 TM 的 `percpu(cpu_number)`，固定。
- `thread_owner_cpu(t)` — `t->tm->owner_cpu`，无 tm → `CPU_ID_INVALID`（detach / `delete` 摘环后**不能**当终身标签）。

**`cpu_id_is_online(cpu)`（完整条件）：**

```text
cpu < RENDEZVOS_MAX_CPU_NUMBER
&& 0 ≤ cpu < NR_CPU
&& per_cpu(core_tm, cpu) != NULL
```

**不**表示可 migrate，只表示「可以首次选这核入队」。这里的「online」是软件侧「该核已经有 Task_Manager」，不是 ACPI / MADT 里 Local APIC 是否 enabled，也不是 GIC 的 CPU interface 是否起来——后者见 SMP / 平台中断篇。

**`add_thread_to_cpu` 门禁：**

| 条件 | 返回 |
|------|------|
| `thread == NULL` | `-E_IN_PARAM` |
| `thread->tm != NULL` | `-E_RENDEZVOS` |
| `status != init` | `-E_RENDEZVOS` |
| `task_manager_for_cpu` 空 | `-E_IN_PARAM` |
| 否则 | `add_thread_to_manager` |

注意：`add_thread_to_manager` **本身**对非 init 只 `pr_warn` 仍链接——严格 status 检查只在 `add_thread_to_cpu`。绑核路径请走后者。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `thread.h` / `thread.c` | 四 helper + 无迁移注释 |
| `thread_boot.c` `add_thread_to_manager` | 实际入环 |
| `thread_affinity_test.c` | SMP 环绑核验证 |
| `new_task_manager` | 固定 `owner_cpu` |

---

## 6. 流程

1. 确认目标 online → `task_manager_for_cpu`
2. `create_thread` 或 `gen_thread_from_func(..., tm, ...)`
3. 若分步：`add_thread_to_cpu` → `init→ready`，`tm` 指向目标
4. 之后只在该核 `schedule` 上被 RR 选中

测例：CPU `i` 在 `(i+1)%NR_CPU` 建 probe（arg=creator）；probe 写 `affinity_seen[ran_on]=creator`；check `affinity_seen[j]==(j+NR_CPU-1)%NR_CPU`。创建者 spin `schedule` 等到 seen，再等到 zombie 后 `delete_thread`。

---

## 7. 公开 API

```c
bool cpu_id_is_online(cpu_id_t cpu);
Task_Manager *task_manager_for_cpu(cpu_id_t cpu);
cpu_id_t thread_owner_cpu(const Thread_Base *thread);
error_t add_thread_to_cpu(Thread_Base *thread, cpu_id_t cpu);
```

---

## 8. 多架构

与 ISA 无关；逻辑 `cpu_id` 与 arch affinity / MPIDR / APIC ID 对照见 SMP 拓扑篇，勿混进本篇。硬件中断路由（IOAPIC RTE、GICD ITARGETSR / affinity）改的是「中断落到哪」，不会改 `thread->tm`。

---

## 9. 测试

`smp_thread_affinity_test` / `smp_thread_affinity_check`（`smp_test[]`）。本篇未复测。

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

---

## 10. 限制与后续

- 无运行期迁移（evolution E3）。
- ELF harness 不能跨核创建。
- `thread_owner_cpu` 摘环后失效。
- Linux affinity 只能映射到创建时选核。

---

## 11. 变更记录

- 2026-09-25：语言整理；厘清软件 online / 绑核与中断亲和（GIC / APIC）不是同一概念。
- 2026-08-29：整篇重做——「亲和=首次入队」；online 完整条件；`add_thread_to_cpu` vs `add_thread_to_manager`；IRQ 不混；owner 非终身标签。
- 2026-08-27：初稿。
