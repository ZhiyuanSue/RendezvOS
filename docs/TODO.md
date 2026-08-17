# Core 待办（冻结收尾）

完成 / 明确不做：[`archive/TODO_DONE.md`](archive/TODO_DONE.md)  
接口：[`GUIDE.md`](GUIDE.md) §6 · [`USING_CORE.md`](USING_CORE.md) · 日志背景 [`log.md`](log.md)

做完 → 记 DONE 归档，并清源码对应 TODO 注释。

---

## 怎么读这张单（冻结边界）

| 档 | 含义 |
|----|------|
| **A 必做** | 不做完不宣布 core 接口冻结 |
| **B 尽量** | 冻结前值得清；拖过冻结也行，但会变成冻后小补丁 |
| **C 非冻结** | 明确以后再说；**不要**和 B 混成「还剩两大块」 |

**对你「剩两大块」的纠正：**

- **可以当成两大块主线的**：  
  1）**Port 准入 + 可见性 hook（A+B）**（唯一冻结必做）  
  2）**串口收包 + log 最小收尾**（UART `getc` + #37/#38；同属「输出/调试链路」，冻结前尽量做）
- **不要把 #46 Log IPC server 算进冻结主线。**  
  它和上层 `uart_server` 同向，是 **C 档 / 冻后**；现在 `log_put_byte` 仍同步 `uart_putc`，early/panic 直写必须保留（见 `log.md`）。  
  把「log 前后端最小可用」和「专用 IPC log server」拆开，否则会把冻结范围撑大。

---

## A. 冻结必做

### 1. Port 准入 + 可见性 hook（配对 A + 可见性 B）

**问题（今天）**

- `port_ops_begin/end` 只做 **生命周期门闩**（REGISTERED ↔ CLOSING，保护 unregister 与 send/recv 并发），**不做权限**。
- 全局 `name_index`：**谁都能 `port_table_lookup` / `thread_lookup_port`** → 查到名就能 ops（ambient authority：有名字 ≈ 有权）。
- 单租户 / 早期 Linux 兼容够用；以后要 container / 多租户隔离时，若冻结后再改这些入口 = 破冻。

**目标（只留门缝，不写策略）** — 仿 `pmm_set_reclaim_hook` / `pmm->reclaim_fn`：

| 子项 | 卡点 | 行为 |
|------|------|------|
| **A 配对准入** | `port_ops_begin`（覆盖 blocking/`try` 的 send、recv） | hook：`当前 thread/task × port × op → allow/deny`（bool 或 errno）；deny 则 ops 失败，不进入匹配 |
| **B 可见性** | `port_table_lookup*` / `thread_lookup_port`（及必要时 `register_port`） | 同样过 hook，避免「只能卡收发、名字仍全局泄漏」 |

**约束**

- 默认 hook = **NULL** → 行为与今日完全一致（全放行）。
- core **不**实现 capability 表、namespace、seccomp、cgroup；策略全在上层（compat / servers）。
- 签名细节实现时再定（全局一个 hook vs per-port；`op` 枚举）。先写进 `USING_CORE.md` / `GUIDE.md` §6。
- **不做**：真 capability / 本地 handle、弱化全局名（那是模型换皮，C 档愿望）。

**验收**

- NULL hook：现有 IPC / RPC / 测例路径无行为变化。
- 装一个「deny 某名 / 某 op」的测试 hook，能挡住 lookup 或 send/recv，且生命周期门闩仍工作。

---

## B. 冻结前尽量清掉

### 2. UART 轮询 `getc`（16550 / PL011）

**现状**

- 门面已有：`uart_getc()` → `uart_16550A_getc` / `uart_pl011_getc`（`modules/driver/uart/`）。
- 两个实现都是 **`return 0` 空壳**，没有轮询 LSR/FR。
- `putc` 已是忙等 THR/TXFF；收包应对称做成 **轮询 RX 就绪再读**，**不要**开 RX IRQ（x86 接 IRQ 还要 IOAPIC，已明确不做冻结项）。

**要做**

- **16550**：等 `LSR` data-ready，读 `RBR`；无数据时约定返回值（建议：阻塞等到一字节，或提供非阻塞变体——二选一并写进头文件注释；冻结最小集用「阻塞轮询一字节」即可）。
- **PL011**：等 `FR` 非 RXFE，读 `DR`（注意错误位是否忽略）。
- 不改中断模型；不接 console 子系统大改。

**用途**

