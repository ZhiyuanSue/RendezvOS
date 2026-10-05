# 日志与 UART 控制台

v0.1 · 2026-09-27

本篇覆盖：`modules/log/log.c`、`include/modules/log/log.h`、`modules/driver/uart/uart.c`、`uart_16550A.c`、`uart_pl011.c`、各 ISA 的 UART 头（`uart_16550A.h` / `uart_pl011.h` 等）、`modules/driver/x86_char_console/char_console.c`。

SMP 下 MCS 见锁篇；`cmain` 里 `uart_open` → `log_init` 时序见启动总览；aarch64 PL011 early map / `boot_uart_base_addr` 见平台启动与 DTB 篇。

**硬件对照：** 16550A / 8250 兼容 UART（COM1 I/O）；PrimeCell PL011（MMIO）；x86 VGA 文本模式帧缓冲 `0xB8000`。手册名点到为止，寄存器以本仓库读写为准。

---

## 1. 概述

Boot / 调试需要 **同步、不依赖 IPC / 堆 / 调度** 的字符输出——buddy、port、线程都尚未初始化时也要能输出字符。

路径刻意压缩为一条线：`pr_*` / `printk` → 格式化 → `log_put_byte` → `uart_putc` → 硬件。没有 ring、没有 console server、没有「多 sink 框架」。后端由编译宏 **`_UART_16550A_`** 或 **`_UART_PL011_`** 在 `uart.c` 里静态分发。

x86 另有 VGA 文本层（`x86_char_console`），但 **`log_put_byte` 从不 `CONSOLE_PUTC`**：`pr_*` 只经 `SET_CONSOLE_COLOR` 改 VGA **属性字节**；正文只出现在串口（QEMU 典型 `-serial stdio`）。只看虚拟机窗口 VGA 会以为「内核没日志」。

### 1.1 为什么坚持直写

- **早期排障**：`cmain` 第一步就 `uart_open`，随后任何 `prepare_arch` / `phy_mm_init` panic 仍可能输出字符。经 IPC 或线程 drain 做不到这一点。
- **panic 路径**：即便将来链接方做 UART server，core 仍应保留直写——server 崩溃时不能连自检日志都丢失。
- **复杂度边界：** ring + 多核汇聚 + termios 属于用户可见 console 语义，不纳入 v0.1 core。

---

## 2. 目标与边界

**提供：** printf 子集；级别过滤；`uart_open` / `putc` / `getc`；x86 VGA 颜色辅助；SMP 下 `pr_*` / `log_put_locked` 的 MCS（MCS lock，基于链表的可伸缩自旋锁）。

**不做：** termios、pty、异步 flush 线程、串口 DMA、IRQ 驱动的 RX、`/dev/console`、多 sink。

**RX 现状：** `uart_getc` 是**无限阻塞轮询**。PL011 可启用设备侧 RXIM，但 GIC SPI（Shared Peripheral Interrupt，共享外设中断）仍默认 mask（见 GIC 篇）→ CPU 收不到中断线，getc 仍依赖轮询。16550 路径 `IER=0`，完全不使能 UART 中断。

**轮询 vs 中断收发对比：**

| 维度 | 轮询（v0.1 现状） | 中断驱动（未启用） |
|------|------------------|--------------------|
| TX | `putc` 轮询 LSR.THRE / FR.TXFF 后写 | THRE IRQ 触发，ISR 从 TX FIFO drain |
| RX | `getc` 轮询 LSR.DR / FR.RXFE 后读，无限阻塞 | DR IRQ 触发，ISR 读 RBR/DR 推 ring |
| 依赖 | 无 IRQ、无调度、可在 boot 早期用 | 需 IRQ 路由 + GIC unmask + 调度上下文 |
| 延迟 | 取决于调用频率；RX 阻塞会卡死调用方 | 硬件事件即时响应 |
| core 选型理由 | boot/panic 阶段 IRQ 与调度未必就绪 | 留给链接方做 UART server |

