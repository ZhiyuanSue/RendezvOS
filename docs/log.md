# log模块的设计

> 短参考，可能滞后于代码。头文件 `modules/log/log.h`，打印见 `rendezvos/stdio.h`。待办见 [`TODO.md`](TODO.md)（冻结：#37/#38 最小；冻后：#46 server）。

log模块作为最重要的一个调试手段，在boot完成之后应当放在最开始进行。否则不利于后面的调试

## 前后端分离架构

参考linux的log模块，使用前后端分离架构，整体采用生产者-消费者模型，前端应当只是负责把输出信息放入log_buffer，
后端则负责定期把log_buffer输出。

现状：有 log_buffer 骨架，但热路径仍同步 uart_putc；VGA 清理还挂在 log_init；前后端分离和下面多核 IPI 都还没真正落地（对应待办 37、38）。

## 多核的问题

在多核下需要考虑通过ipi进行输出信息到缓冲池。

## 和直写串口、IPC log server 怎么交接

待办 46（Log/输出走专用 IPC server）和上层 uart_server 是同一方向。server 没起来之前，不要拆掉 early 直写。

- 开机早期：core 继续 uart_putc / early_printf  
- 运行期目标：log 或 UART server 经 IPC 管串口，常规打印和 console 走它  
- panic / 紧急：允许直写，免得卡在 IPC 上  

还没拍板：是全部 pr_* 都走 IPC，还是只有用户 console。收包先依赖驱动 getc；x86 若要 IRQ 收包再谈远期 IOAPIC（见 TODO / TODO_DONE）。
