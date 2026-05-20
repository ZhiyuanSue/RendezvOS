# Core open work items

Active maintainer backlog. Completed history: [`archive/TODO_DONE.md`](archive/TODO_DONE.md).

For public APIs, see [`GUIDE.md`](GUIDE.md) §6.

---

## Trap / IRQ / CPU

1. IDT/GDT: segment types, DPL/CPL/RPL usage in 64-bit mode.
2. 8259A vs APIC coexistence paths.
3. CPUID caching (avoid re-read every query).
10. Page-table isolation domain bits review.
22. CPU topology in `cpu_id` / per-CPU info.
25. Interrupt nesting policy; RT flag for non-nestable timers.

## Memory

15. Per-core stack pages and permissions; map_handler permission change API if needed.
26–28. Allocator cache-line collision tests; per-CPU cache size; kmalloc tuning.
30. Atomic vs non-atomic bitmap in `common/` (header discipline).
31. `memory_zone` parameters instead of hardcoded `ZONE_NORMAL`.
48. Multi-zone allocator selection (not only handler default).
53. Map handler entry refill on failure; fault handler / nexus entry failure paths.

## Platform / drivers

19. ACPI parsing modularization into `modules/`.
39. Multiboot2 path validation under QEMU.
43. aarch64 DTB vs PCI device unification.
46. Log/output via dedicated IPC server process.
52. Port management; function-call → IPC message wrappers.

## Log / user bring-up

37–38. Log buffer + flush policy; decouple VGA early print from log module.
42. User stack argc/argv in standard loader paths.
47. Run core tests as kernel thread (if not already).

## Documentation

50. Doxygen-style comments on public APIs (incremental).

---

*Last reorganized: 2026-05 — done items moved to `archive/TODO_DONE.md`.*