v0.1 全程轮询是刻意为之：early/panic 路径不能依赖 IRQ 已注册或调度已初始化。

---

## 3. 分层与调用方

| 场景 | 用法 |
|------|------|
| 任意内核路径 | `#include <modules/log/log.h>`，`pr_error` / `pr_info`… |
| Early / 级别旁路 | `print(...)` = `printk(..., LOG_OFF)`——过滤下 LOG_OFF 仍可输出 |
| 兼容层整段写入 | `log_put_locked(buf, len)`（整段持 MCS，逐字节 `uart_putc`） |
| Boot | `cmain`：`uart_open(ROUND_UP(&_end, MIDDLE_PAGE_SIZE))` → `log_init(log_level)` |

打印入口只允许 **`modules/log/log.h`**。历史上的空壳 `rendezvos/stdio.h` 已删，勿再 `#include`。

---

## 4. 数据结构与不变量

### 4.1 级别

`LOG_OFF … LOG_DEBUG`。默认由 `_LOG_*_` 编译宏注入 `log_level`；否则 `LOG_OFF`。
`printk`：仅当 **`msg_level <= log_level`** 输出。
`log_init`：输出一个 `\n` 并重设 `log_level`（不是清屏）。

### 4.2 锁

SMP：`COLOR_SET` / `COLOR_CLR`（包住 `pr_*`）与 `log_put_locked` 用同一把 MCS：`log_spin_lock_ptr` + `percpu(log_spin_lock)`。
**`printk` / `print` 本身不加锁**——无 `pr_*` 包装的并发 `print` 可交错产生乱字。

### 4.3 printf 子集

`d/i/u/x/X/o/p/c/s` + 部分长度 / flags；无 `*` / `n` / 浮点；未知 specifier 原样输出 `%X`。

### 4.4 颜色参数错位（易踩）

`COLOR_SET(dis_mod, forward_color, backword_color)` 调 `uart_set_color(dis_mod, forward_color)`——把 **显示模式** 和 **前景色** 传入 UART ANSI 的「前景 / 背景」两个形参，**第三参背景色不传入 ANSI**。VGA 侧 `map_color(forward, back)` 用的是后两个参数，顺序正确。常用 `COLOR_SET(0, 32, 40)`：串口侧大致发出 `\033[0;…m` 一类序列；VGA 侧按绿字黑底映射。能用，但命名与 `uart_set_color(forword, backword)` 不对齐——改宏时不要「想当然改动」而不测试。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `log.c` / `log.h` | `printk` / `pr_*` / `print` / `log_put_*` / 级别 |
| `uart.c` | 编译期分发 + `uart_set_color`（ANSI） |
| `uart_16550A.c` | COM1 PIO；115200；轮询 THR/RHR |
| `uart_pl011.c` | MMIO；清 ICR、开 UARTEN\|TXE\|RXE；轮询 FR |
| `char_console.c` | VGA `0xB8000` 文本；颜色 / 滚动；**log 热路径不写字符** |

`driver.h` 几乎只是把 uart 头汇集在一起——不是驱动框架。

---

## 6. 流程

### 6.1 Boot

```text
cmain:
  uart_open(ROUND_UP(&_end, MIDDLE_PAGE_SIZE))
      // aarch64：该 VA == early map 的 PL011 高半窗（boot_uart_base_addr）
      //          uart_pl011_open 只 cast，不再 map
      // x86：参数忽略，固定 COM1 0x3F8
  log_init(log_level)   // '\n' + 设级别
  [HELLO] hello_world()
  …
```

AP **不**再 `uart_open`（BSP 已配置好硬件；每核只共用同一串口，依赖 MCS 串行化打印）。

### 6.2 打印

```text
pr_* → COLOR_SET（SMP 时加锁 + ANSI [+ VGA 色]）
     → printk(level) → log_put_byte → uart_putc
     → COLOR_CLR（解锁）
```

