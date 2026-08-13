# Core 待办

已完成历史：[`archive/TODO_DONE.md`](archive/TODO_DONE.md)  
接口入口：[`GUIDE.md`](GUIDE.md) §6 · [`USING_CORE.md`](USING_CORE.md)

2026-08-09 对照源码与 log / lockfree-ipc 文档核过一遍：下面按「平台 → 内存 → 日志与 IPC → 不做或归上层」列，方便一项项收窄。编号沿用旧清单（中间空号是历史留下的）。

建议收尾顺序：先平台（IOAPIC / 外设中断 / **IDT 向量登记** / UART 收包），再内存（改页属性、boot 栈），再日志前后端与 IPC 输出（#37–38、#46），最后清文档和过时注释。

---

## 一、平台（外设中断、SMP、串口硬件）

1、IDT/GDT：段类型、DPL/CPL/RPL 在 64-bit 下的用法复查。（部分：用户段 dpl 等已有，缺系统复查）  
2、8259A 与 APIC 共存路径；APIC 模式下外设中断路由。（未做：IOAPIC 头文件仍是空的，MADT 里 IOAPIC 分支空，COM1 等挂不上）  
3、CPUID 结果缓存，避免每次查询都重读。（部分：启动时填过 `cpu_info`；`start_arch` 检查逻辑与 leaf 0x06 仍 TODO）  
10、页表 isolation / domain bits 复查。（部分：统一 ENTRY_FLAGS 已做，见已完成 4、5；更深域模型未再审）  
19、ACPI 解析拆进 `modules/`。（部分：探测在 modules，表解析仍偏 arch）  
22、CPU topology 接到 `cpu_id` / per-CPU 信息。（未做：头文件有结构，未真正用起来）  
25、中断嵌套策略；不可嵌套的 timer 要有 RT 一类标志。（未做）  
39、Multiboot2 路径在 QEMU 下校验。（未做：日常仍 Multiboot1）  
43、aarch64 上 DTB 与 PCI 设备模型统一。（未做：两套节点）  

源码里还有、原先没写进编号的：

- x2APIC 路径收尾（`IRQ.c` 已 enable，注释仍 TODO）  
- APIC timer 是否 always-running（CPUID 0x06 / ARAT，`LocalAPIC.c`、`cpuinfo.h`）  
- **IDT 向量占用未登记（timer 与 TLB IPI 同债）**：core 各自硬编码占用 IDT 槽——timer 用 `timer_irq_num`（现约 `0x20`）、SMP TLB flush IPI 用 `IRQ_VECTOR_SMP_TLB_FLUSH`（现 `0x30`）、另有 spurious 等——**没有**给兼容层 / 日后 IOAPIC 设备分配看的「保留集」。结果是上层若按 Linux 习惯在中低段要设备向量，可能与 core 私占号撞车；兼容层也不该靠猜「哪几个号已被占」。应做：（1）软件 IPI（TLB 及以后 resched 等）收到**高位保留区**，与设备向量池分开；（2）core 单一真源登记或极小分配接口（保留 IPI / 分配设备向量），timer、TLB、IOAPIC 路由都走它；（3）上层禁止私自拣裸 vector，只能向 core 申请。功能上 TLB IPI 最小路径已接；本项是**所有权与防冲突**，不是再实现一遍 SMP flush。  
- PCID：用户态 SMP TLB flush 已按 `tlb_cpu_mask` 定点 IPI；远端按页 `invlpg` 已接，PCID 仍未做（`arch_smp_tlb_flush.c`）  
- UART 16550A：`getc` 恒返回 0，IER=0（收包中断关着）  
- UART PL011：`getc` 恒返回 0；开了 RXIM 但没有 handler / 真读 DR  
- PCI：使能设备、分配 IRQ、BAR 直接复用 BIOS、ROM device（`pci_ops.c`）  
- aarch64 `boot.S`：EL3 / SPSR / ELR 设置  
- idle 里「或许关中断」（`tcb.c`）  
- `get_cpu_var` / `put_cpu_var` 仍是空宏（`percpu.h`）  
- `start_arch.c` 重写 CPUID 检查（和上面 3 一条线）  

外设中断怎么挂：先在控制器上 unmask / 路由（x86 走 APIC 就必须把 IOAPIC 做起来），再 `register_irq_handler`（写法见 `trap.md`，不要只看 `interrupt.md` 硬件笔记），需要进线程再走现有的 IRQ→IPC。设备向量号必须避开上一则 core 保留集，勿与 timer / IPI 硬编码槽冲突。

---

## 二、内存（基本收尾相关）

