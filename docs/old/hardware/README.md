# Hardware description assets

Files here are **not** narrative documentation—they support QEMU/platform bring-up.

| File | Use |
|------|-----|
| [`aarch64-virt.dts`](aarch64-virt.dts) | Sample DT for `virt` machine (multi-core notes in [`../smp.md`](../smp.md)) |

When changing CPU count or UART for aarch64 tests, update this DTS and document the QEMU command line in repo test docs.
