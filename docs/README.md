# Core documentation

| Document | Audience |
|----------|----------|
| **[`USING_CORE.md`](USING_CORE.md)** | **Code outside `core/`** — how to use public APIs (start here) |
| [`GUIDE.md`](GUIDE.md) | In-tree map: layout, §6 API index, §7 headers |
| Topic files below | Deep dives |

**Boundary:** Core only. Compat / AI workflow: [`../../doc/README.md`](../../doc/README.md).

---

## Topic references (deep dives)

| Topic | Document |
|-------|----------|
| Memory (+ §0 caller contract, §0.7 radix) | [`memory.md`](memory.md) |
| Cache / TLB | [`cache&tlb.md`](cache&tlb.md) |
| Tasks / ELF | [`task-thread.md`](task-thread.md) |
| IPC usage | [`ipc.md`](ipc.md) |
| IPC design | [`lockfree-ipc.md`](lockfree-ipc.md) |
| Traps | [`trap.md`](trap.md) |
| IRQ hardware | [`interrupt.md`](interrupt.md) |
| Boot | [`boot.md`](boot.md) |
| SMP | [`smp.md`](smp.md) |
| Timers | [`timer.md`](timer.md) |
| Log | [`log.md`](log.md) |

## Platform / misc

[`x86_sys_ctrl.md`](x86_sys_ctrl.md) · [`little_endian.md`](little_endian.md) · [`ROCm.md`](ROCm.md) · [`hardware/README.md`](hardware/README.md)

## Work tracking

[`TODO.md`](TODO.md) · [`archive/TODO_DONE.md`](archive/TODO_DONE.md)

## Merged stubs (do not edit)

`ARCHITECTURE.md` · `CAPABILITY_INDEX.md` · `HEADERS.md` · `MAINTENANCE.md` · `traps-and-irq.md` · `sync-and-smp.md` → see [`GUIDE.md`](GUIDE.md).
