# 日志与 UART 控制台

v0.1 · 2026-08-29

本篇覆盖：`modules/log/log.c`、`include/modules/log/log.h`、`modules/driver/uart/uart.c`、`uart_16550A.c`、`uart_pl011.c`、`modules/driver/x86_char_console/char_console.c`。

SMP 下 MCS 见锁篇；`cmain` early 时序见启动总览；aarch64 PL011 基址 early map 见平台启动 / DTB 篇。compat `write(1/2)` 可汇合到 `log_put_locked`。

---

## 1. 概述

Boot/调试需要 **同步、不依赖 IPC** 的字符输出——MM/调度/port 起来之前就能打字。

直写 UART；不是 syslog，也不是 console server。

主路径：`pr_*` / `printk` → 格式化 → `uart_putc`。后端由编译宏 **`_UART_16550A_`** 或 **`_UART_PL011_`** 选择。  
x86 另有 VGA 文本层——但 **`log_put_byte` 从不写显存**：`pr_*` 只改 VGA **颜色**，正文仍只在串口（QEMU `-serial stdio`）。「只盯 VGA」会看不到日志。

Ring buffer / uart_server / `/dev/console` 留给上层；panic 仍允许直写。

---

## 2. 目标与边界

**提供：** printf 子集；级别过滤；UART open/putc/getc；x86 VGA 颜色辅助；SMP 下 `pr_*` / `log_put_locked` 加锁。

**不做：** termios、pty、异步 flush 线程、串口 DMA、多 sink 框架。

---

## 3. 分层与调用方

| 场景 | 用法 |
|------|------|
| 任意内核 | `#include <modules/log/log.h>`，`pr_err` / `pr_info`… |
| Early / panic 友好 | `print(...)` = `printk(..., LOG_OFF)`——级别过滤下 LOG_OFF 仍可出 |
| compat stdout | `log_put_locked(buf,len)`（整段 MCS） |
| Boot | `cmain`：`uart_open(...)` → `log_init(log_level)` → … |

**纠正：** `stdio.h` 只有未实现的 `printf` 声明；真正入口是 **`log.h`**。

---

## 4. 数据结构与不变量

### 4.1 级别

`LOG_OFF … LOG_DEBUG`。默认由 `_LOG_*_` 编译宏；否则 `LOG_OFF`。  
`printk`：仅当 **`msg_level <= log_level`** 输出。  
现行 config 多为 `_LOG_DEBUG_` / `_LOG_INFO_`。

### 4.2 锁

SMP：`COLOR_SET`/`CLR`（`pr_*`）与 `log_put_locked` 用 MCS：`log_spin_lock_ptr` + `percpu(log_spin_lock)`。  
**`printk`/`print` 本身不加锁**——无 `pr_*` 包装的并发 `print` 可交错。

### 4.3 printf 子集

`d/i/u/x/X/o/p/c/s` + 部分长度/flags；无 `*`/`n`/浮点；未知 specifier 原样吐 `%X`。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `log.c` / `log.h` | `printk`/`pr_*`/`print`/`log_put_*` |
| `uart.c` | 编译期分发 |
| `uart_16550A.c` | COM1 I/O；115200 轮询 |
| `uart_pl011.c` | MMIO；可开 RXIM，但 **GIC SPI 仍 mask → getc 仍轮询** |
| `char_console.c` | x86 VGA 0xB8000；颜色 |

`driver.h` 几乎只 include uart——不是驱动框架。

---

## 6. 流程

### 6.1 Boot

```text
cmain:
  uart_open(ROUND_UP(&_end, MIDDLE_PAGE_SIZE))  // aarch64：early map 的虚址；x86：忽略，用 COM1 port
  log_init(log_level)   // 实质多半换行 + 重设当前级别
```

### 6.2 打印

```text
pr_* → COLOR_SET（锁+ANSI[+VGA色]）→ printk(level) → log_put_byte → uart_putc → COLOR_CLR
```

### 6.3 UART

- **16550：** 轮询 THR/RHR；`getc` 阻塞等 LSR。  
- **PL011：** 清 ICR、开 UARTEN|TXE|RXE；RX 无接到 IRQ 向量（DONE #68）。  

`uart_set_color` 发 ANSI；参数命名与宏略错位，常用 `COLOR_SET(0,32,40)` 仍得绿字。

---

## 7. 公开 API

```c
void print(const char *fmt, ...);
void pr_err/pr_info/pr_debug/...(...);  /* 宏 → printk */
void printk(const char *fmt, int msg_level, ...);
void log_init(int level);
void log_put_locked(const char *buf, size_t len);

void uart_open(vaddr base);
void uart_putc(char c);
int uart_getc(void);   /* 无限阻塞轮询 */
```

---

## 8. 多架构

| | UART | VGA |
|--|------|-----|
| x86 | 16550 COM1 port | 有（颜色 only vs log） |
| aarch64 | PL011 MMIO（DTB early map） | 无（颜色宏空） |
| riscv 头 | 有 16550 MMIO 宏 | — |

---

## 9. 测试

间接：全程启动日志。无独立 log 单测。本篇未复测。

---

## 10. 限制与后续

- 同步轮询；SMP 下裸 `print` 不安全。  
- VGA 与正文解耦。  
- 无行规程；`getc` 无 timeout。  
- 上层 uart_server handoff 后 panic 仍可直写。

---

## 11. 变更记录

- 2026-08-29：整篇重做——直写叙述；纠正 stdio.h；VGA 只改色；锁在 pr_*；PL011 无 IRQ；early open 时序。
- 2026-08-27：初稿。
