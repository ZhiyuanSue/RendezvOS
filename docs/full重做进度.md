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
| 1 | 00-总览/架构与源码布局.md | 已重做 | common.h 伞+默认 x86；cmain≠main_init；BSP→kernel_handle_msg；interrupt_init 死声明；weak syscall；initcall 注册≠运行；powerd BSP 门控；挂接点以 thread_boot 为准 |
| 2 | 00-总览/构建与链接.md | 已重做 | 模块feature→kernel/arch；手写顶层Makefile；DBG/MEM_SIZE/DUMP持久化；SMP双义；find顺序；loongarch空ld；双勾保留待你是否复审 |
| 3 | 01-启动与初始化/启动流程总览.md | 已重做+补布局链 | 含 §4.4 链接→加载→早期PA→页表→运行演化；仍须随 trap/APIC 篇回灌硬件深度 |
| 4 | 01-启动与初始化/模块初始化与内核入口.md | 已重做 | |
| 5 | 01-启动与初始化/平台启动-x86_64.md | 已重做+补布局 | §4.6 PAE→LME→PG、2MiB、identity；硬件深度随 interrupt 篇继续 |
| 6 | 01-启动与初始化/平台启动-aarch64.md | 已重做+补布局 | §4.5 TTBR/SCTLR、物理快照；EL/PSCI 细节可再加深 |
| 7 | 02-内存管理/物理内存与Buddy分配器.md | 已重做 | phy_mm_init全序+PMM布局；zone回退；errno矩阵；reclaim未接线；多zone骨架诚实 |
| 8 | 02-内存管理/虚拟地址空间与页表.md | 已重做 | 窗口≠Linux自映射；4槽/共享2MiB；BSP/AP virt_mm_init；内容页vs PT页；TLB new/update |

