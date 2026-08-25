# Using core from outside this tree

**Canonical guide for any personality built on RendezvOS core** (kernel modules, servers, or other repos linked against `core/include/`).

Core docs describe **mechanisms only**—not Linux syscall numbers, errno tables, or compat policy. Those belong in the caller’s own documentation.

| If you need… | Read |
|--------------|------|
| API index + headers | [`GUIDE.md`](GUIDE.md) §6–§7 |
| Memory / radix / COW mechanics | [`memory.md`](memory.md) §0–§0.8 |
| Kernel sparse page index (page_slice) | [`page-slice.md`](page-slice.md) |
| Threads / ELF / fork primitives | [`task-thread.md`](task-thread.md) |
| IPC ports + messages | [`ipc.md`](ipc.md) · design [`lockfree-ipc.md`](lockfree-ipc.md) |
| Traps + syscall hook | [`trap.md`](trap.md) |
| Repo-wide doc map (compat / AI) | [`../../doc/README.md`](../../doc/README.md) |

---

## 1. Rules

1. **Reuse** APIs in [`GUIDE.md`](GUIDE.md) §6—do not reimplement ports, `copy_thread`, radix, or syscall-return helpers.
2. **Respect boundaries** ([`GUIDE.md`](GUIDE.md) §3): core supplies scheduler, `VSpace`, IPC, trap frame helpers; callers supply ABI policy.
3. **Per-CPU access**: `percpu(core_tm)`, `percpu(current_vspace)`, `&percpu(Map_Handler)` ([`task-thread.md`](task-thread.md)).
4. **SMP / teardown**: follow your tree’s invariant doc; core does not duplicate it here.
5. **Changing `core/` code** requires maintainer approval; extend **this doc** when you depend on new public APIs.
6. **VSpace ownership:** 见 §3.0；细则 [`task-thread.md`](task-thread.md) § VSpace ownership，`register_vspace` 见 [`memory.md`](memory.md) §0.8。

---

## 2. Reading order by task

| Task | Order |
|------|--------|
| New **user or kernel** thread (ownership rules) | §3.0 → [`task-thread.md`](task-thread.md) § VSpace ownership |
| New kernel **server** thread | §3.0 → §3.1 → [`ipc.md`](ipc.md) → [`task-thread.md`](task-thread.md) |
| **Exec** / replace user image | §3.2 → [`memory.md`](memory.md) §0.3 |
| **Fork**-style thread + address space | §3.3 → [`memory.md`](memory.md) §0.4 |
| **mmap** / unmap / mprotect-style | [`memory.md`](memory.md) §0.3, §0.7 |
| **Page fault** handler | §3.5 → [`trap.md`](trap.md) |
| **Syscall** dispatch | §3.6 → [`trap.md`](trap.md) |
| **MM COW** policy on caller side | [`memory.md`](memory.md) §0.7 + caller design doc |
| **Kernel sparse buffer** (page cache, large linear window) | [`page-slice.md`](page-slice.md) · §3.7 |

---

## 3. Call patterns

### 3.0 Thread creation & VSpace ownership

**Contract (all personalities):**

1. **`vs` is never NULL** at `create_thread` / `copy_thread`.
2. Caller holds exactly **one live ref** to pass in (from `create_vspace`, `clone_vspace`, or `ref_get` / `ref_get_not_zero`).
3. Successful call **transfers** that ref to `thread->vs` — caller must not `ref_put` it afterward.
4. **`register_vspace(vs, &root_vspace)`** after `create_vspace` / `clone_vspace`, before the thread owns the ref (RB key = `vspace_root_addr`; no vspace id field).
5. Mark user threads with **`THREAD_FLAG_USER`** only when `vs` is a **user** address space (not `&root_vspace`).

| Goal | Pattern |
|------|---------|
| Kernel server / idle-style thread | `gen_thread_from_func` (gets root, then `create_thread`) |
| First user thread from embedded ELF | `gen_thread_from_elf` (see §3.2b) |
| Fork-style child | `clone_vspace` → `register_vspace`（新建子树）→ `ref_get` 若共享父 AS → `copy_thread`（§3.3） |
| Exec in place | Keep thread + `vs`; §3.2 Path A |

Schedule / teardown AS 行为：[`task-thread.md`](task-thread.md) § VSpace ownership · [`memory.md`](memory.md) §0.6。

### 3.1 Kernel service thread

