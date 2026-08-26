# Tasks, threads, and ELF loading

Core scheduling units and program load helpers.

External callers: [`USING_CORE.md`](USING_CORE.md) · API index: [`GUIDE.md`](GUIDE.md) §6

Core’s object model is **thread + address space**. There is no first-class process / TCB object. A personality that needs pid/wait/fd/signal keeps that state itself (Linux: a heap `linux_proc` pointed from thread append).

---

## Runtime context（当前 CPU / 线程）

调度与 IPC 原语假定在 **正确的 CPU** 上、且能解析 **当前线程**。

| Need | API | Header |
|------|-----|--------|
| Current thread | `get_cpu_current_thread()` | `task/thread.h` |
| This CPU’s scheduler | `percpu(core_tm)` → `Task_Manager*` | `task/thread.h`, `smp/percpu.h` |
| Running thread in TM | `percpu(core_tm)->current_thread` | `task/thread.h` |
| Active address space on CPU | `percpu(current_vspace)` | `mm/vmm.h` |
| Page-table helper on CPU | `&percpu(Map_Handler)` | `mm/map_handler.h` |

`core_tm` 是 per-CPU 的 `Task_Manager` 模板变量；**始终**通过 `percpu(core_tm)` 访问，不要把它当全局单例指针使用。

`gen_thread_from_func(..., tm, ...)` 的 `tm` 通常为 `percpu(core_tm)`。跨 CPU 操作其他 CPU 的 run queue 或 per-CPU 结构需要项目约定的 SMP 同步（见仓库协作文档中的 teardown 规则）。

### CPU affinity（线程绑核）

**创建时绑核（已实现，DONE #72）：**

| API | 作用 |
|-----|------|
| `cpu_id_is_online(cpu)` | `@p cpu` 是否已上线且有 `core_tm`（**不**表示可把运行中线程迁过去） |
| `task_manager_for_cpu(cpu)` | 取目标 CPU 的 `Task_Manager*` |
| `thread_owner_cpu(thread)` | 查询线程所在 run queue 的 `owner_cpu` |
| `add_thread_to_cpu(thread, cpu)` | **首次**入队：须 `thread_status_init` 且 `thread->tm == NULL` |

**惯例：** `gen_thread_from_func(..., percpu(core_tm), ...)`（本核）；跨核则 `gen_thread_from_func(..., task_manager_for_cpu(cpu), ...)`（先确认 `tm` 非 NULL）。

**未实现：** 运行期迁移（须在线程已下 CPU、从原 queue 摘下后再挂新核；`running` / `block` / `exit` 禁止 `add_thread_to_cpu`）。IRQ 绑核见 [`trap.md`](trap.md)，勿与本节混淆。

**测（`modules/test/thread_affinity_test.c`，挂在 `smp_test[]`）：**  
`smp_thread_affinity_test` — CPU `i` 在 `(i+1)%NR_CPU` 上创建探针，arg=creator；探针写 `affinity_seen[ran_on]=creator`。  
`smp_thread_affinity_check` — 断言 `affinity_seen[j]==(j+NR_CPU-1)%NR_CPU`（有 `check_result` 时框架忽略各核 `test()` 返回值）。

### Thread display name

`thread_set_name_with_copy(name, thread)`：内核 alloc 拷贝（`strncpy`）；线程拥有缓冲区，teardown 路径 `m_free`。字面量 / 静态串 / 堆串均可传入；**勿**再假设「只挂指针、不拷贝」。

### Thread status and flags

| Mechanism | API |
|-----------|-----|
| Block / wake | `thread_set_status`, `schedule(percpu(core_tm))` |
| Exit intent (survives IPC status) | `thread_or_flags(thread, THREAD_FLAG_EXIT_REQUESTED)` |
| User vs kernel thread | `THREAD_FLAG_USER` in `thread->flags` |

---

## Objects

