# log模块的设计

> 短参考，可能滞后于代码。头文件 `modules/log/log.h`，打印见 `rendezvos/stdio.h`。

log 在 boot 早期启用，便于后续调试。

## 冻结期实现（同步直写）

- `log_init(level)`：只设置过滤级别；**无** ring buffer。
- `printk` / `pr_*`：格式化后经 `log_put_byte` → `uart_putc`；`pr_*` 仍带 `uart_set_color`（及 x86 可选 VGA）。
- `log_put_locked`：compat `write(1/2)` 与 `printk` 共用 UART 路径（持锁逐字节）。
- early / panic：**继续直写 UART**（不经 IPC）。

VGA / `x86_char_console` 已与 log 解耦；串口调试不依赖 VGA。

## 上层 / 冻后

console、UART 设备 server、`read(0)`、运行期 log handoff 等不在 core 待办里维护，见上层：

- [`doc/linux_compat/NEXT_PLAN.md`](../../doc/linux_compat/NEXT_PLAN.md) §3（uart_server）
- [`doc/linux_compat/STDIO_SHIM.md`](../../doc/linux_compat/STDIO_SHIM.md)
