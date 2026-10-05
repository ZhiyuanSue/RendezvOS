# CPU 亲和性：创建时绑核

v0.1 · 2026-09-26

本篇覆盖：`add_thread_to_cpu` / `cpu_id_is_online` / `task_manager_for_cpu` / `thread_owner_cpu`（`thread.c` / `thread.h`）、`modules/test/thread_affinity_test.c`。

调度环见 `13-线程与Task_Manager.md`；`create_thread` / `gen_thread_from_*` 见创建篇；`NR_CPU` / SMP 拓扑见 `06-SMP与同步/`。IRQ / GIC / IOAPIC 的中断亲和 **不是**本篇——那是「中断送到哪颗核」，和「线程挂在哪条就绪环」是两套东西。

---

## 1. 概述

每核一个 `Task_Manager`、一套就绪环。调度、IPC 阻塞归属、`tlb_cpu_mask`、percpu MCS `me` 都按「线程只属于一个 owner CPU」来写。

v0.1 的「亲和」就是**首次入队选哪条环**，不是 Linux 那种 `cpumask`，也不是运行期迁核。

没有线程上的 affinity 字段。入队成功后，亲和性就是 `thread->tm->owner_cpu`。头文件写明：**无运行期迁移**；running / blocked / exiting 禁止再 `add_thread_to_cpu`。

用途：per-CPU server 复制、跨核探针、避免「创建者以为在 A 核跑、其实还挂在创建者环上」。

per-CPU TM 派发示意（创建者可在任意核，目标核由 `add_thread_to_cpu` 选定）：

```text
   创建者 CPU i                          目标 CPU j
   ┌──────────────────────┐              ┌──────────────────────┐
   │ core_tm[i]            │              │ core_tm[j]            │
   │  current = creator    │              │  current = idle       │
   │  就绪环: [creator,...] │              │  就绪环: [idle]        │
   └──────────────────────┘              └──────────────────────┘
        │
        │ create_thread(..., vs, ...)      ← status=init, tm=NULL
        │ add_thread_to_cpu(t, j)          ← 校验 tm==NULL && status==init
        │   └─► task_manager_for_cpu(j)    ← 软件在线判定
        │       add_thread_to_manager      ← 挂到 j 的就绪环，init→ready
        ▼
   t->tm = core_tm[j];  t->tm->owner_cpu = j
   之后 t 只在 CPU j 的 schedule 上被 RR 选中
   创建者 CPU i 永不调度 t；CPU j 永不感知 i 的就绪环
```

CPU i 与 CPU j 的就绪环互不可见：`schedule` 只扫本核 `core_tm` 的环，IPC 阻塞归属、`tlb_cpu_mask` 本地位也都按 owner CPU 算。这就是「无运行期迁移」能成立的前提——一旦运行期迁核，percpu `me`、mask、IPC 归属全部要重做。

---

## 2. 目标与边界

**提供：** 判断 CPU 是否已有 `core_tm`；取目标 TM；首次入队到指定核；查询 owner。

**不做：** `migrate_thread_to_cpu`；负载均衡 / work stealing；按 IRQ 亲和自动绑核；保证「创建调用所在核」==「首次运行核」（首次运行只保证在 **owner**；创建可来自任意核）。

compat 若要 `sched_setaffinity`：在自有 proc 记期望 CPU，**创建时**映射到本 API；已存在线程无法修改。

---

## 3. 分层与调用方

| 场景 | 用法 |
|------|------|
| 同核 server | `gen_thread_from_func(..., percpu(core_tm), ...)` |
| per-CPU 复制 | `for cpu: gen_thread_from_func(..., task_manager_for_cpu(cpu), ...)`（先确认 tm 非 NULL） |
| 分步 | `create_thread`（`init`，`tm=NULL`）→ `add_thread_to_cpu(t, j)` |
| 跨核 probe | 测试用例：核 `i` 往 `(i+1)%NR_CPU` 入队 |

错误用法：已有 `tm` 再 add；`status != init`；offline CPU；把 GIC / MPIDR / APIC 中断亲和跟线程绑核混为一谈。

`gen_thread_from_elf` **只**挂本核 `percpu(core_tm)`——用户 ELF harness 无跨核 helper。

---

## 4. 数据结构与不变量

### 4.1 `owner_cpu`

- `Task_Manager::owner_cpu` — `new_task_manager` 时 = 创建该 TM 的 `percpu(cpu_number)`，固定。
- `thread_owner_cpu(t)` — `t->tm->owner_cpu`，无 tm → `CPU_ID_INVALID`（detach / `delete` 摘环后**不能**当终身标签）。

### 4.2 能否往这颗核上挂线程（软件 online）

`cpu_id_is_online(cpu)` 的完整条件：

```text
cpu < RENDEZVOS_MAX_CPU_NUMBER
&& 0 ≤ cpu < NR_CPU
&& per_cpu(core_tm, cpu) != NULL
```

这只表示「可以第一次把线程挂进这颗核的 `Task_Manager`」，**不**表示以后能迁核。这里的 online 是软件侧「该核已经有 `core_tm`」，不是 MADT（ACPI 多 APIC 描述表）里 Local APIC 是否 enabled，也不是 GIC 的 CPU interface 是否起来——硬件侧见 SMP 篇、平台中断篇。图中 SIPI 指 Startup IPI（x86 启动 AP 的方式）。

软件 online 与硬件 online 对照：