| Type | Header | Role |
|------|--------|------|
| `Task_Manager` | `task/thread.h` | Per-CPU scheduler and run queues |
| `Thread_Base` | `task/thread.h` | Schedulable unit. After successful `create_thread` / `copy_thread`, `thread->vs` is non-NULL (user AS or `&root_vspace`). **Exception:** boot thread from `create_boot_thread` — `new_thread_structure` only; `thread->vs` stays NULL |
| `VSpace` | `mm/vmm.h` | Address space (radix + page tables). Registered with `register_vspace(vs, root_vs)`; RB key is `vspace_root_addr` (no numeric vspace id). `create_thread` / `copy_thread` take ownership onto `Thread_Base->vs` |
| `Arch_Thread_Context` | `arch/*/thread_arch.h` | Saved registers, user SP, TLS fields |

Kernel threads do **not** attach to a fake root task. **`thread->vs`** 与 ownership / schedule 细则见下节 **VSpace ownership**（boot thread 例外：`vs == NULL`）。

---

## VSpace ownership

权威注释：`task/thread.h`（`create_thread` / `copy_thread`）、`mm/vmm.h`（`VSpace.refcount`）。TLB / `tlb_cpu_mask` 见 [`memory.md`](memory.md) §0.6。

| API | Caller obligation |
|-----|-------------------|
| `create_thread(..., vs, ...)` | `vs` **non-NULL**；传入 caller 的 **live ref**（create/clone/`ref_get`，或 kernel 对 `&root_vspace` 的 `ref_get_not_zero`）。成功则 ref 转移到 `thread->vs`，**无二次 get**；caller 不得再 `ref_put`。失败且尚未 assign 时 caller 仍持有 `vs`。 |
| `copy_thread(src, vs, ret)` | 同上。共享父 AS：caller 先 `ref_get(&parent->vs->refcount)`。部分失败路径 core 会 `ref_put` 传入的 `vs`。 |
| `gen_thread_from_func` | `ref_get_not_zero(&root_vspace.refcount)` → `create_thread(..., &root_vspace, ...)`；`create_thread` 失败则 put 回滚。 |
| `gen_thread_from_elf` | `create_vspace` → `register_vspace(vs, &root_vspace)` → `create_thread(run_elf_program, vs, …)` → stack → `THREAD_FLAG_USER` → `add_thread_to_manager`。 |
| `run_elf_program(slice)` | 仅用 **`current_thread->vs`**（无 `vs` 参数）。Path B 加载 + 可选 `append_hooks->init`。 |
| `register_vspace(vs, root_vs)` | `create_vspace` / `clone_vspace` 之后、thread 取得 ownership 之前。RB 键 = `vspace_root_addr`（无 vspace id）。 |
| Thread teardown | `del_thread_structure` 路径：先 `append_hooks->fini`，再对 `thread->vs` **ownership `ref_put` only** — 不切换 CR3/TTBR，不卸 leftover user AS。 |
| `schedule` | 仅当 **next** 为 `THREAD_FLAG_USER` 且 user `vs`（非 `&root_vspace`）时切换 HW AS 并 extra-get + 置本 CPU `tlb_cpu_mask`。切到 kernel/idle：**不**清 mask、**不** drop extra ref、**不**强制换 AS。user→user（不同 `vs`）：换 AS，清旧 mask，drop 旧 extra ref。 |

**Invariant:** `percpu(current_vspace) ==` user `vs` ⇒ 本 CPU 持有 schedule extra ref 且 mask 位置位；末线程 ownership put 后 `del_vspace` 可能仍被 CPU extra 推迟，直到之后 user→user 切换。

**USER + AS:** `THREAD_FLAG_USER` 不得搭配 `&root_vspace`（`schedule` 视为 misconfig）。

**Path A / B:** A = 同线程 syscall 内 exec（`load_elf_to_vs` + `arch_syscall_set_user_return`）；B = 新线程体 `run_elf_program`。

---

## Kernel threads

```c
error_t gen_thread_from_func(Thread_Base** out, kthread_func fn,
                             char* name, Task_Manager* tm, void* arg);
```

Entry runs in kernel mode; block with `thread_set_status` and `schedule` when waiting on IPC or other events.

Module registration: `DEFINE_INIT` / `do_init_call()` in `task/initcall.h`.

---

## User ELF images

Typical in-address-space sequence:

