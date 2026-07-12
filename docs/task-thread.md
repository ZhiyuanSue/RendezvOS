# Tasks, threads, and ELF loading

Core scheduling units and program load helpers.

External callers: [`USING_CORE.md`](USING_CORE.md) · API index: [`GUIDE.md`](GUIDE.md) §6

---

## Runtime context（当前 CPU / 线程）

调度与 IPC 原语假定在 **正确的 CPU** 上、且能解析 **当前线程**。

| Need | API | Header |
|------|-----|--------|
| Current thread | `get_cpu_current_thread()` | `task/tcb.h` |
| Current task (`Tcb_Base`) | `get_cpu_current_task()` | `task/tcb.h` |
| This CPU’s scheduler | `percpu(core_tm)` → `Task_Manager*` | `task/tcb.h`, `smp/percpu.h` |
| Running thread in TM | `percpu(core_tm)->current_thread` | `task/tcb.h` |
| Active address space on CPU | `percpu(current_vspace)` | `mm/vmm.h` |
| Page-table helper on CPU | `&percpu(Map_Handler)` | `mm/map_handler.h` |

`core_tm` 是 per-CPU 的 `Task_Manager` 模板变量；**始终**通过 `percpu(core_tm)` 访问，不要把它当全局单例指针使用。

`gen_thread_from_func(..., tm, ...)` 的 `tm` 通常为 `percpu(core_tm)`。跨 CPU 操作其他 CPU 的 run queue 或 per-CPU 结构需要项目约定的 SMP 同步（见仓库协作文档中的 teardown 规则）。

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
| `Task_Manager` | `task/tcb.h` | Per-CPU scheduler and run queues |
| `Tcb_Base` | `task/tcb.h` | Address space (`VSpace`) and thread group |
| `Thread_Base` | `task/tcb.h` | Schedulable thread (kernel or user) |
| `Arch_Task_Context` | `arch/*/tcb_arch.h` | Saved registers, user SP, TLS fields |

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
| `gen_task_from_elf` | New task + thread + load; runs @c thread_append_hooks.init |
| `run_elf_program` | Load into an existing `VSpace` and run |
| `load_elf_to_vs` | Map ELF PT_LOAD into a `VSpace` (preferred) |

`elf_load_info_t` in `thread_loader.h` carries load metadata (entry, stack, phdr info) without ABI-specific policy.

---

## Thread duplication

```c
struct Thread_Base* copy_thread(Thread_Base* src_thread, Tcb_Base* target_task,
                                u64 return_value);
void run_copied_thread(u64 return_value);
```

Before copy, ensure the source thread’s user context is current when entering from a syscall path (`arch_ctx_refresh` / `arch_ctx_merge_from_src` on the source `Arch_Task_Context`).

Core does not copy append tail bytes. After attaching `append_hooks` from the source thread, core invokes `dst_thread->append_hooks->copy(dst, src)` when present. Upper layers build dst append state (shared vs fresh heap, inherited scalars, etc.).

For task-level append, upper layers invoke `dst_task->append_hooks->copy(dst, src)` after initializing static proc-append fields.

| Hook | When core / caller runs it |
|------|----------------------------|
| `task_append_hooks.init` | `new_task_structure` after kallocator alloc |
| `task_append_hooks.copy` | Caller after task duplication |
| `task_append_hooks.fini` | `delete_task` |
| `thread_append_hooks.init` | `run_elf_program` after PT_LOAD + user SP (@p elf_info set) |
| `thread_append_hooks.copy` | `copy_thread` after hooks attached from src |
| `thread_append_hooks.fini` | `del_thread_structure` |

Pass hook tables via `new_task_structure` / `create_thread` / `gen_task_from_elf`; `copy_thread` inherits `append_hooks` from the source thread.

---

## Teardown

| API | Use |
|-----|-----|
| `delete_thread` | Remove one thread |
| `delete_task` | Remove task and its threads |
| `del_*_from_manager` | Detach from scheduler structures |

`delete_thread` order: `del_thread_from_manager` (sched ring) → `del_thread_from_task` → `ref_put`.

`del_thread_from_manager` returns `-E_REND_AGAIN` when the thread is still `tm->current_thread` on the owner CPU (checked under `sched_lock`). `delete_thread` retries that case: on the owner CPU it calls `schedule(tm)`; remote callers spin-retry while the owner’s exit path runs `schedule`.

Follow usual refcount and cross-CPU teardown discipline for the calling environment.

`schedule` clears `vs->tlb_cpu_mask` and drops a vspace ref when switching between user threads with different `VSpace` objects. User → kernel/idle transitions do not clear the mask; `del_vspace` then waits until `tlb_cpu_mask` is zero (see [`memory.md`](memory.md)).

---

## Scheduler

| API | Role |
|-----|------|
| `schedule` / `choose_schedule` | Run next ready thread on this CPU |
| `thread_set_status` | Block (e.g. on port wait) |
| `thread_join` | Attach thread to task + per-CPU manager and set `ready` (not a blocking wait-for-exit) |

IPC receive paths typically block until a message is available; see [`ipc.md`](ipc.md).

---

## See also

- [`GUIDE.md`](GUIDE.md)
- [`memory.md`](memory.md) — `VSpace`
- [`trap.md`](trap.md) — trap frame layout