### 6.3 硬件：16550A（x86 / 可选 riscv MMIO）

PC 经典 **COM1** 基址 **`0x3F8`**（`_X86_16550A_COM1_BASE_`），`inb`/`outb` 访问。寄存器复用在同一偏移：LCR.DLAB=1 时 `0/1` 是 DLL/DLM，否则是 RHR/THR 与 IER。

**16550A 寄存器布局（DLAB=0 / DLAB=1 复用，DLAB = Divisor Latch Access Bit，分频锁存访问位）：**

| 偏移 | DLAB=0 | DLAB=1 | 关键位 |
|------|--------|--------|--------|
| `+0` | RBR（读）/ THR（写） | DLL | 8 位数据 |
| `+1` | IER | DLM | bit0=RX-available IRQ, bit1=THRE IRQ, bit2=LSI IRQ |
| `+2` | IIR（读）/ FCR（写） | — | FCR bit0=FE, bit1/2/3=FIFO 触发水位 |
| `+3` | LCR | LCR | bit7=DLAB, bit1:0=字长（11=8N1） |
| `+4` | MCR | MCR | DTR/RTS/OUT2（OUT2 在 PC 上常作 IRQ 使能门控） |
| `+5` | LSR | LSR | bit0=DR（数据就绪）, bit5=THRE（可写）, bit6=TEMT |
| `+6` | MSR | MSR | Modem 状态 |
| `+7` | SCR | SCR | 暂存 |

本仓库只用 `+0`（THR/RBR）、`+1`（IER 置 0）、`+3`（LCR 切 DLAB）、`+5`（LSR 轮询）；FIFO / Modem 线未编程。

`uart_16550A_open`：

1. `IER = 0`（关中断——early 路径不需要 IRQ）。
2. LCR 置 DLAB，写 **DLL=1, DLM=0** → 假定 1.8432 MHz 晶振时 **115200** baud。
3. LCR 清零（8N1 默认配置；未显式写字长位时依赖复位后状态）。

**波特率分频计算：** 16550A 内部基准时钟 1.8432 MHz，波特率 = `1.8432 MHz / (16 × divisor)`，故

```text
  divisor = 1.8432 MHz / (16 × baud)
  115200 baud → divisor = 1   (DLL=1, DLM=0)   ← 本仓库默认
   9600 baud → divisor = 12
```

DLL/DLM 合为 16 位分频值（DLM 高字节，DLL 低字节）。`DLL=1, DLM=0` 即 divisor=1，对应 115200。换波特需重写 LCR.DLAB=1 → DLL/DLM → LCR.DLAB=0。

`putc`：轮询 **LSR bit5（THRE）** 后写 THR。
`getc`：轮询 **LSR bit0（DR）** 后读 RHR——无超时。

riscv 头保留了 `0x10000000` MMIO 宏；是否编入镜像取决于 arch 配置，本篇以 x86 PIO 为主叙述。

### 6.4 硬件：PL011（aarch64）

PrimeCell PL011，MMIO。`uart_open` 传入的虚址即 early 页表已映射的窗口（`boot_map` 把 PL011 映射到 `ROUND_UP(kernel_end, 2MiB)` 高半窗；同址写入 `boot_uart_base_addr`；`cmain` 再把该 VA 传给 `uart_open`）。

`uart_pl011_open`：**仅** `(UART_PL011 *)base_addr` 强转并写寄存器——**不再 map**、不分配页表。

**PL011 寄存器布局（MMIO，字偏移）：**

