# Core 待办（冻结收尾）

归档：[`archive/TODO_DONE.md`](archive/TODO_DONE.md)（含「明确不做」）  
接口：[`GUIDE.md`](GUIDE.md) §6 · [`USING_CORE.md`](USING_CORE.md) · [`log.md`](log.md)

**规则：** 完成一项 → 写入 DONE 归档，**从本文件删掉该项**；本文件只列未完成工作。

---

## 怎么读这张单

| 档 | 含义 |
|----|------|
| **B** | 冻结前尽量完成 |
| **C** | 非冻结；有需要再开 |

**冻结前主线：** UART `getc` + log 最小收尾（#37 / #38）。

**范围：** 专用 Log IPC server（#46）**不算**冻结主线，与 B.2 拆开；`log_put_byte` 现仍同步 `uart_putc`，early / panic 须保留直写（见 [`log.md`](log.md)）。

---

## B. 冻结前

### 1. UART 轮询 `getc`（16550 / PL011）

**现状**

- 门面：`uart_getc()` → `uart_16550A_getc` / `uart_pl011_getc`（`modules/driver/uart/`）。
- 两实现均为 **`return 0` 空壳**，未轮询 LSR / FR。
- `putc` 已忙等 THR / TXFF；收包应对称为 **轮询 RX 就绪再读**。**不要**开 RX IRQ（x86 需 IOAPIC，非冻结项）。

**要做**

- **16550**：等 `LSR` data-ready，读 `RBR`；约定无数据行为（冻结最小集：阻塞轮询一字节；或另提供非阻塞变体——二选一并写进头文件）。
- **PL011**：等 `FR` 非 RXFE，读 `DR`（错误位按驱动惯例处理）。
- 不改中断模型；不大改 console 子系统。

**验收**

- QEMU 串口：外部输入一字节，`uart_getc` 能读到。

---

### 2. Log 最小收尾（#37 / #38）

背景：[`log.md`](log.md)。冻结目标为 **最小可用** 前后端分离，**不要求**多核 IPI 汇聚刷屏。

**现状**

- `struct log_buffer` / `LOG_BUFFER` 有骨架；`log_init` 初始化 buffer 描述符。
- 热路径仍 **同步直写**：`log_put_byte` → `uart_putc`；`printk` 几乎未当环形缓冲用。
- `log_init` 调 `CONSOLE_CLEAN_SCREEN(&X86_CHAR_CONSOLE)`；`pr_*` 绑 x86 VGA → 非 x86 被拖进 x86 头。

**要做**

1. **#37**：VGA / `X86_CHAR_CONSOLE` 与 `log_init`、通用 `pr_*` 解耦（arch 可选 sink 或 `#ifdef` / 弱符号）；log 只认字节输出抽象。
2. **#38（缩小版）**：`pr_*` 写 `LOG_BUFFER`，提供 flush 到 uart（`log_flush` 或满页时刷）；**不必**多核 IPI 汇聚。
3. early / panic：**继续允许直写 uart**。

**不在本项**

- #46 专用 Log IPC server（见 C.4）。
- 多核 log 汇聚（冻后）。

**验收**

- 非 x86 构建不因 log 强依赖 VGA 头而别扭（或 x86-only 路径隔离清楚）。
- 常规 `pr_*` 进 buffer 并能刷到串口；panic / early 仍能出字。

---

### 3. 关键 API Doxygen（#50）

- 冻结对外承诺的入口：`port.h`、`pmm_set_reclaim_hook`、`configure_pmm_zones_hook`、trap vector alloc 等。
- 改到哪个头就补 `@brief` / 参数约定；与 `GUIDE.md` §6、`USING_CORE.md` 一致。
- 不必全仓库扫一遍。

---

## C. 非冻结

### 4. Log / 输出走专用 IPC server（#46）

- 与上层 uart_server 同向；core early / panic 仍直写。
- 未拍板：全部 `pr_*` 走 IPC，还是仅用户 console。
- **依赖**：可用 `getc`（B.1）；x86 RX IRQ 另需 IOAPIC（远期）。

### 5. 其它远期

| 项 | 说明 |
|----|------|
| #22 CPU topology | `cpu_topology.h` 等有头未接线 |
| #43 aarch64 DTB vs PCI | 两套节点描述并存 |
| PCI 使能 / IRQ / BAR | `pci_ops.c` 等 |
| aarch64 `boot.S` EL3/SPSR/ELR | 仅从 EL3 进内核时需要 |
| lockfree-ipc §8.3–8.5 | 批量 / 广播 / 调度感知 IPC |
| 真 capability / 本地 port handle | 上层策略模型 |

---

## 建议顺序

1. UART `getc`  
2. Log #37 / #38  
3. Doxygen 随手  
4. 冻后再开 #46  
