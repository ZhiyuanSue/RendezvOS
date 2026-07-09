# Using core from outside this tree

**Canonical guide for any personality built on RendezvOS core** (kernel modules, servers, or other repos linked against `core/include/`).

Core docs describe **mechanisms only**—not Linux syscall numbers, errno tables, or compat policy. Those belong in the caller’s own documentation.

| If you need… | Read |
|--------------|------|
| API index + headers | [`GUIDE.md`](GUIDE.md) §6–§7 |
| Memory / radix / COW mechanics | [`memory.md`](memory.md) §0–§0.7 |
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

---

## 2. Reading order by task

| Task | Order |
|------|--------|
| New kernel **server** thread | §3.1 → [`ipc.md`](ipc.md) → [`task-thread.md`](task-thread.md) |
| **Exec** / replace user image | §3.2 → [`memory.md`](memory.md) §0.3 |
| **Fork**-style thread + address space | §3.3 → [`memory.md`](memory.md) §0.4 |
| **mmap** / unmap / mprotect-style | [`memory.md`](memory.md) §0.3, §0.7 |
| **Page fault** handler | §3.5 → [`trap.md`](trap.md) |
| **Syscall** dispatch | §3.6 → [`trap.md`](trap.md) |
| **MM COW** policy on caller side | [`memory.md`](memory.md) §0.7 + caller design doc |
| **Kernel sparse buffer** (page cache, large linear window) | [`page-slice.md`](page-slice.md) · §3.7 |

---

## 3. Call patterns

### 3.1 Kernel service thread

1. `DEFINE_INIT` → `gen_thread_from_func(..., entry, name, percpu(core_tm), arg)`.
2. `port_table_lookup(global_port_table, name)` or `create_message_port` + `register_port`.
3. Loop: `recv_msg(port)` → `dequeue_recv_msg()` → dispatch → `kmsg_create` → `enqueue_msg_for_send` → `send_msg`.
4. Optional non-blocking delivery (timers, cancel): `enqueue_msg_for_send` then `ipc_try_send_msg(port)` — see [`ipc.md`](ipc.md).
5. Shutdown: stop loop, `unregister_port`, `delete_thread`.

Details: [`ipc.md`](ipc.md).

### 3.2 Replace user image (same `VSpace`)

1. `vspace_clear_user_mappings(vs, &percpu(Map_Handler), true)` — obligations: [`memory.md`](memory.md) §0.5.
2. Populate a `page_slice` (create + `page_slice_insert_page`, or compat `linux_page_slice_copy_from_kva`).
3. `load_elf_to_vs(slice, vs, &max_end)`.
3. `generate_user_stack(vs)`.
4. Caller lays out argv/env on user stack (policy).
5. If returning from **syscall**: `arch_ctx_refresh` if needed → `arch_syscall_set_user_return(tf, ctx, entry, sp, ret)`. (TLS via `arch_set_user_tls_base` when needed).

Use path A on the in-flight `trap_frame`; do not invent a separate “first entry” jump if already in syscall context.

### 3.3 Fork-style thread

1. Optional new AS: `clone_vspace(parent, &child, flags)` — [`memory.md`](memory.md) §0.4.
2. In syscall context: `arch_ctx_refresh` / `arch_ctx_merge_from_src` on parent before `copy_thread`.
3. `copy_thread(parent, child_task, child_ret, append_len)`; child may `run_copied_thread(child_ret)`.

### 3.4 Duplicate address space (no new ELF)

1. `clone_vspace(src, &dst, flags)`.
2. L0 lock → walk with `vmm_radix_tree_find_first_occupied_interval` → adjust via `mm_user_utils_*` or radix bind/unbind ([`memory.md`](memory.md) §0.7).
3. `register_vspace(dst, root_vs, id)` when appropriate.

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
- [ ] MM paths hold L0 before `mm_user_utils_*` ([`memory.md`](memory.md) §0.2)
- [ ] IPC send/recv uses `enqueue_msg_for_send` / `dequeue_recv_msg` ([`ipc.md`](ipc.md))
- [ ] Syscall return uses `arch_syscall_set_user_return` when on syscall path
- [ ] Documented caller-specific policy in **caller** docs, not in `core/docs/`

---

## Changelog

| Date | Change |
|------|--------|
| 2026-05 | Created; consolidated external-caller material from repo upper-layer docs |
