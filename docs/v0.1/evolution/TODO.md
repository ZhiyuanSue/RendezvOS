# TODO（evolution）

非 v0.1 现行契约；full 文档进度、代码远期项与 design 占位。

---

## full 文档状态

| 项 | 状态 |
|----|------|
| 44 篇正文初稿 | 已完成（`v0.1/full/README.md` 第一格 `[x]`） |
| Maintainer 审定 | 进行中（第二格 `[ ]`） |
| 兼容层文档正文 | 待 full 全部 `[x] [x]` 后从 full 抽出 |
| evolution `design/`、`archive/` | 规划见 `evolution/README.md`，正文未写 |

full 正文缺口：（当前无；新增篇目或整篇重写需求在此表格追加。）

---

## 代码与文档远期项

摘自各 full 篇 §10「限制与后续」；实现或专篇 design 完成前**不是** v0.1 API 承诺。

| ID | 主题 | 来源 full 篇 | 说明 |
|----|------|--------------|------|
| E1 | NUMA / 多 zone `VSpace::pmm` | `02-内存管理/08-Radix树与用户映射.md`、`03-任务与调度/15-VSpace所有权与调度切换.md` | 物理页路由与 shootdown 策略增强 |
| E2 | Port / MSQ 性能与 IPC 演进 | `04-IPC/19-Port与消息模型.md`、`04-IPC/20-阻塞与非阻塞收发.md`、`04-IPC/18-支持IPC的无锁MSQ队列实现.md` | 队列与 port 注册表扩展；背压窗口、批量绕开会合、广播、调度感知 IPC 见 `20` §10.1（均非 v0.1）；design 占位 `design/lockfree-IPC远期.md` |
| E3 | 运行期 CPU 迁移 / `affinity_mask` | `03-任务与调度/16-CPU亲和性-创建时绑核.md`、`03-任务与调度/13-线程与Task_Manager.md` | v0.1 仅创建时绑核；design 占位 `design/线程运行期迁移与affinity_mask.md` |
| E4 | 调度：优先级 / 迁移 / exit 协议 | `03-任务与调度/13-线程与Task_Manager.md`、`04-IPC/20-阻塞与非阻塞收发.md` | 默认 `round_robin_schedule`；`scheduler` 函数指针可换（兼容层可注入）；IPC 依赖 / 优先级翻转见 `20` §10.1 |
| E5 | x86 IOAPIC / MSI 路由 | `05-陷阱与中断/26-平台中断-x86_64-APIC与PIC.md` | RTE/mask/`irq_enable`/定向 EOI 已接；**ISO/MSI/设备接线**未做；design 占位 `design/IRQ亲和性与IOAPIC.md` |
| E6 | PCI enable / IRQ / BAR | `09-平台模块/38-PCI枚举与配置空间.md` | 源码注释 TODO；当前仅枚举 |
| E7 | UEFI / Secure Boot 引导 | `01-启动与初始化/02-启动流程总览.md` | 现行 Multiboot / Linux Image + DTB |
| E8 | aarch64 EL3/EL2 `drop_to_el1` | `01-启动与初始化/05-平台启动-aarch64.md` | QEMU virt 通常已在 EL1 |
| E9 | 日志异步 / console-server | `08-日志与控制台/34-日志与UART控制台.md` | 热路径同步 UART；design 占位 `design/日志与console-server.md` |
| E10 | riscv64 / loongarch 主线化 | `00-总览/00-架构与源码布局.md` | 启动与测例覆盖弱于 x86_64/aarch64 |
| E11 | EBR / MSQ slot 扩展 | `03-任务与调度/17-EBR与线程资源回收.md` | 更大 retire 表或 generational 方案 |
| E13 | ~~线程切换保存浮点 / 向量状态~~ | `03-任务与调度/13-线程与Task_Manager.md`、`01-启动与初始化/04-平台启动-x86_64.md`、`01-启动与初始化/05-平台启动-aarch64.md` | **已做（eager）**：x86 `arch_fp_cpu_init` + FXSAVE 或 XSAVE（CPU 支持则 **AVX**；**不开** AVX-512）；aarch64 Q0–Q31+FPCR/FPSR。**仅 user** `ctx->fp`；`arch_fp_state_size()` 运行时。AVX-512 / SVE 仍不做 |
| E14 | ~~硬件随机数 / `rand64`~~ | `10-基础设施/42-公共基础库与数据结构.md` §4.3–§4.4 | **已做（best-effort）**：`arch_get_random_long` + per-CPU / `random_cpu_init` / `rand64`·`rand32`；混洗唯一核 `splitmix64`。非 CSPRNG；强化池 / getrandom 归 compat |
| E15 | ~~配对成功后 transfer 失败丢唤醒~~ | `04-IPC/20-阻塞与非阻塞收发.md` | **已做（空醒，maintainer 审定）**：`THREAD_FLAG_IPC_EMPTY_WAKE`；`NO_MEM`/`NO_MSG` 等由配对成功方对 peer flag+ready；已被配对的一侧返回 `-E_REND_AGAIN`。不空醒：send `AGAIN`/`RENDEZVOS`。残余见 `20` §10（fill 失败丢消息） |
| E16 | x86 sparse APIC id vs 稠密 `cpu_id` / `NR_CPU` | `06-SMP与同步/28-SMP启动与处理器拓扑.md`、`03-任务与调度/16-CPU亲和性-创建时绑核.md` | x86 以 APIC id 当下标；`cpu_id_is_online` / `task_manager_for_cpu` 用 `cpu < NR_CPU` 稠密假设。QEMU 稠密 id 可瞒过。v0.1 **不承诺**拓扑在稀疏 APIC 下正确。选项：启动时建 APIC→逻辑下标表，或统一稠密编号（改 IPI dest）。与 E3（运行期迁核）正交；中等工程，post-v0.1 |