1. `DEFINE_INIT` → `gen_thread_from_func(..., entry, name, percpu(core_tm), arg)`.
2. `thread_lookup_port(name)` (cached) or `port_table_lookup(global_port_table, name)` (cold) or `create_message_port(name, hooks)` + `register_port`.
   - Pass **`NULL`** hooks for ports that need no policy tail / gates (most servers today).
   - Optional **`port_append_hooks_t`** on create: FAM `append_port_info[]` + `init`/`fini` + `ops_allow` admission/visibility. Full gate table, lookup APIs, and tests: [`ipc.md`](ipc.md) §11.
3. Loop: `recv_msg(port)` → `dequeue_recv_msg()` → dispatch → `kmsg_create` → `enqueue_msg_for_send` → `send_msg`.
4. Optional non-blocking delivery (timers, cancel): `enqueue_msg_for_send` then `ipc_try_send_msg(port)` — see [`ipc.md`](ipc.md).
5. Shutdown: stop loop, `unregister_port`, `delete_thread`.

Details: [`ipc.md`](ipc.md).

### 3.2 Replace user image (same `VSpace`)

1. `vspace_clear_user_mappings(vs, &percpu(Map_Handler), true)` — obligations: [`memory.md`](memory.md) §0.5.
2. Populate a `page_slice` (create + `page_slice_insert_page`, or compat `linux_page_slice_copy_from_kva`).
3. `load_elf_to_vs(slice, vs, &max_end)`.
4. `generate_user_stack(vs)`.
5. Caller lays out argv/env on user stack (policy).
6. If returning from **syscall**: `arch_ctx_refresh` if needed → `arch_syscall_set_user_return(tf, ctx, entry, sp, ret)`. (TLS via `arch_set_user_tls_base` when needed).

Use path A on the in-flight `trap_frame`; do not invent a separate “first entry” jump if already in syscall context.

### 3.2b Spawn user ELF with no FS (incbin harness)

```c
error_t gen_thread_from_elf(Thread_Base** out,
                            const thread_append_hooks_t* hooks,
                            struct page_slice* slice);
```

Creates a user `VSpace`, `create_thread(run_elf_program, …)` (takes ownership of vs), maps the stack, sets `THREAD_FLAG_USER`, and `add_thread_to_manager`. The new thread’s body loads the image and Path-B drops to user; optional `hooks->init` gets `elf_load_info_t`. Core does not destroy `@p slice`.

### 3.3 Fork-style thread

1. Optional new AS: `clone_vspace(parent_vs, &child_vs, flags)` → `register_vspace(child_vs, &root_vspace)` when the child gets a new tree — [`memory.md`](memory.md) §0.4.
2. In syscall context: `arch_ctx_refresh` / `arch_ctx_merge_from_src` on parent before `copy_thread`.
3. `copy_thread(parent, child_vs, child_ret)` takes ownership of `child_vs`; then `add_thread_to_manager(percpu(core_tm), child)`. Child may `run_copied_thread(child_ret)`.
   To share a parent AS: `ref_get(&parent->vs->refcount)` then pass that ref. Do not `ref_put` after successful `copy_thread`.
   Kernel-only threads: use `gen_thread_from_func` (§3.1); do not pass NULL `vs`.

### 3.4 Duplicate address space (no new ELF)

1. `clone_vspace(src, &dst, flags)`.
2. L0 lock → walk with `vmm_radix_tree_find_first_occupied_interval` → adjust via `mm_user_utils_*` or radix bind/unbind ([`memory.md`](memory.md) §0.7).
3. `register_vspace(dst, &root_vspace)` before any thread takes ownership of `dst`.
4. Pass the live ref into `create_thread` or `copy_thread` (§3.0).

### 3.5 Page fault / trap handler

```c
register_fixed_trap(TRAP_CLASS_PAGE_FAULT, handler, IRQ_NEED_EOI);
```

In the handler: `arch_populate_trap_info(tf, &info)`; use `trap_class` and `info.fault_addr` / `info.is_write` ([`trap.md`](trap.md)). Lazy fill: `mm_user_utils_fill_page_with_exist_range` ([`memory.md`](memory.md) §0.7).

### 3.6 Syscall dispatch

Provide a **strong** symbol overriding core’s weak default:

```c
void syscall(struct trap_frame* syscall_ctx);  /* core/kernel/system/syscall.c */
```

Read `ARCH_SYSCALL_*` macros in `arch/*/trap/trap.h`. Return to user via `arch_syscall_*` ([`trap.md`](trap.md), [`task-thread.md`](task-thread.md)).

