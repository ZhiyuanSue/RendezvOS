# Core 已完成项（归档）

从 `TODO.md` 挪过来的历史清单。新完成的项请追加在下面，并同步改 [`../TODO.md`](../TODO.md)。

---

4、参考其他内核，给不同架构的页表的bits设定一个统一的表示。(done)  
5、根据修改过的架构统一页表flag表示，修改原先的代码。(done)  
6、添加清理tlb的部分(done)  
7、添加map函数的test和unmap的实现(done)  
8、实现nexus(done)  
9、实现spmalloc(done)  
11、启动PIT时钟，并用于校准APIC时钟(Done)  
12、x86 中断保存 / IDT cli 问题(Done)  
13、x86 简单时钟系统(Done)  
14、APIC 多核启动(Done)  
16、aarch64 smp 启动（Done）  
17、x86_64 启动其他核心（Done）  
18、makefile 改进(Done)  
20、MCS spinlock 多核整合(Done)  
21、arm generic timer(Done)  
23、aarch64 中断和 Syscall(Done)  
24、aarch64 timer / unknown trap 等(Done)  
29、dtb 解析 uart(Done)  
33、进程相关调研(DONE)  
34、多仓库拆分设计(DONE)  
35、初始化内存分配调试输出(DONE)  
36、error_t 重设计(DONE)  
41、x86_64 用户态进内核中断(DONE)  
45、IPC 机制(Done)  
47、pmm node/zone、nexus 改造(Done，nexus整套系统均已废除)  
49、mm 文档重构(Done with cursor)  
51、返回值/入参检查(Done with cursor)  
44、进程/线程资源回收(Done with cursor)  
54、x86 用户态 trap 路径上的 swapgs（已实现；`trap_vec.S` 过时 TODO 注释已删）(done)  
55、core 测试以内核线程跑（modules/test → gen_thread_from_func）(done)  