```text
   硬件 online (平台层)              软件 online (本篇)
   ───────────────────────────       ──────────────────────────────
   MADT / APIC: Local APIC enabled   per_cpu(core_tm, cpu) != NULL
   GIC: CPU interface 已启用          new_task_manager 已在该核跑过
   CPU 已被 SIPI 拉起、跑 cmain       init_proc 已完成、就绪环可挂线程
   ───────────────────────────       ──────────────────────────────
   硬件 online 是软件 online 的前提，但两者不等价：
   一颗核可能硬件已起但 core_tm 尚未建（早期 AP），此时 cpu_id_is_online=false。
```

函数签名见 §7。

### 4.3 第一次入队时检查什么

`add_thread_to_cpu` 的前置条件：

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

测试用例：CPU `i` 在 `(i+1)%NR_CPU` 创建 probe（arg=creator）；probe 写 `affinity_seen[ran_on]=creator`；check `affinity_seen[j]==(j+NR_CPU-1)%NR_CPU`。创建者 spin `schedule` 直至 seen 置位，再等到 zombie 后 `delete_thread`。

---

## 7. 公开 API

本篇涉及的接口分布在：创建时选核的四个 helper（`thread.h` / `thread.c`）。说明改写自头文件 Doxygen，并已与 `.c` 核对。调度环 / `add_thread_to_manager` 细节见 `13`；`gen_thread_from_*` 见 `14`；`NR_CPU` / 拓扑见 SMP 篇。

**本篇不涉及：** 运行期迁核（无此 API）；IRQ / GIC / IOAPIC 中断亲和；ACPI「CPU online」语义。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序 |
|------|------|
| 同核 | `gen_thread_from_func(..., percpu(core_tm), ...)` |
| 指定核（便捷） | 确认 `task_manager_for_cpu(cpu)` 非 NULL → `gen_thread_from_func(..., tm, ...)` |
| 分步绑核 | `create_thread`（status=`init`，`tm==NULL`）→ **`add_thread_to_cpu(t, cpu)`** → 仅在 owner 上被 RR 选中 |
| 查询 | 入队后 `thread_owner_cpu(t)`；摘环后勿再当标签 |

创建调用所在核 **不必** 等于目标核；首次运行只保证在 **owner**。

### 7.2 选核与入队

```c
bool cpu_id_is_online(cpu_id_t cpu);
Task_Manager *task_manager_for_cpu(cpu_id_t cpu);
cpu_id_t thread_owner_cpu(const Thread_Base *thread);
error_t add_thread_to_cpu(Thread_Base *thread, cpu_id_t cpu);
```

| 接口 | 说明 |
|------|------|
| `cpu_id_is_online` | 条件：`cpu < MAX` ∧ `0 ≤ cpu < NR_CPU` ∧ `per_cpu(core_tm,cpu)≠NULL`。只表示「可以首次入队」；不等于 MADT / GIC 硬件 online。 |
| `task_manager_for_cpu` | 若 online 则返回该核 `core_tm`，否则 NULL。 |
| `thread_owner_cpu` | 读 `tm->owner_cpu`；无 tm → `CPU_ID_INVALID`（detach / delete 摘环后失效）。 |
| `add_thread_to_cpu` | 门禁：NULL → `-E_IN_PARAM`；已有 tm 或 status≠init → `-E_RENDEZVOS`；offline → `-E_IN_PARAM`；否则转调 `add_thread_to_manager`。**没有**运行期迁核。 |

对照：`add_thread_to_manager` 对非 init 只打印 warn 仍挂环——严格的 status 检查只在本 API。`gen_thread_from_elf` **固定挂本核**，没有跨核 helper。

---

## 8. 多架构

与 ISA 无关；逻辑 `cpu_id` 与 arch affinity / MPIDR / APIC ID 对照见 SMP 拓扑篇，勿混入本篇。硬件中断路由（IOAPIC RTE、GICD ITARGETSR / affinity）改的是「中断送达哪颗核」，不会改 `thread->tm`。

---

## 9. 测试

`smp_thread_affinity_test` / `smp_thread_affinity_check`（`smp_test[]`）。

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

- 2026-10-05：任务 1/3/5 精读——§3/§6「测例」→「测试用例」、「建 probe」→「创建 probe」；§3「混谈」→「混为一谈」；§2「改不了」→「无法修改」；§4.2「GIC: CPU interface 已起」→「已启用」、首次出现 MADT 补全称（ACPI 多 APIC 描述表）、SIPI 补说明（Startup IPI）；§7.2「只 warn」→「只打印 warn」；§8「勿混进」→「勿混入」。
- 2026-10-04：补硬件/架构知识——§1 加 per-CPU TM 派发 ASCII 图，画清创建者 CPU 与目标 CPU 的就绪环互不可见；§4.2 加软件 online 与硬件 online（MADT / GIC CPU interface）对照图，写明前者以后者为前提但不等价。
- 2026-10-04：§4 拆成 `owner_cpu`、软件 online、第一次入队检查三个小节。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：§7 全文审阅——四 helper Doxygen（online 三条件、门禁顺序、owner 非终身标签）；写清编排与「≠中断亲和」。
- 2026-09-25：语言整理；厘清软件 online / 绑核与中断亲和（GIC / APIC）不是同一概念。
- 2026-08-29：整篇重做——「亲和=首次入队」；online 完整条件；`add_thread_to_cpu` vs `add_thread_to_manager`；IRQ 不混；owner 非终身标签。
- 2026-08-27：初稿。
- 2026-10-05：最终词句顺畅。