### 3.7 Kernel sparse page index (`page_slice`)

For **kernel-only** buffers indexed by file page offset (not user `VSpace`). Full contract: [`page-slice.md`](page-slice.md).

1. Caller allocates each **content page** with `percpu(kallocator)->m_alloc(..., PAGE_SIZE)`.
2. `page_slice_create(append_info_size, logical_byte_size)`.
3. `page_slice_insert_page(slice, pgoff, kva, flags)` — default flags `0`: slice **owns** kva and `m_free`s on remove/destroy.
4. `page_slice_copy_to_buffer` / `page_slice_copy_to_user` / `page_slice_copy_to_slice` — see `page_slice_copy.h` (composed on lookup/insert; not in `page_slice.c`).
5. `page_slice_lookup(slice, pgoff)` → `entry->kernel_virtual_address` (+ `PAGE_SLICE_IN_PAGE_OFF` for byte offsets).
6. Evict: `page_slice_remove_page`; teardown: `page_slice_destroy(&slice)` or `page_slice_set_size(&slice, 0)`.

Populate slices from file images in the **caller** (e.g. compat `linux_page_slice_copy_from_kva`, or lazy `insert_page` per page). Do **not** alias foreign kva into slice slots from core.

### 3.8 PMM reclaim (optional, per zone / per `struct pmm`)

When that pmm cannot satisfy `pmm_alloc`:

```c
pmm_set_reclaim_hook(zone->pmm, my_reclaim);

bool my_reclaim(struct pmm *pmm, size_t need_pages, unsigned attempt)
{
        /* Free/swap/kill for *this* pmm/zone only. Use need_pages + attempt. */
        if (/* give up */)
                return false;
        return true; /* alloc will try again */
}
```

No second hook: **true = retry, false = give up**. Core also caps at `PMM_RECLAIM_MAX_ATTEMPTS`. May `pmm_free`; **must not** `pmm_alloc`. Unset hook ⇒ fail immediately.

### 3.8a Boot PMM zones (weak `configure_pmm_zones_hook`)

`ZONE_NR_MAX` is compile-time capacity; `nr_mem_zones` is the active compact prefix of `mem_zones[]`. Full contract: [`memory.md`](memory.md) §2.4.1.

**Default (weak):** one `ZONE_NORMAL` = all available RAM + `buddy_pmm`.

**Override:** strong `configure_pmm_zones_hook(avail_lo, avail_hi)` that sets `nr_mem_zones` and `mem_zones[i].{lower_addr,upper_addr,pmm}` (static `struct pmm*` only). Invoked from `phy_mm_init` → `pmm_configure_zones` **before** `split_pmm_zones`. Not initcall / compat `DEFINE_INIT`. Illegal plan → core falls back to default.

Callers that need a non-NORMAL pool select that zone’s `pmm` explicitly; most of core still uses `mem_zones[ZONE_NORMAL].pmm`.

### 3.9 Soft IPI doorbell (`smp/ipi.h`) — shipped

One HW IPI line per arch; many logical reasons share it (per-CPU pending bits + handler table). Public surface is complete for callers; x86 TLB flush already uses it.

```c
#include <rendezvos/smp/ipi.h>

static void example_ipi_fn(void) { /* IRQ context: flag / work; no sleep */ }

ipi_id_t ipi;
smp_ipi_register(&ipi, example_ipi_fn); /* once: logical table row */
smp_ipi_send(target_cpu, ipi);          /* pending bit + arch HW send */
```

Each CPU: `smp_ipi_init()` → `arch_smp_ipi_init(dispatch)` after the interrupt controller is ready. Do **not** call `APIC_send_IPI` / `gic.send_sgi` from outside `arch/`. Table rows: `RENDEZVOS_SMP_IPI_MAX` in `limits.h` (not HW vector count). Same shape as future riscv SSIP. HW vector state: see §3.10.

### 3.10 IRQ vector use + alloc pool (`trap/trap.h`) — shipped

Per-CPU `irq_vector[]`. Ownership is one bit in `irq_attr`: **`IRQ_VEC_USED`**
(taken vs free). EOI and other flags share the same word (`IRQ_NEED_EOI`, …).

| Action | API |
|--------|-----|
| Mark ids USED on one CPU | `irq_vector_reserve_range_for_cpu` |
| Mark ids USED on all CPUs | `irq_vector_reserve_range_for_all_cpus` |
| Publish alloc window `[lo, hi]` | `irq_vector_set_alloc_pool` |
| Take / release a pool id (all CPUs) | `irq_vector_alloc` / `irq_vector_free` |
| Install handler (all CPUs; id must be USED) | `register_irq_handler` |

