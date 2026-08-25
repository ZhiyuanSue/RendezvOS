# Core 待办

归档：[`archive/TODO_DONE.md`](archive/TODO_DONE.md)（含「明确不做」与已关闭项）  
接口：[`GUIDE.md`](GUIDE.md) §6 · [`USING_CORE.md`](USING_CORE.md) · [`log.md`](log.md)

**规则：** 完成或关闭一项 → 写入 DONE 归档，**从本文件删掉**；本文件只列未完成工作。

---

## 怎么读这张单

| 档 | 含义 |
|----|------|
| **B** | 值得做、可排期 |
| **C** | 远期；有需要再开 |

**2026-08-25：** 轮询 `uart_getc`（DONE #68）与 **创建时线程 CPU affinity**（DONE #72）已落地并经 maintainer 同意。**无** `core-v0.1-frozen` tag 时，剩余冻前债以本文件 B 档为准；IRQ affinity **不**再挡 freeze（与 IOAPIC 同档远期，见 DONE #14）。

**现行 log（够用即可）：** `printk` / `pr_*` → 同步 `uart_putc`；上层 `write(1/2)` 可走 `log_put_locked`（compat 接线）。见 [`log.md`](log.md)。

---

## B. 可排期

### 1. 中断 CPU affinity（IRQ 绑哪个 CPU 处理）

**现状：** `irq_vector[]` per-CPU；`register_irq_handler` 全核安装。aarch64 `gicd_v2_set_affinity` 仅 arch 内部；x86 IOAPIC 路由空（见 [`interrupt.md`](interrupt.md)、DONE「明确不做」#14）。线程侧创建时绑核见 DONE #72 / [`USING_CORE.md`](USING_CORE.md) §3.12。

**要做：**

1. Portable API（如 `irq_set_affinity(irq_num, cpu_mask)` / `irq_get_affinity`），arch 实现 GIC ITARGETSR、将来 IOAPIC 等。
2. 文档：软件向量 per-CPU 登记 vs 硬件路由目标 CPU 的一致性；SGI / timer / 设备 SPI 分工。
3. 与 [`trap.md`](trap.md)、`register_irq_handler` 关系写清（是否允许仅部分 CPU 装 handler）。

**验收：** aarch64 上可将一 SPI 绑到单 CPU 并实测只在该核进 handler；API 写入 [`USING_CORE.md`](USING_CORE.md)（新小节，勿塞进 §3.12 线程条目）与 [`trap.md`](trap.md)。

---

### 2. 关键 API Doxygen（#50）

- 对外承诺的入口：`port.h`、`pmm_set_reclaim_hook`、`configure_pmm_zones_hook`、trap vector alloc、**已落地的线程 affinity 头注释**（`thread.h`）、将来 IRQ affinity。
- 改到哪个头就补 `@brief` / 参数约定；与 `GUIDE.md` §6、`USING_CORE.md` 一致。
- 不必全仓库扫一遍。

---

## C. 远期

| 项 | 说明 |
|----|------|
| 线程运行期迁移 / affinity mask | create-time 已够（DONE #72）；迁核须先下 CPU 再 `del`+`add`；mask / `gen_thread_from_elf` 绑核变体另开 |
| #22 CPU topology | `cpu_topology.h` 等有头未接线；与 IRQ affinity 相关时可合并 |
| #43 aarch64 DTB vs PCI | 两套节点描述并存 |
| PCI 使能 / IRQ / BAR | `pci_ops.c` 等 |
| aarch64 `boot.S` EL3/SPSR/ELR | 仅从 EL3 进内核时需要 |
| lockfree-ipc §8.3–8.5 | 批量 / 广播 / 调度感知 IPC |
| 真 capability / 本地 port handle | 上层策略模型 |
| UART RX IRQ / IOAPIC | 轮询 `uart_getc` 已有（DONE #68）；IRQ 路由仍见 DONE #14 |
| Log 前后端 / IPC server（#46） | 见 DONE #69–#71；归上层 + panic 直写 |

---

## 建议顺序

1. 中断 affinity（B.1），有设备 IRQ 绑核需求时再开；依赖 GIC / 将来 IOAPIC  
2. Doxygen（B.2）随手补  

---

## Changelog

| 日期 | 变更 |
|------|------|
| 2026-08-25 | 创建时线程 affinity → DONE #72；IRQ affinity 改 B（不挡 freeze）；运行期迁移进 C |
| 2026-08-23 | 关闭 UART/log 冻前主线；曾列 B.1/B.2 affinity 为冻结范围 |
| 2026-08-23 | 初版重构：剩 #50 + C 表 |