| 偏移 | 寄存器 | 含义 | 本仓库用法 |
|------|--------|------|-----------|
| `0x00` | DR | 数据寄存器（读 RX / 写 TX，低 8 位有效） | `putc` 写 / `getc` 读 |
| `0x04` | RSR/EIR | 接收状态 / 错误 | 未读 |
| `0x18` | FR | 标志：bit5=TXFF（TX 满）, bit4=RXFE（RX 空） | `putc`/`getc` 轮询位 |
| `0x20` | IBRD | 整数分频 | **未编程**（依赖固件默认） |
| `0x24` | FBRD | 小数分频 | **未编程** |
| `0x28` | LCR_H | 行控制：FEN（FIFO 使能）、WLEN（字长） | 未显式编程 |
| `0x2C` | CR | 控制位：UARTEN / TXE / RXE | 写 `UARTEN\|TXE\|RXE` |
| `0x30` | IFLS | FIFO 水位选择 | 写 0 |
| `0x34` | IMSC | 中断使能掩码 | bit4=RXIM（设备侧 RX） |
| `0x38` | RIS | 原始中断状态 | — |
| `0x3C` | MIS | 屏蔽后中断状态 | — |
| `0x44` | ICR | 中断清除 | 写 `0x7ff` 清全部 |

**PL011 波特率分频：** `baud = UARTCLK / (16 × (IBRD + FBRD/64))`，故

```text
  IBRD = int(UARTCLK / (16 × baud))
  FBRD = frac(UARTCLK / (16 × baud)) × 64   (取整)
  例：UARTCLK=24 MHz, baud=115200
    IBRD = int(24e6 / (16×115200)) = 13
    FBRD = int(0.0163 × 64) = 1
```

本仓库不写 IBRD/FBRD，依赖 QEMU / 固件已设置的默认波特；换板需在 `uart_pl011_open` 补充。

| 动作 | 寄存器 | 含义 |
|------|--------|------|
| 清中断 | `ICR = 0x7ff` | 清 pending |
| 水位 | `IFLS = 0` | |
| 设备侧 RX 掩码 | `IMSC` bit4（RXIM） | **可**在外设断言；GIC 未 unmask SPI 时 CPU 仍无 IRQ |
| 使能 | `CR`：UARTEN \| TXE \| RXE | 未编程 IBRD/FBRD——**依赖固件 / QEMU 默认波特** |

`putc`：等 **FR bit5（TXFF）** 清，写 DR。
`getc`：等 **FR bit4（RXFE）** 清，读 DR 低 8 位。

### 6.5 VGA 文本（仅 x86）

物理 **`0xB8000`**（+ `KERNEL_VIRT_OFFSET` 映射到内核高半窗），每字符 2 字节（ASCII + 属性）。`char_console_putc` 能换行、Tab、卷屏。

**VGA 文本模式显存布局（25 行 × 80 列 = 4000 字节）：**

```text
  0xB8000 ┌─────────────────────────────────────┐
          │ char[0][0]  attr[0][0]  char[0][1] ... │  ← 行 0
          │ ...                                   │
          │ char[24][79] attr[24][79]             │  ← 行 24 (末行)
  0xB9F9F └─────────────────────────────────────┘

  属性字节 (attr):
    bit7   = 闪烁 / 背景高亮 (依 BLINK 模式)
    bit6:4 = 背景色 (RGB)
    bit3   = 前景高亮
    bit2:0 = 前景色 (RGB)
    常用映射: 0=黑 1=蓝 2=绿 4=红 7=白 14=亮黄 ...
    例: 绿字黑底 = 0x02 ; 白字黑底 = 0x07
```

`map_color(forward, back)` 把 ANSI 风格的色号映射到上述 VGA 属性字节。与 log 的关系：**热路径只改 `console->color`，不写字符**。若要在屏上输出字符，须另调 `CONSOLE_PUTC`——当前 core 打印路径中没有。正文实际经串口输出（见 §1）。

---

## 7. 公开 API

本篇涉及的接口分布在：同步调试输出——`modules/log/log.h`（`printk` / `pr_*` / `log_put_*` / `COLOR_SET`）、可移植 UART 分发（`uart.h`）、16550 / PL011 后端、x86 VGA 文本（`char_console.h`）。以头文件注释为准（见上列头文件；已与 `.c` 核对）。

