# full 逐篇重做进度

本文档是**文档工程进度**（怎么重写 full），**不是**内核 evolution 契约。远期代码项仍只写在 `v0.1/evolution/TODO.md`。

## 工作方式（强制）

1. 按 [`v0.1/full/README.md`](v0.1/full/README.md) 顺序，**一篇一篇**做。  
2. 每篇：深度读该篇源码列表 → 发现设计点（以代码为准，不预设短名单）→ 整篇对照 [`文档生成操作计划.md`](文档生成操作计划.md) 修订 → 本表勾选 → 下一篇。  
3. **禁止**只给几篇加两段动机冒充完成。

状态：`未开始` / `深读中` / `已重做` / `待 Maintainer 复审`

---

## 进度表

| 顺序 | 篇目 | 状态 | 备注（本篇从代码挖出的要点，随做随记） |
|------|------|------|----------------------------------------|
| 1 | 00-总览/00-架构与源码布局.md | 已重做 | common.h 伞+默认 x86；cmain≠main_init；BSP→kernel_handle_msg；weak syscall；initcall 注册≠运行；powerd BSP 门控；挂接点以 thread_boot 为准 |
| 2 | 00-总览/01-构建与链接.md | 已重做 | 模块feature→kernel/arch；手写顶层Makefile；DBG/MEM_SIZE/DUMP持久化；SMP双义；find顺序；loongarch空ld；双勾保留待你是否复审 |
| 3 | 01-启动与初始化/02-启动流程总览.md | 已重做 | 2026-09-20：概述保留；十一节收回；§4.4 布局链 + §6 `cmain` 编排 |
| 4 | 01-启动与初始化/03-模块初始化与内核入口.md | 已重做 | 2026-09-20：initcall 内容，十一节 |
| 5 | 01-启动与初始化/04-平台启动-x86_64.md | 已重做 | 2026-09-25：扩写+§4.6 SDM/Multiboot 官方对照；待 Maintainer 复审 |
| 6 | 01-启动与初始化/05-平台启动-aarch64.md | 已重做 | 2026-09-25：扩写+§4.6 ARM ARM / arm64 booting 对照；待 Maintainer 复审 |
| 7 | 02-内存管理/06-物理内存与Buddy分配器.md | 已重做 | 2026-09-25：§6.1 Multiboot mmap / DTB memory+reg；待复审 |
| 8 | 02-内存管理/07-虚拟地址空间与页表.md | 已重做 | 2026-09-25：§4.5 ENTRY_FLAGS↔硬件 PTE encode/decode；待复审 |

