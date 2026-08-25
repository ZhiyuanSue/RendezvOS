# log模块的设计

<<<<<<< HEAD
> 短参考，可能滞后于代码。头文件 `modules/log/log.h`，打印见 `rendezvos/stdio.h`。
=======
> 短参考，可能滞后于代码。头文件 `modules/log/log.h`，打印见 `rendezvos/stdio.h`。待办见 [`TODO.md`](TODO.md)；**#37/#38/#46 已关闭**见 [`archive/TODO_DONE.md`](archive/TODO_DONE.md) #68–#71。
>>>>>>> affinity

log 在 boot 早期启用，便于后续调试。

## 冻结期实现（同步直写）

- `log_init(level)`：只设置过滤级别；**无** ring buffer。
- `printk` / `pr_*`：格式化后经 `log_put_byte` → `uart_putc`；`pr_*` 仍带 `uart_set_color`（及 x86 可选 VGA）。
- `log_put_locked`：compat `write(1/2)` 与 `printk` 共用 UART 路径（持锁逐字节）。
- early / panic：**继续直写 UART**（不经 IPC）。

VGA / `x86_char_console` 已与 log 解耦；串口调试不依赖 VGA。

## 上层 / 冻后

console、UART 设备 server、`read(0)`、运行期 log handoff 等不在 core 待办里维护，见上层：

<<<<<<< HEAD
- [`doc/linux_compat/NEXT_PLAN.md`](../../doc/linux_compat/NEXT_PLAN.md) §3（uart_server）
- [`doc/linux_compat/STDIO_SHIM.md`](../../doc/linux_compat/STDIO_SHIM.md)
=======
## 和直写串口、IPC log server 怎么交接

待办 46（Log/输出走专用 IPC server）和上层 uart_server 是同一方向。server 没起来之前，不要拆掉 early 直写。

- 开机早期：core 继续 uart_putc / early_printf  
- 运行期目标：log 或 UART server 经 IPC 管串口，常规打印和 console 走它  
- panic / 紧急：允许直写，免得卡在 IPC 上  

还没拍板：是全部 pr_* 都走 IPC，还是只有用户 console。**RX：** `uart_getc()` 已为阻塞轮询（16550 / PL011）；无 IRQ、无 stdin 接线。外设 IRQ / IOAPIC 见 DONE #14。
>>>>>>> affinity