**本篇不涉及：** MCS 原语本身 → `31`；`cmain` 里 `uart_open`→`log_init` 时序 → `02`；aarch64 early map / `boot_uart_base_addr` → `05`/`36`；GIC SPI mask → `27`。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| BSP boot | **`uart_open(...)`** → **`log_init(level)`** → 此后才可依赖 `pr_*` |
| 常规日志 | `#include <modules/log/log.h>` → **`pr_info` / `pr_error`…**（SMP 下宏内加锁） |
| 级别旁路 | **`print(...)`** = `printk(..., LOG_OFF)`——仍**无** MCS |
| 整段写入 | **`log_put_locked(buf, len)`**（持 MCS，逐字节 `uart_putc`） |
| VGA 出字符 | 须另调 **`CONSOLE_PUTC`**——`log_put_byte` **从不**写屏 |

AP 不再 `uart_open`；多核共用同一串口，依赖 MCS 串行化。

### 7.2 日志（`log.h`）

```c
void log_init(u64 msg_level);
void printk(const char *fmt, u64 msg_level, ...);
void log_put_byte(char ch);
void log_put_locked(const u8 *buf, u64 len);
/* macros */ pr_debug / pr_info / pr_notice / pr_warn / pr_error / …
             pr_crit / pr_alert / pr_emer / print / COLOR_SET / COLOR_CLR
```

| 接口 | 说明 |
|------|------|
| `log_init` | 写 `\\n` + 设 `log_level`；非清屏。 |
| `printk` | 仅当 `msg_level <= log_level` 输出；**自身不加锁**。 |
| `pr_*` | `COLOR_SET`→`printk`→`COLOR_CLR`；SMP 用 `log_spin_lock_ptr` + `percpu(log_spin_lock)`。 |
| `print` | `LOG_OFF` 旁路过滤；无锁。 |
| `log_put_byte` | 直调 `uart_putc`；**不** `CONSOLE_PUTC`。 |
| `COLOR_SET(d,fg,bg)` | UART 只接收 `(d,fg)`——第三参背景**不传入 ANSI**（已知错位）。 |

### 7.3 UART（`uart.h` + 后端）

```c
void uart_open(void *base_addr);
void uart_putc(u_int8_t ch);
u_int8_t uart_getc(void);   /* 无限阻塞轮询 */
void uart_close(void);
void uart_set_color(u64 forword, u64 backword);  /* ANSI CSI */
```

| 后端 | 入口 | 要点 |
|------|------|------|
| 16550 | `uart_16550A_*` | COM1 `0x3F8`；IER=0；115200；轮询 THRE/DR。 |
| PL011 | `uart_pl011_*` | **cast 已映射 VA**，不再 map；无 IBRD/FBRD；轮询 FR。 |

编译宏 `_UART_16550A_` / `_UART_PL011_` 在 `uart.c` 静态分发。

### 7.4 VGA 文本（仅 x86，`char_console.h`）

```c
void set_console_color / set_console_size / clear_screen / clear_line(...);
void char_console_putc(...);   /* 经 CONSOLE_PUTC 宏 */
u8 map_color(u64 forward, u64 back);
```

物理 `0xB8000`（+ `KERNEL_VIRT_OFFSET`）。log 热路径只改 `console->color`。

---

## 8. 多架构

| | UART | VGA |
|--|------|-----|
| x86_64 | 16550 COM1 `0x3F8` PIO | 有结构体；log 只改色 |
| aarch64 | PL011 MMIO（DTB early map） | `SET_CONSOLE_*` 空宏 |
| riscv64 | 头文件有 16550 MMIO 基址宏 | — |

---

## 9. 测试

间接：全程启动日志。无独立 log 单元测试。

---

## 10. 限制与后续

