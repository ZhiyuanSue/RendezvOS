# Core 待办

已完成历史：[`archive/TODO_DONE.md`](archive/TODO_DONE.md)  
接口入口：[`GUIDE.md`](GUIDE.md) §6 · [`USING_CORE.md`](USING_CORE.md)

2026-08-09 对照源码与 log / lockfree-ipc 文档核过一遍：下面按「平台 → 内存 → 日志与 IPC」列，方便一项项收窄。编号沿用旧清单（中间空号是历史留下的）。明确不做 / 归上层见 [`archive/TODO_DONE.md`](archive/TODO_DONE.md) 文末。

建议收尾顺序：先平台（IOAPIC / 外设中断 / **IDT 向量登记** / UART 收包），再内存（改页属性、boot 栈、分配失败 hook），再日志前后端与 IPC 输出（#37–38、#46），**冻结前补 Port 准入/可见性 hook（A+B）**，最后清文档和过时注释。

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
- **IDT 向量占用未登记（timer / soft IPI HW 槽同债）**：soft IPI **逻辑框架已完成**（见归档）；x86 仍硬编码单门铃向量 `0x30`、timer 约 `0x20`、spurious 等，**没有**给兼容层 / IOAPIC 看的保留集。应做：（1）core 保留 IPI/timer 等到高位或单一登记表；（2）设备向量走分配接口；（3）上层禁止私拣裸 vector。本项是**向量所有权**，不是再实现一套 IPI。  
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
31、`memory_zone` 参数化，别写死 ZONE_NORMAL。（未做）  
48、多 zone 时选哪个分配器，不要只会 handler 默认那一套。（未做：见 `memory.md`）  
53、map handler entry 失败时 refill；fault / 分配失败路径。（部分：`map_fail` 能补 ppn_cache；per-pmm `reclaim_fn` 已接，策略归上层）  

源码 / 文档里还有：

- `map_handler.h`：缺改 PTE 属性接口（归进 15）  
- `main.c`：PMM 起来后给栈分配页，弃用 boot stack（x86 LSS）  
- `buddy_pmm.c`：耗尽时走该 pmm 的 `reclaim_fn`（策略在上层）  
- `pmm.c`：更多 zone / zone 上界  
- `thread_loader.c`：记录已用到的用户 VA，方便清理 / 影响 radix  
- `memory.md`：多 zone 仍文档级 TODO  

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

- **Port 准入 + 可见性 hook（冻结前；A+B；只留门缝不写策略）**：core 定位是混合内核**基座**——为微内核式隔离提供可能，**不**在 core 里实现完整 capability / namespace / cgroup。今天 `port_ops_begin/end` 只做生命周期门闩（能否配对）；全局 `name_index` 谁都能查（有名字≈有权）。冻结前要补的是可注入的基础面，仿 `pmm->reclaim_fn`：  
  - **A（配对）**：`port_ops_begin`（send/recv/try）路径问 hook：当前 thread/task 可否对该 port 做该 op。  
  - **B（可见性）**：lookup（及必要时 register）路径同样过 hook，避免只卡收发、名字仍全局泄漏。  
  - 默认 hook = NULL → 行为与今完全相同。core 只认 allow/deny（或 errno），**不**放 cap 表、不放 namespace 对象、不做委派/回收。capability / 容器策略全归上层；有此 hook 后上层才「有基本实现可挂」。这符合「机制在下、策略在上」。**明确不做（本项范围外）**：L4 式 capability 系统、拆掉全局 port 表、cgroup 配额——那些是上层亮点，不是冻结前 core 债。参考动机：MettEagle（OSDI’25）用 cap 表达可见性；我们只预留等价注入点。  

`lockfree-ipc.md` §8 里还挂着的：

- 8.3 批量 / 异步 / 函数调用式直投（原语有一些，上层模式没产品化）  
- 8.4 消息广播（远期）  
- 8.5 调度器可感知的 IPC / 优先级依赖（远期）  

---

## 四、文档

50、公共 API 逐步补 Doxygen 风格注释。（做一点算一点）  

---

做完一项：在 [`archive/TODO_DONE.md`](archive/TODO_DONE.md) 按同样中文条目录一条，并把源码里过时的 TODO 注释清掉。明确不做的也记进该归档文末列表，勿堆在本文件。