| 9 | 02-内存管理/Radix树与用户映射.md | 已重做 | radix真源vs PTE；L0/L2+clone窗口；utils顺序；COW_PREP；纠正I5/API/测例 |
| 10 | 02-内存管理/kmalloc与内核堆.md | 已重做 | 双MSQ；页走radix owner；drain@kalloc/kfree/schedule；堆不再大锁 |
| 11 | 02-内存管理/page_slice稀疏页索引.md | 已重做 | 调用方养内容页；copy遇洞失败；clone=深拷非COW |
| 12 | 02-内存管理/TLB与缓存一致性.md | 已重做 | mask+schedule顺序；new vs all-core；x86 IPI vs aarch64 IS；cache头空 |
| 13 | 02-内存管理/ASID与地址空间标识.md | 已重做 | 软件号≠硬件；无generation；x86无PCID；alloc在alloc_user_vs |
| 14 | 03-任务与调度/线程与Task_Manager.md | 已重做 | 薄RR；flag/zombie/exit；内核切入不换根；tid真锁；init_proc |
| 15 | 03-任务与调度/线程创建与ELF加载.md | 已重做 | Path A/B；四API对照；harness零调用方；copy失败put vs |
| 16 | 03-任务与调度/VSpace所有权与调度切换.md | 已重做 | 双钉子；六步顺序；user→kernel滞后；allow_self_use精确条件 |
| 17 | 03-任务与调度/CPU亲和性-创建时绑核.md | 已重做 | 亲和=首次入队；online完整条件；无迁移；IRQ勿混 |
| 18 | 03-任务与调度/EBR与线程资源回收.md | 已重做 | 节点EBR≠TCB同步释放；schedule双钩子；overflow leak |
| 19 | 04-IPC/Port与消息模型.md | 已重做 | 两步模型/单队列/Msg_Data↔MSQ假出队；与无锁篇分工 |
| 20 | 04-IPC/阻塞与非阻塞收发.md | 已重做（动机加强） | 推拉平衡叙述；机制段仍可再对照源码深挖 |
| 21 | 04-IPC/kmsg与TLV序列化.md | 已重做 | 信封叙述；LMSG；s含NUL；system表；encode_alloc近死；RPC边界 |
| 22 | 04-IPC/Port钩子与准入门.md | 已重做 | 外置策略；REGISTER lookup_name=NULL；生产零hooks；deny≠关港 |
| 23 | 04-IPC/无锁队列与EBR设计.md | 已重做 | 同步搬家叙述；MSQ假出队；混合≈微内核；禁审计外链 |
| 24 | 05-陷阱与中断/Trap抽象与分类.md | 已重做 | IDT/VBAR硬件链；双入口；schedule不对称；开中断时序；NMI≡IRQ |
| 25 | 05-陷阱与中断/系统调用入口.md | 已重做 | x86 LSTAR旁路 vs aarch64 SVC；sti/DAIF；弱符号 |
| 26 | 05-陷阱与中断/IRQ向量分配与处理.md | 已重做 | 两侧reserve表；NR_IRQ布局；alloc驱动链；free陷阱 |
| 27 | 05-陷阱与中断/平台中断-x86_64-APIC与PIC.md | 已重做 | 三选一；LVT/SVR/ICR；IOAPIC空壳；init时序；已知缺口 |
| 28 | 05-陷阱与中断/平台中断-aarch64-GIC.md | 已重做 | GICD/GICC；intid+64；PPI30；开中断时序；v3占位 |
| 29 | 06-SMP与同步/SMP启动与处理器拓扑.md | 已重做 | 双身份模型；NR_CPU/online二分；INIT-SIPI/PSCI；AP重跑 |
| 30 | 06-SMP与同步/per-CPU数据与访问约定.md | 已重做 | 清零预留非memcpy；MCS me；GS/TPIDR |
| 31 | 06-SMP与同步/软IPI机制.md | 已重做 | 门铃+pending；仅x86 TLB注册；SGI0→64 |
| 32 | 06-SMP与同步/锁与内存屏障.md | 已重做 | 删假mb/rmb；MCS me；调用方表；双me禁忌 |
| 33 | 06-SMP与同步/TLB_shootdown与跨核一致性.md | 已重做 | x86握手；禁aarch64-via-IPI；与02边界 |
| 34 | 07-时间与定时器/时间与定时器子系统.md | 已重做 | 三层叙述；PIT路径；jeffies≠tick_cnt；CNTP/PPI30 quirk |
| 35 | 08-日志与控制台/日志与UART控制台.md | 已重做 | 直写UART；VGA只改色；锁在pr_*；纠正stdio |
| 36 | 09-平台模块/ACPI与MADT-x86_64.md | 已重做 | 两段式RSDP；只消费Local APIC；忽略enable；FEE00000 |
| 37 | 09-平台模块/DTB与设备树-aarch64.md | 已重做 | raw vs树；build_device_tree真位置；删PCI双源 |
| 38 | 09-平台模块/ELF加载辅助模块.md | 已重做 | 薄边界；与03硬分工 |
| 39 | 09-平台模块/PCI枚举与配置空间.md | 已重做 | 仅x86 PIO；删ECAM/aarch64；enable被IRQ stub卡 |
| 40 | 09-平台模块/PSCI与处理器电源-aarch64.md | 已重做 | arch路径；属性vs硬编码id；powerd边界 |
| 41 | 10-基础设施/错误码与panic.md | 已重做 | 负返回；码表；x86 0x604；powerd vs 直接panic |
| 42 | 10-基础设施/名称索引注册表.md | 已重做 | 纠正VSpace≠name_index；双me；port同名查重 |
| 43 | 10-基础设施/公共基础库与数据结构.md | 已重做 | 分级盘点；缩arch灌水 |
| 44 | 11-测试/内核测试框架与测例索引.md | 已重做 | 失败惊喜；MSQ合并；按域索引 |

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
- 2026-08-29：篇 15–18 调度区其余四篇整篇重做（ELF / VSpace 所有权 / 亲和性 / EBR）；下一篇按 README：`04-IPC/kmsg与TLV序列化.md`（Port/收发/无锁已重做过）。
- 2026-08-29：篇 21–22《kmsg与TLV》《Port钩子》整篇重做；04-IPC 五篇均已重做。下一篇按 README：`05-陷阱与中断/Trap抽象与分类.md`（硬件深度重点区）。
- 2026-08-29：篇 24–26《Trap抽象》《系统调用入口》《IRQ向量》整篇重做（硬件深度）；下一篇《平台中断-x86_64-APIC与PIC》。
- 2026-08-29：篇 27–28《APIC与PIC》《GIC》整篇重做（硬件深度）；05-陷阱与中断五篇均已重做。下一篇：`06-SMP与同步/SMP启动与处理器拓扑.md`。
- 2026-08-29：篇 29–31《SMP拓扑》《per-CPU》《软IPI》整篇重做；下一篇《锁与内存屏障》。
- 2026-08-29：篇 32–33《锁与内存屏障》《TLB_shootdown》整篇重做；06-SMP 五篇均已重做。下一篇：`07-时间与定时器/时间与定时器子系统.md`。
- 2026-08-29：篇 34–35《时间与定时器》《日志与UART》整篇重做；下一篇：`09-平台模块/ACPI与MADT-x86_64.md`。
- 2026-08-29：篇 36–40 平台模块五篇整篇重做；09 完成。下一篇：`10-基础设施/错误码与panic.md`。
- 2026-08-29：篇 41–44《错误码》《名称索引》《公共库》《测试索引》整篇重做。**full README 顺序 1–44 整篇重做轮次完成**；待 Maintainer 按双勾复审；未宣称 QEMU 全量复测、未提交。
- 2026-08-30：通篇去「人话」标签与 AI 腔，改成正常技术笔记语气；进度表同步清理。
