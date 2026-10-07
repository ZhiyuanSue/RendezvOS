# TODO（evolution）

非 v0.1 现行契约；full 文档进度、代码远期项与 design 占位。

---

## full 文档状态

| 项 | 状态 |
|----|------|
| 44 篇正文初稿 | 已完成（`v0.1/full/README.md` 第一格 `[x]`） |
| Maintainer 审定 | 进行中（第二格 `[ ]`） |
| compat 正文 | 待 full 全部 `[x] [x]` 后从 full 抽出 |
| evolution `design/`、`archive/` | 规划见 `evolution/README.md`，正文未写 |

full 正文缺口：（当前无；新增篇目或整篇重写需求在此表格追加。）

---

## 代码与文档远期项

摘自各 full 篇 §10「限制与后续」；实现或专篇 design 完成前**不是** v0.1 API 承诺。

| ID | 主题 | 来源 full 篇 | 说明 |
|----|------|--------------|------|
| E1 | NUMA / 多 zone `VSpace::pmm` | `02-内存管理/08-Radix树与用户映射.md`、`03-任务与调度/15-VSpace所有权与调度切换.md` | 物理页路由与 shootdown 策略增强 |
| E2 | Port / MSQ 性能与 IPC 演进 | `04-IPC/19-Port与消息模型.md`、`04-IPC/20-阻塞与非阻塞收发.md`、`04-IPC/18-无锁队列与EBR设计.md` | 队列与 port 表扩展；背压窗口、批量绕开会合、广播、调度感知 IPC 见 `20` §10.1（均非 v0.1）；design 占位 `design/lockfree-IPC远期.md` |
| E3 | 运行期 CPU 迁移 / `affinity_mask` | `03-任务与调度/16-CPU亲和性-创建时绑核.md`、`03-任务与调度/13-线程与Task_Manager.md` | v0.1 仅创建时绑核；design 占位 `design/线程运行期迁移与affinity_mask.md` |
| E4 | 调度：优先级 / 迁移 / exit 协议 | `03-任务与调度/13-线程与Task_Manager.md`、`04-IPC/20-阻塞与非阻塞收发.md` | RR 单策略；`scheduler` 函数指针预留；IPC 依赖 / 优先级翻转见 `20` §10.1 |
| E5 | x86 IOAPIC / MSI 路由 | `05-陷阱与中断/26-平台中断-x86_64-APIC与PIC.md` | IOAPIC 空壳；design 占位 `design/IRQ亲和性与IOAPIC.md` |
| E6 | PCI enable / IRQ / BAR | `09-平台模块/38-PCI枚举与配置空间.md` | 源码注释 TODO；当前仅枚举 |
| E7 | UEFI / Secure Boot 引导 | `01-启动与初始化/02-启动流程总览.md` | 现行 Multiboot / Linux Image + DTB |
| E8 | aarch64 EL3/EL2 `drop_to_el1` | `01-启动与初始化/05-平台启动-aarch64.md` | QEMU virt 通常已在 EL1 |
| E9 | 日志异步 / console-server | `08-日志与控制台/34-日志与UART控制台.md` | 热路径同步 UART；design 占位 `design/日志与console-server.md` |
| E10 | riscv64 / loongarch 主线化 | `00-总览/00-架构与源码布局.md` | 启动与测例覆盖弱于 x86_64/aarch64 |
| E11 | EBR / MSQ slot 扩展 | `03-任务与调度/17-EBR与线程资源回收.md` | 更大 retire 表或 generational 方案 |
| E13 | 线程切换保存浮点 / 向量状态 | `03-任务与调度/13-线程与Task_Manager.md`、`01-启动与初始化/04-平台启动-x86_64.md`、`01-启动与初始化/05-平台启动-aarch64.md` | 启动已开硬件（用户可能用）；`switch_to` 仍只保存通用寄存器。x86 的 XMM 等，aarch64 的 `v0`–`v31` |

新增条目：在此表追加一行，并在对应 full 篇 §10 加一句回链本表 ID。

---

## 变更记录

- 2026-10-05：新增 **E13**——线程切换保存浮点/向量（启动已开硬件，上下文未保存）。
- 2026-09-12：关闭 **E12**——已从 `common.h` 删除死声明 `interrupt_init`；现行路径仅为 `init_interrupt`。
- 2026-08-27：新增「构建与链接」专篇待办（Maintainer 审阅总览时提出）。
- 2026-08-27：`00-总览/01-构建与链接.md` 初稿完成，移除上表对应待办。
- 2026-08-27：full 44 篇初稿全部完成；本节改为「文档状态 + 远期项」；汇总 E1–E11。
- 2026-08-29：追加 E12（`interrupt_init` 死声明）；full 进入「按操作计划逐篇深读重做」（进度不在 evolution，见 `core/docs/生成进度.md`）。
