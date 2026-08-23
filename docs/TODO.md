# Core 待办（冻结收尾）

归档：[`archive/TODO_DONE.md`](archive/TODO_DONE.md)（含「明确不做」）  
接口：[`GUIDE.md`](GUIDE.md) §6 · [`USING_CORE.md`](USING_CORE.md)

**规则：** 完成一项 → 写入 DONE 归档，**从本文件删掉该项**；本文件只列 **core 内**未完成工作。

**不在此列：** console / UART RX / `uart_server` / log handoff 等 **compat 与上层**事项 → [`doc/linux_compat/NEXT_PLAN.md`](../../doc/linux_compat/NEXT_PLAN.md)、[`log.md`](log.md)、[`STDIO_SHIM.md`](../../doc/linux_compat/STDIO_SHIM.md)。

---

## 怎么读这张单

| 档 | 含义 |
|----|------|
| **B** | 冻结前尽量完成 |
| **C** | 非冻结；有需要再开 |

**冻结前主线：** Doxygen 随手（#50）。

---

## B. 冻结前

### 关键 API Doxygen（#50）

- 冻结对外承诺的入口：`port.h`、`pmm_set_reclaim_hook`、`configure_pmm_zones_hook`、trap vector alloc 等。
- 改到哪个头就补 `@brief` / 参数约定；与 `GUIDE.md` §6、`USING_CORE.md` 一致。

---

## C. 非冻结（core 内远期）

| 项 | 说明 |
|----|------|
| #22 CPU topology | `cpu_topology.h` 等有头未接线 |
| #43 aarch64 DTB vs PCI | 两套节点描述并存 |
| PCI 使能 / IRQ / BAR | `pci_ops.c` 等 |
| aarch64 `boot.S` EL3/SPSR/ELR | 仅从 EL3 进内核时需要 |
| lockfree-ipc §8.3–8.5 | 批量 / 广播 / 调度感知 IPC |
| 真 capability / 本地 port handle | 上层策略模型 |