新增条目：在此表追加一行，并在对应 full 篇 §10 加一句回链本表 ID。

---

## 变更记录

- 2026-11-24：新增 **E16**——x86 sparse APIC id vs 稠密 `NR_CPU` / `cpu_id_is_online`；回链 `28` §10。
- 2026-11-22：关闭 **E15**——空醒 flag + claim 失败路径唤醒；回链 `20` §6.2–§6.4 / §10。
- 2026-11-22：新增 **E15**——配对成功后 transfer 失败时不唤醒已配对等待者（既有问题；should_block_hook 窗口外部审阅期间发现，回链写入 `20` §10）。
- 2026-11-21：E14 行措辞「熵」改「随机数」（全树同步，「熵」字易困惑）。
- 2026-11-21：E14 命名——混洗 `splitmix64`；内核取数 `rand64`/`rand32`；per-CPU + `random_cpu_init`。
- 2026-11-20：关闭 **E14**——硬件随机数入口；分界写入 `42` §4.3（后统一命名如上）。
- 2026-11-15：E13 x86 侧定稿形态——`arch_fp.c` 单文件：`fp_cfg`（路径+XCR0/RFBM）、ALLOWED/DENIED、`cpu_init` 内联 detect/enable；文档 §6.6 / 启动篇 04 对齐。
- 2026-11-14：E13 后续——`arch_fp_cpu_init`；x86 按 CPUID 开 AVX + XSAVE，`arch_fp_state_size()` 运行时。
- 2026-11-13：关闭 **E13**——eager FXSAVE（x86）/ FPSIMD（aarch64）挂入 `switch_to` + refresh / fork merge。
- 2026-10-05：新增 **E13**——线程切换保存浮点/向量（启动已开硬件，上下文未保存）。
- 2026-09-12：关闭 **E12**——已从 `common.h` 删除死声明 `interrupt_init`；现行路径仅为 `init_interrupt`。
- 2026-08-27：新增「构建与链接」专篇待办（Maintainer 审阅总览时提出）。
- 2026-08-27：`00-总览/01-构建与链接.md` 初稿完成，移除上表对应待办。
- 2026-08-27：full 44 篇初稿全部完成；本节改为「文档状态 + 远期项」；汇总 E1–E11。
- 2026-08-29：追加 E12（`interrupt_init` 死声明）；full 进入「按操作计划逐篇深读重做」（进度不在 evolution，见 `core/docs/生成进度.md`）。