- 同步轮询；SMP 下无包装的 `print` 不安全。
- VGA 与正文解耦——真正输出字符的是串口。
- 无行规程；`getc` 无 timeout、无 stdin 接入。
- PL011 不启用波特编程；换板需确认固件默认或补 IBRD/FBRD。
- 链接方若做 UART server：handoff 后 **panic / early 仍应能直写**。

---

## 11. 变更记录

- 2026-10-05：最终词句顺畅。
- 2026-10-05：任务 1/3/5 精读——补缩写全称（MCS / SPI / DLAB 首次出现处加括注）；翻译腔/口语词修正：「还没起来…能打字」→「尚未初始化…能输出字符」、「压成一条线」→「压缩为一条线」、「只盯」→「只看」、「出字」→「输出字符」、「走 IPC」→「经 IPC」、「server 挂了…都丢」→「server 崩溃时…都丢失」、「不进 v0.1 core」→「不纳入 v0.1 core」、「可开设备侧 RXIM」→「可启用设备侧 RXIM」、「收不到线」→「收不到中断线」、「靠轮询」→「依赖轮询」、「已挂好或调度已起来」→「已注册或调度已初始化」、「仍可出」→「仍可输出」、「只认」→「只允许」、「打一个 \n」→「输出一个 \n」、「交错乱字」→「交错产生乱字」、「原样吐 %X」→「原样输出 %X」、「放进…不进 ANSI」→「传入…不传入 ANSI」、「别想当然改动」→「不要想当然改动」、「叠在同一偏移」→「复用在同一偏移」、「early 路径不要 IRQ」→「early 路径不需要 IRQ」、「默认外观」→「默认配置」、「留了…编进…看 arch」→「保留了…编入…取决于 arch」、「映好的窗口…映到」→「已映射的窗口…映射到」、「已设好…补上」→「已设置…补充」、「只收…不进 ANSI」→「只接收…不传入 ANSI」、「已映 VA」→「已映射 VA」、「裸 print」→「无包装的 print」、「真正出字的是串口」→「真正输出字符的是串口」、「不开波特编程」→「不启用波特编程」、「整段写/VGA 出字」→「整段写入/VGA 出字符」；§6.5 末尾重复的「只盯虚拟机窗口 VGA…」改为交叉引用「见 §1」。
- 2026-10-04：补硬件/架构知识——§6.3 加 16550A 寄存器布局表（THR/RBR/IER/LSR/FCR/LCR 等）与波特率分频计算；§6.4 加 PL011 寄存器布局表（DR/FR/IBRD/FBRD/LCR_H/CR/IMSC/ICR 等）与波特率分频公式；§6.5 加 VGA 文本模式显存布局图（25×80、属性字节位域）；§2 加轮询 vs 中断收发对比表。语言润色——「塞进」→「放进」、「拢在一起」→「汇集在一起」、「想当然修一刀」→「想当然改动」、「本篇拥有」→「本篇涉及的接口分布在」、「靠 MCS」→「依赖 MCS」、修正「为为主」笔误。

- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：核对——`uart_pl011_open` = cast 已映 VA、不再 map；与 `boot_uart_base_addr` / `cmain` 传参对齐；`COLOR_SET` UART 仅 `(d,fg)`。
- 2026-09-26：§7 全文审阅——log/uart/char_console Doxygen；划清 vs `31`/`02`/`05`；强调 `log_put_byte` 不写 VGA。
- 2026-09-25：语言轮——补 16550/PL011/VGA `0xB8000` 寄存器链；COLOR_SET 参数错位；确认 `log_put_byte` 不写 VGA 字符；RX 轮询与 GIC mask。
- 2026-09-12：删除空壳 `rendezvos/stdio.h` / `stdlib.h`；入口只认 `log.h`。
- 2026-08-29：整篇重做——直写叙述；VGA 只改色；锁在 `pr_*`；PL011 无有效 IRQ。
- 2026-08-27：初稿。