- 给将来 log/uart server、交互调试留驱动能力；本身不依赖 #46。

**验收**

- QEMU 串口：外部输入一字节，内核侧 `uart_getc` 能读到（可用临时测试调用或已有 console 路径验证）。

---

### 3. Log 最小收尾（原 #37 / #38）— **不是** Log IPC server

背景：[`log.md`](log.md)。设计目标是前后端分离（前端写 buffer，后端刷出）；**冻结只要求「最小可用」，不要求多核 IPI 刷屏。**

**现状**

- `struct log_buffer` / `LOG_BUFFER` 有骨架；`log_init` 会初始化 buffer 描述符。
- **热路径仍同步直写**：`log_put_byte` → `uart_putc`；`printk` 算了 `cur_buffer_*` 地址却几乎不当环形缓冲用。
- `log_init` 里直接 `CONSOLE_CLEAN_SCREEN(&X86_CHAR_CONSOLE)`；`log.h` 的 `COLOR_SET`/`pr_*` 绑着 **x86 VGA console** → aarch64 等路径也被拖进 x86 头（耦合）。

**冻结范围（做这些就够）**

1. **#37 倾向**：VGA / `X86_CHAR_CONSOLE` 从 `log_init` / 通用 `pr_*` 解耦（清屏、着色改成 arch 可选 sink，或 `#ifdef _X86_64_` / 弱符号）；log 模块本身只认「字节输出」抽象。
2. **#38 倾向（缩小版）**：前端写入 `LOG_BUFFER`（环形/分段），提供一次 **flush/刷出到 uart**（可在 `log_init` 后、或显式 `log_flush`、或 printk 满页时刷）。**不必**做多核 IPI 汇聚。
3. early / panic：**继续允许直写 uart**，不要为了「纯前端」拆掉救命路径。

**明确不在本项**

- #46 专用 Log IPC server、替代全部 `pr_*` 走端口（见 C）。
- 多核 IPI 把各核 log 打到同一 buffer（冻后再说）。

**验收**

- 非 x86 构建不再因 log 强依赖 VGA 头而别扭（或至少 x86-only 路径隔离清楚）。
- 常规 `pr_*` 能进 buffer 并被刷到串口；panic/early 仍能打出字。

---

### 4. 关键 API 随手补 Doxygen（原 #50）

- 范围：冻结会对外承诺的入口（尤其本轮 **Port hook**、已有 `pmm_set_reclaim_hook` / `configure_pmm_zones_hook`、trap vector alloc 等）。
- 不必全仓库扫一遍；改到哪个头文件就补哪个 `@brief` / 参数约定。
- 与 `GUIDE.md` §6 / `USING_CORE.md` 交叉引用一致即可。

---

## C. 非冻结（有需要再开）

### 5. Log / 输出走专用 IPC server（原 #46）

- 与上层 **uart_server** 同方向：运行期串口由 server 经 port 管；core early/panic 仍直写。
- 未拍板：全部 `pr_*` 走 IPC，还是只有用户 console。
- **依赖**：至少要有可用的驱动 `getc`（B.2）；x86 RX IRQ 另需 IOAPIC（远期）。
- **不要**和 B.3 混成一项。

### 6. 其它远期

| 项 | 说明 |
|----|------|
| #22 CPU topology | `cpu_topology.h` 等有头未接线 |
| #43 aarch64 DTB vs PCI | 两套节点描述并存 |
| PCI 使能 / IRQ / BAR | `pci_ops.c` 等 |
| aarch64 `boot.S` EL3/SPSR/ELR | 仅从 EL3 进内核时需要 |
| lockfree-ipc §8.3–8.5 | 批量/广播/调度感知 IPC，愿望单 |
| 真 capability / 本地 port handle | Port hook 之上的模型换皮，不是 A 项的延伸工单 |

Multiboot2 QEMU 校验、中断嵌套/RT 标志、IOAPIC 等：见 [`archive/TODO_DONE.md`](archive/TODO_DONE.md) 文末「明确不做」。MB2 header/mmap 硬化与 ARAT 已进 DONE（#65/#66）。

---

## 建议动手顺序

1. **Port A+B hook**（锁冻结面）  
2. **UART `getc`**（小、可测）  
3. **Log #37/#38 最小**（解耦 VGA + buffer/flush）  
4. Doxygen 随手  
5. 冻后再谈 #46 log/uart server  

Review 时重点看：A/B/C 分档是否符合你的冻结定义；B.3 与 C.5 是否拆得够开。
