# Log module design

> **文档角色：** 短参考（可能滞后于代码；非主线阅读）  
> **头文件：** `modules/log/log.h` · **打印 API：** `rendezvos/stdio.h` 的 `printf` / `early_printf`  
> **入口：** [`GUIDE.md`](GUIDE.md) §3、§6

实现细节以 `modules/log/` 与 `printf` 调用路径为准；本文仅记录设计意图。

---

# log模块的设计

log模块作为最重要的一个调试手段，在boot完成之后应当放在最开始进行。否则不利于后面的调试

## 前后端分离架构
参考linux的log模块，使用前后端分离架构，整体采用生产者-消费者模型，前端应当只是负责把输出信息放入log_buffer，
后端则负责定期把log_buffer输出。

## 多核的问题
在多核下需要考虑通过ipi进行输出信息到缓冲池。