| 9 | 02-内存管理/08-Radix树与用户映射.md | 已重做 | 2026-09-25：语言整理；API=`vmm_radix_tree_*`/`mm_user_utils_*`；DELETE≠owner过滤 |
| 10 | 02-内存管理/09-kmalloc与内核堆.md | 已重做 | 2026-09-25：语言整理；双MSQ/drain/slot档 |
| 11 | 02-内存管理/10-page_slice稀疏页索引.md | 已重做 | 2026-09-25：语言整理；destroy 签名 |
| 12 | 02-内存管理/11-TLB与缓存一致性.md | 已重做 | 2026-09-25：中文+SDM invlpg/IPI、ARM tlbi/*is；待复审 |
| 13 | 02-内存管理/12-ASID与地址空间标识.md | 已重做 | 2026-09-25：中文+TTBR0 ASID/TCR.AS、x86 无PCID；待复审 |
| 14 | 03-任务与调度/13-线程与Task_Manager.md | 已重做 | 2026-09-25：中文+§6.6 TSS/GS/FS、SP_EL0/TPIDR/DAIF；薄RR/zombie/exit |
| 15 | 03-任务与调度/14-线程创建与ELF加载.md | 已重做 | 2026-09-25：中文+§6.8 sysretq/eret；Path A/B；四API；harness零调用方 |
| 16 | 03-任务与调度/15-VSpace所有权与调度切换.md | 已重做 | 2026-09-25：中文+§8 CR3/TTBR0；六步为何；user→kernel滞后 |
| 17 | 03-任务与调度/16-CPU亲和性-创建时绑核.md | 已重做 | 2026-09-25：厘清软件绑核≠中断亲和；online=有TM |
| 18 | 03-任务与调度/17-EBR与线程资源回收.md | 已重做 | 2026-09-25：语言整理；节点EBR≠TCB；与kfree排水并列 |
| 19 | 04-IPC/18-Port与消息模型.md | 已重做 | 2026-09-25：语言整理；两步模型/单队列/Msg_Data；无硬件寄存器 |
| 20 | 04-IPC/19-阻塞与非阻塞收发.md | 已重做 | 2026-09-25：推拉平衡+§6.1文字流；transfer race/IRQ勿阻塞 |
| 21 | 04-IPC/20-kmsg与TLV序列化.md | 已重做 | 2026-09-25：语言整理；LMSG/`s`含NUL/`t`≠`s`；encode_alloc近死 |
| 22 | 04-IPC/21-Port钩子与准入门.md | 已重做 | 2026-09-25：外置策略；REGISTER lookup_name=NULL；deny≠关港 |
| 23 | 04-IPC/22-无锁队列与EBR设计.md | 已重做 | 2026-09-25：同步搬家；假出队；CAS随arch；禁审计外链 |
| 24 | 05-陷阱与中断/23-Trap抽象与分类.md | 已重做 | 2026-09-25：补TSS.RSP0/VBAR16槽/#PF ec；IDT/VBAR；双入口 |
| 25 | 05-陷阱与中断/24-系统调用入口.md | 已重做 | 2026-09-25：LSTAR旁路 vs SVC；sti/DAIF；exit读栈rax |
| 26 | 05-陷阱与中断/25-IRQ向量分配与处理.md | 已重做 | 2026-09-25：硬件号↔trap id；per-CPU表/EOI；两侧reserve实表 |
| 27 | 05-陷阱与中断/26-平台中断-x86_64-APIC与PIC.md | 已重做 | 2026-09-25：8259/LAPIC深挖；补APIC.h/rtc/EOI分支 |
| 28 | 05-陷阱与中断/27-平台中断-aarch64-GIC.md | 已重做 | 2026-09-25：GICD/GICC/IAR·EOIR；INTID；TRAP_ID_MASK缺口 |
| 29 | 06-SMP与同步/28-SMP启动与处理器拓扑.md | 已重做 | 2026-09-25：INIT-SIPI/PSCI与ICR·跳板；双身份；online二分 |
| 30 | 06-SMP与同步/29-per-CPU数据与访问约定.md | 已重做 | 2026-09-25：清零预留；GS_BASE/TPIDR_EL1；MCS me |
| 31 | 06-SMP与同步/30-软IPI机制.md | 已重做 | 2026-09-25：门铃vs工作；0x30/SGI0；仅x86 TLB注册 |
| 32 | 06-SMP与同步/31-锁与内存屏障.md | 已重做 | 2026-09-25：lock前缀/ldaxr·dmb；删假mb；双me禁忌 |
| 33 | 06-SMP与同步/32-TLB_shootdown与跨核一致性.md | 已重做 | 2026-09-25：为何x86 IPI vs tlbi is；gen/busy/done |
| 34 | 07-时间与定时器/33-时间与定时器子系统.md | 已重做 | 2026-09-25：中文+8254/LVT·DCR·TSC-DDL/CNTP；PIT路径；jeffies≠tick_cnt；CNTP/PPI30；README去假8254.c |
| 35 | 08-日志与控制台/34-日志与UART控制台.md | 已重做 | 2026-09-25：中文+16550/PL011/VGA0xB8000；COLOR_SET错位；log不写VGA字；RX轮询 |
| 36 | 09-平台模块/35-ACPI与MADT-x86_64.md | 已重做 | 2026-09-25：RSDP搜索窗/RSDT/MADT；忽略enable；FEE00000；两段式；签名表脏点 |
| 37 | 09-平台模块/36-DTB与设备树-aarch64.md | 已重做 | 2026-09-25：FDT规范；raw vs树；前缀compatible；GIC写死；无ACPI/无PCI消费 |
| 38 | 09-平台模块/37-ELF加载辅助模块.md | 已重做 | 2026-09-25：薄边界；不校验e_machine；打印默认pr_off；与14硬分工 |
| 39 | 09-平台模块/38-PCI枚举与配置空间.md | 已重做 | 2026-09-25：CF8/CFC Mechanism#1；PIO-only；enable被IRQ stub；func0写死缺口 |
| 40 | 09-平台模块/39-PSCI与处理器电源-aarch64.md | 已重做 | 2026-09-25：SMCCC；硬编码id vs DT属性门闩；arch路径；关机终点 |
| 41 | 10-基础设施/40-错误码与panic.md | 已重做 | 2026-09-26：负返回；码表；x86 0x604/0x92；powerd vs 直接panic；riscv缺口 |
| 42 | 10-基础设施/41-名称索引注册表.md | 已重做 | 2026-09-26：VSpace≠name_index；双me；token/grow；仅port生产 |
| 43 | 10-基础设施/42-公共基础库与数据结构.md | 已重做 | 2026-09-26：分级盘点；VSpace=rb；不灌arch寄存器 |
| 44 | 11-测试/43-内核测试框架与测例索引.md | 已重做 | 2026-09-26：失败非干净收敛；按域索引；PCI stub；MSQ同文件 |

**README 顺序 1–44：文档工程「整篇重做」轮次完成**（Maintainer 双勾复审另计）。

---

## 变更记录

- 2026-08-29：建立；明确逐篇深读重做；从 evolution 挪出（不作契约）。
- 2026-08-29：篇 1–2《架构与源码布局》《构建与链接》整篇重做/验收。
- 2026-08-29：篇 3–6 启动分区四篇整篇重做完成。
- 2026-08-29：篇 7–9 内存前三篇（《Buddy》《页表》《Radix》）整篇重做完成；下一篇 kmalloc。
- 2026-08-29：回应「布局链/硬件语境/full 真源」——操作计划增补；删除对 `设计深度审计` 的 full 引用；启动总览+双平台+构建/架构总览补「链接→加载→早期页表」；标明 trap/APIC/GIC/切换篇仍须按新标准加深硬件说明。
- 2026-08-29：old 叙述整合——IPC Port/无锁/收发 + kmalloc 整篇；操作计划写明「动机进 §1/§2」；顺序上 IPC 与 kmalloc 并行加强（用户点名 IPC 笔记）。
- 2026-08-29：篇 11–14《page_slice》《TLB》《ASID》《线程与Task_Manager》整篇重做；下一篇《线程创建与ELF加载》。
- 2026-08-29：篇 15–18 调度区其余四篇整篇重做（ELF / VSpace 所有权 / 亲和性 / EBR）；下一篇按 README：`04-IPC/20-kmsg与TLV序列化.md`（Port/收发/无锁已重做过）。
- 2026-08-29：篇 21–22《kmsg与TLV》《Port钩子》整篇重做；04-IPC 五篇均已重做。下一篇按 README：`05-陷阱与中断/23-Trap抽象与分类.md`（硬件深度重点区）。
- 2026-08-29：篇 24–26《Trap抽象》《系统调用入口》《IRQ向量》整篇重做（硬件深度）；下一篇《平台中断-x86_64-APIC与PIC》。
- 2026-08-29：篇 27–28《APIC与PIC》《GIC》整篇重做（硬件深度）；05-陷阱与中断五篇均已重做。下一篇：`06-SMP与同步/28-SMP启动与处理器拓扑.md`。
- 2026-08-29：篇 29–31《SMP拓扑》《per-CPU》《软IPI》整篇重做；下一篇《锁与内存屏障》。
- 2026-08-29：篇 32–33《锁与内存屏障》《TLB_shootdown》整篇重做；06-SMP 五篇均已重做。下一篇：`07-时间与定时器/33-时间与定时器子系统.md`。
- 2026-08-29：篇 34–35《时间与定时器》《日志与UART》整篇重做；下一篇：`09-平台模块/35-ACPI与MADT-x86_64.md`。
- 2026-08-29：篇 36–40 平台模块五篇整篇重做；09 完成。下一篇：`10-基础设施/40-错误码与panic.md`。
- 2026-08-29：篇 41–44《错误码》《名称索引》《公共库》《测试索引》整篇重做。**full README 顺序 1–44 整篇重做轮次完成**；待 Maintainer 按双勾复审；未宣称 QEMU 全量复测、未提交。
- 2026-08-30：通篇去「人话」标签与 AI 腔，改成正常技术笔记语气；进度表同步清理。
- 2026-09-25：启动区语言轮——`04`/`05` 平台启动按操作计划「硬件链条」扩写（对照源码；动机改写自整理期笔记，成稿无归档路径）；待 Maintainer 复审。
- 2026-09-25：`04`/`05` 增补官方手册对照（SDM / Multiboot；ARM ARM / [arm64 booting](https://docs.kernel.org/arch/arm64/booting.html)）；`06`/`07` 语言整理并纠正 API 签名笔误。
- 2026-09-25：`06` 补平台 memmap（Multiboot / DTB `memory`+`reg`）；`07` 补页表 encode/decode 硬件对照；`11`/`12` 文首记下「下一轮补官方 TLB/ASID」。
- 2026-09-25：`08`/`09`/`10` 语言整理；核对 radix/utils/kmalloc/page_slice 与源码 API。
- 2026-09-25：修正 `vmm_radix_tree.h` I5/owner 注释与实现一致（DELETE 不按 owner 过滤；`owner_info` 原样写入）；清理 v0.1 / SYSCALL 状态文中的 nexus 残留表述。
- 2026-09-25：收回 core 改码授权后继续文档——复核 06–10；`11`/`12` 补硬件官方对照并整理中文。
- 2026-09-25：确认 `vmm_radix_tree.h` 为纠错（DELETE≠owner 过滤；`owner_info` 原样写入）非润色；`13`–`17` 任务区语言+硬件对照（`switch_to` / Path A·B 出口 / CR3·TTBR0 / 绑核≠IRQ 亲和）。
- 2026-09-25：`18`/`19` IPC 前两篇语言整理（推拉平衡对照 `ipc.c`）。
- 2026-09-25：`20`–`22` 收完 IPC 区；`23`/`24` 陷阱前两篇语言+手册入口。
- 2026-09-25：按 maintainer 要求深挖中断硬件——`25`/`26`/`27` 对照 SDM Vol.3、GICv2 IHI0048 与源码扩写（8259/LAPIC/GICD·GICC；OS 如何依赖向量/INTID/EOI）；网络拉手册超时，以规范知识+实现为准。
- 2026-09-25：对照中断相关源码查遗漏——补 `23` TSS.RSP0 / VBAR 16 槽 / #PF ec；`26` APIC.h·rtc·EOI 分支；续 `28` INIT-SIPI/PSCI 硬件链。
- 2026-09-25：`29`–`32` 收完 SMP 同步区（GS/TPIDR、软 IPI 门铃、屏障硬件、TLB shootdown 为何分叉）。下一篇：`33-时间与定时器子系统.md`。
- 2026-09-25：`33`/`34` 时间与日志——扩写定时器硬件链（8254/LVT/TSC-DDL/CNTP）与 UART/VGA；纠正 README 假 `8254.c` 路径。下一篇：`35-ACPI与MADT-x86_64.md`。
- 2026-09-25：`35`/`36` ACPI 与 DTB——RSDP/RSDT/MADT 与 FDT 两阶段；下一篇：`37-ELF加载辅助模块.md`。
- 2026-09-25：`37`–`39` 收完 09-平台模块（ELF 薄边界、PCI CF8/CFC、PSCI/SMCCC）。下一篇：`40-错误码与panic.md`。
- 2026-09-26：`40`–`43` 收完基础设施与测试索引——语言轮对齐源码；**full README 顺序 1–44 本轮语言+硬件对照收束**（Maintainer 双勾复审另计；未宣称 QEMU 复测、未提交）。