Boot: each `arch_start_core` → `init_interrupt` → `arch_init_irq_vector_state`
(reserves this CPU’s core vectors, then `set_alloc_pool`). Arch constants:
`ARCH_IRQ_VEC_*` in arch `trap.h`.

Device / upper-layer path:

```c
u32 vec;
irq_vector_alloc(&vec);
register_irq_handler((int)vec, fn, IRQ_NEED_EOI);
/* … */
irq_vector_free(vec);
```

Do not pick bare vector numbers outside the arch reserve set / alloc pool.

### 3.12 CPU affinity（线程，创建时绑核）

| Need | API |
|------|-----|
| 目标 CPU 是否上线 | `cpu_id_is_online(cpu)` |
| 目标 run queue | `task_manager_for_cpu(cpu)` |
| 查询线程在哪颗 CPU 排队 | `thread_owner_cpu(thread)` |
| 已有线程挂到指定 CPU | `add_thread_to_cpu(thread, cpu)` |
| 新建并绑核 | `gen_thread_from_func(..., task_manager_for_cpu(cpu), ...)` |
| 设置线程名（拷贝） | `thread_set_name_with_copy(name, thread)` |

当前 CPU：`gen_thread_from_func(..., percpu(core_tm), ...)`。**无**运行期迁移 API。

设备 IRQ affinity **不在本节**（见 [`trap.md`](trap.md)；portable API 仍为 backlog）。细节与测例：[`task-thread.md`](task-thread.md)。

---

## 4. `error_t` (caller mapping)

From `rendezvos/error.h`:

| Value | Typical meaning |
|-------|-----------------|
| `REND_SUCCESS` (0) | OK |
| `-E_IN_PARAM` | Invalid argument |
| `-E_REND_NO_MSG` | IPC: no message (retry) |
| `-E_REND_AGAIN` | IPC: peer exiting / retry |
| `-E_REND_NOFOUND` | Lookup miss |
| `-E_RENDEZVOS` | Generic failure |

Map to the caller’s ABI (e.g. negative errno). Core does not set personality `errno`.

---

## 5. When to call core directly vs message a server

| Prefer **direct core** | Prefer **IPC server** |
|------------------------|------------------------|
| Page map/unmap in current thread’s `VSpace` | Global serial policy (IDs, registries) |
| `schedule` / block on port in known thread | Avoid lock-order cycles across many subsystems |
| Radix + `mm_user_utils` on hot path | Work that must not run on caller’s CPU stack |

Mechanism choice is **caller architecture**; core does not mandate servers.

---

## 6. Checklist before shipping caller code

- [ ] Uses only public headers under `rendezvos/` + required `arch/*` hooks
- [ ] No duplicate of IPC / `copy_thread` / radix orchestration already in §6
- [ ] `create_thread` / `copy_thread`: non-NULL `vs`, ownership transfer understood (§3.0); user threads use user `vs` + `THREAD_FLAG_USER`
- [ ] New user `VSpace`: `register_vspace(vs, &root_vspace)` before thread owns ref
- [ ] MM paths hold L0 before `mm_user_utils_*` ([`memory.md`](memory.md) §0.2)
- [ ] IPC send/recv uses `enqueue_msg_for_send` / `dequeue_recv_msg` ([`ipc.md`](ipc.md))
- [ ] Syscall return uses `arch_syscall_set_user_return` when on syscall path
- [ ] Documented caller-specific policy in **caller** docs, not in `core/docs/`

---

## Changelog

| Date | Change |
|------|--------|
| 2026-05 | Created; consolidated external-caller material from repo upper-layer docs |
| 2026-08 | §3.9 soft IPI shipped (`smp_ipi_register` / `send` / `init`) |
| 2026-08 | §3.10 IRQ vectors: `IRQ_VEC_USED` + alloc pool (`trap/trap.h`) |
| 2026-08 | Thread + VSpace model: §1 rule 6 / §3.0 ownership ([`task-thread.md`](task-thread.md)) |
| 2026-08-25 | §3.12 创建时线程 affinity 落地（DONE #72）；IRQ affinity 仍见 trap.md |
| 2026-08-23 | §3.12 初稿（曾与 IRQ 待办并列） |
| 2026-08 | Core rename: `thread.h`, `thread_arch.h`, `thread_boot.c`, `Arch_Thread_Context` |