15、每核栈页与权限；map_handler 增加「只改页表项属性、不换物理页」的接口。（部分：线程栈已走分配器；boot 栈还是静态的；改属性 API 没有。和下面源码条重叠。）  
30、`common/dsa/bitmap.h` 分清「可多核原子改」和「单线程/已持锁再改」两套用法。（未做：现在只有普通读写改 bit；谁在多核共享位图上用、要不要另做 atomic 版或强制外层加锁，头文件/注释里要写死，避免混用。）  
31、`memory_zone` 参数化，别写死 ZONE_NORMAL。（未做）  
48、多 zone 时选哪个分配器，不要只会 handler 默认那一套。（未做：见 `memory.md`）  
53、map handler entry 失败时 refill；fault / 分配失败路径。（部分：`map_fail` 能补 ppn_cache；OOM/swap 没有）  

源码 / 文档里还有：

- `map_handler.h`：缺改 PTE 属性接口（归进 15）  
- `main.c`：PMM 起来后给栈分配页，弃用 boot stack（x86 LSS）  
- `buddy_pmm.c`：buddy 耗尽时 swap（策略偏上层，core 至少先把失败语义说清楚）  
- `pmm.c`：更多 zone / zone 上界  
- `thread_loader.c`：记录已用到的用户 VA，方便清理 / 影响 radix  
- `memory.md`：多 zone、OOM→swap 仍是文档级 TODO  

分配器性能调优（不挡基本收尾；QEMU 上意义不大，真机再做）：

- 旧 26、测「多个核的分配器元数据是否打在同一 cache line 上」——不做（见第五节）。  
- 旧 27、per-CPU cache 大小：每个 CPU 本地先囤多少空闲对象/页再去碰全局堆。囤多了费内存，囤少了多核抢锁更勤。现在是「以后要不要改这个数字」的调优，不是缺功能。  
- 旧 28、kmalloc 调参：各档尺寸、对齐、多大改走 buddy 等参数怎么选。同样是性能/碎片权衡，功能上 kmalloc 已能用。  

---

---

## 三、日志、串口输出、IPC 运行时（和上层关系紧）

37、log buffer 与刷出策略。  
38、VGA early print 和 log 模块解耦。  
（37、38 部分完成：有 buffer 骨架，热路径仍同步 `uart_putc`，VGA 还挂在 log_init；`log.md` 前后端分离、多核 IPI 汇入都还没落地。）

46、Log / 输出走专用 IPC server 进程。（未做；和上层 uart_server 同一方向。）  

和直写串口怎么交接（定稿前别拆掉 early 路径）：

- 开机早期：core 继续 `uart_putc` / `early_printf`  
- 运行期目标：log 或 UART server 经 IPC 独占 MMIO，常规打印和 console 走它  
- panic / 紧急：允许直写，免得卡在 IPC 上  
- 尚未拍板：是全部 `pr_*` 都走 IPC，还是只有用户 console；收包还得先把上面平台 IOAPIC 和 getc 做起来  

42、标准 loader 路径上用户栈 argc/argv。（core 的 `thread_loader` 基本只设 SP；Linux 的 argv/auxv 在兼容层 `linux_boot` / exec 栈——策略别塞回 core。）  
52、Port 管理；函数调用包成 IPC wrapper。（原语在 core；系统化 wrapper 更像上层，compat RPC 已有一套。）  

`lockfree-ipc.md` §8 里还挂着的：

- 8.3 批量 / 异步 / 函数调用式直投（原语有一些，上层模式没产品化）  
- 8.4 消息广播（远期）  
- 8.5 调度器可感知的 IPC / 优先级依赖（远期）  

---

## 四、文档

50、公共 API 逐步补 Doxygen 风格注释。（做一点算一点）  

---

## 五、不做，或明确归上层 / 远期

- cancel IPC（`lockfree-ipc` 8.1）：已拒绝，不做。  
- 固定容量那种背压 API（8.2）：阻塞 send/recv 本身就是背压；异步批量的流控归上层。  
- Linux 风格 argc/argv、personality：归兼容层（42）。  
- 换页 / swap 策略、缺页 COW 语义：归上层；core 负责分配失败和 map/radix。  
- 把 busybox 默认 cmdline 写进 core Makefile：不要，策略在上层注入。  
- 旧 26、allocator cache-line 碰撞测试：QEMU 上看不出真实伪共享，短期不做；真机性能问题再单独立项。  
- 旧 27、28（per-CPU cache 大小、kmalloc 调参）：远期性能调优，不列入基本收尾。  

---

做完一项：在 [`archive/TODO_DONE.md`](archive/TODO_DONE.md) 按同样中文条目录一条，并把源码里过时的 TODO 注释清掉。