1. `vspace_clear_user_mappings(vs, handler, true)` — remove existing user mappings
2. `load_elf_to_vs(slice, vs, &max_end)` — map `PT_LOAD` from a populated `page_slice`
3. `generate_user_stack(vs)` — user stack at `USER_SPACE_TOP`
4. Return to user mode via arch syscall-return helpers on the trap frame:

```c
arch_syscall_set_user_return(tf, ctx, entry, user_sp, ret_val);
arch_syscall_set_user_int_arg(tf, arg_index, value);  /* optional ABI arg */
```

One-shot helpers:

| API | Role |
|-----|------|
| `load_elf_to_vs` | Map ELF PT_LOAD into a `VSpace` |
| `generate_user_stack` | Map user stack at `USER_SPACE_TOP` |
| `gen_thread_from_elf` | Bare-core / incbin: create vs + user thread + enqueue; body is `run_elf_program` (Path B) |
| `run_elf_program` | Load ELF into current thread’s vs, optional `append_hooks.init`, drop to user |

Linux personalities usually load images with `linux_exec_replace_image` (PID1 / `sys_execve`). Use `gen_thread_from_elf` when there is no FS yet and the image is already a `page_slice` (embedded tests).

`elf_load_info_t` in `thread_loader.h` carries load metadata (entry, stack, phdr info) without ABI-specific policy.

---

## Thread duplication

```c
struct Thread_Base* copy_thread(Thread_Base* src_thread, VSpace* vs,
                                u64 return_value);
void run_copied_thread(u64 return_value);
```

Before copy, ensure the source thread’s user context is current when entering from a syscall path (`arch_ctx_refresh` / `arch_ctx_merge_from_src` on the source `Arch_Thread_Context`). **`vs` ownership** 见 § VSpace ownership。

Core does not copy append tail bytes. After attaching `append_hooks` from the source thread, core invokes `dst_thread->append_hooks->copy(dst, src)` when present. Upper layers build dst append state (shared vs fresh heap, inherited scalars, etc.).

| Hook | When core / caller runs it |
|------|----------------------------|
| `thread_append_hooks.init` | `run_elf_program` after load + stack (Path B / `gen_thread_from_elf`); not used by Linux exec today |
| `thread_append_hooks.copy` | `copy_thread` after hooks attached from src |
| `thread_append_hooks.fini` | `del_thread_structure` (before owned resources including `vs` are dropped) |

Pass the hook table via `create_thread`; `copy_thread` inherits `append_hooks` from the source thread.

---

## Teardown

| API | Use |
|-----|-----|
| `delete_thread` | Detach from the run queue and drop the thread ref |
| `add_thread_to_manager` | Attach to a `Task_Manager` and set `ready` |
| `del_thread_from_manager` | Unlink from the scheduler ring |

`delete_thread` order: `del_thread_from_manager` (sched ring) → `ref_put`. Last ref runs `del_thread_structure`: `append_hooks->fini` first, then drop `vs` / drain IPC/kstack.

`del_thread_from_manager` returns `-E_REND_AGAIN` when the thread is still `tm->current_thread` on the owner CPU (checked under `sched_lock`). `delete_thread` retries that case: on the owner CPU it calls `schedule(tm)`; remote callers spin-retry while the owner’s exit path runs `schedule`.

Follow usual refcount and cross-CPU teardown discipline for the calling environment.

---

## Scheduler

| API | Role |
|-----|------|
| `schedule` / `choose_schedule` | Run next ready thread on this CPU |
| `thread_set_status` | Block (e.g. on port wait) |
| `add_thread_to_manager` | Attach thread to a manager and set `ready` |

`schedule(percpu(core_tm))` 仅在 **owner CPU** 调用。AS 切换与 `tlb_cpu_mask` 见 § VSpace ownership · [`memory.md`](memory.md) §0.6。

IPC receive paths typically block until a message is available; see [`ipc.md`](ipc.md).

---

## See also

- [`GUIDE.md`](GUIDE.md)
- [`memory.md`](memory.md) — `VSpace`
- [`trap.md`](trap.md) — trap frame layout
