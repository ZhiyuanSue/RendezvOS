# Trap 抽象与分类

v0.1 · 2026-09-25

本篇覆盖：`kernel/trap/trap.c`、`include/rendezvos/trap/trap.h`、`include/rendezvos/trap/trap_common.h`、`arch/{x86_64,aarch64}/trap/trap.c`、`trap_vec.S`、`kernel_entry.S`（x86 syscall 旁路）、**`arch/x86_64/trap/tss.c`（TSS.RSP0）**、`arch/*/boot/start_arch.c` 里的装表时机。

IRQ 池 / alloc 细节见 `25-IRQ向量分配与处理.md`；弱符号 `syscall` / ABI 见 `24-系统调用入口.md`；APIC / PIC、GIC 编程见平台中断两篇。**不存在**扁平路径 `include/rendezvos/trap.h`——真源是 `include/rendezvos/trap/trap.h`。

**不在本篇：** CMOS RTC 读墙钟（`arch/x86_64/time/rtc.c`）——不是中断控制器路径，见时间子系统篇。

---

## 1. 概述

两套硬件完全不像同一种东西：

| | x86_64 | aarch64 |
|--|--------|---------|
| 向量表 | 256 项 **IDT**，向量号 ≈ 入口 | **VBAR_EL1** 上 16 槽（异常类 × 来源），同步再靠 **ESR.EC** |
| 用户→内核 | fault / IRQ 走 IDT；**`syscall` 指令另走 LSTAR** | EL0 `SVC` / IRQ 都走同一套 VBAR |
| 故障信息 | CR2 + error_code | FAR_EL1 + ESR.ISS |
| 特权 | CPL（CS.RPL） | EL + SPSR.M |

若上层硬编码「向量 14 / EC 0x24」，每加一架构就裂一次。

汇编只建 `trap_frame*`；语义用 `trap_class`；分发用 per-CPU `irq_vector[]`。

历史动机（仍成立）：先有 x86 固定 IDT 数组外形；迁 aarch64 时**不改「数组分发」**，把 EC 塞进前 64 槽、IRQ 偏移 +64——**假统一、真映射**。本篇必须讲清这条链，不能只列 API。

手册对照（细节展开在 §6）：

- **Intel SDM Vol.3：** IDT / interrupt gates、`syscall`/`sysret`、CR2、error code。
- **ARM ARM：** VBAR_ELn、ESR_ELx.EC / ISS、FAR_ELx、`eret`、DAIF。

---

## 2. 目标与边界

**提供：** 统一 `trap_handler`；`trap_class` + `TRAP_COMMON`；fixed / direct 两级注册；装表→入口→返回的公共硬件语境；用户来源经 `trap_handler` 后的 `schedule` 钩子。

**不做：** POSIX signal / ptrace / Linux `do_page_fault`；APIC / GIC 寄存器细节；syscall 参数宏与弱符号实现；IRQ alloc 池策略全文。

---

## 3. 分层与调用方

**兼容层** — initcall 后：`register_fixed_trap(TRAP_CLASS_PAGE_FAULT, …)`；handler 内 `arch_populate_trap_info(tf, &info)` 读可移植字段。勿硬编码 14 / 0x24。

**arch boot** — `arch_start_core` → `init_interrupt()` →（平台 IRQ）→ `init_syscall` →（x86 再 `arch_enable_irq`）。见 §6.1 开中断时序差异。

**设备** — `irq_vector_alloc` + `register_irq_handler(..., IRQ_NEED_EOI)`（IRQ 篇）。

---

## 4. 数据结构与不变量

### 4.1 `trap_class`（语义，非硬件号）

`PAGE_FAULT` · `ILLEGAL_INSTR` · `BREAKPOINT` · `ALIGNMENT` · `DIVIDE_ERROR` · `OVERFLOW` · `FP_FAULT` · `GP_FAULT` · `STACK_FAULT` · `MACHINE_CHECK` · `SYSCALL` · `IRQ` · `DEBUG` · `DOUBLE_FAULT` · `SEGMENT_FAULT` · `SECURITY` · `VIRTUALIZATION` · `ASYNC_ABORT` · **`UNKNOWN`（必须末项）**。

`TRAP_CLASS_UNKNOWN <= 255`（存 `u8`）。

### 4.2 `TRAP_COMMON`

嵌入各 arch 的 `*_trap_info`：`tf` 指针（**不**复制整帧）、`trap_class`、`is_user` / `is_fatal`、页 fault 摘要（`fault_addr` / write / exec / present）、`error_code` / `arch_flags`。原始寄存器只在 `tf` 上。

### 4.3 `struct irq` / `irq_vector[]`

```c
struct irq {
        void (*irq_handler)(struct trap_frame *tf);
        u64 irq_attr;  /* USED, NEED_EOI, … */
};
DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ]);
```

- x86：`NR_IRQ` ≈ 256（IDT）。
- aarch64：`NR_IRQ` 更大；**`[0,64)` = EC**，**`[64,…)` = GIC intid + offset**。

### 4.4 两级注册（同一 trap id 互斥，后写覆盖）

| 方式 | API | 用途 |
|------|-----|------|
| fixed | `register_fixed_trap(class, handler, attr)` | arch 反向扫描映射，对匹配 id 装 wrapper |
| direct | `register_irq_handler(id, …)` | 要求 id 已 USED；写所有 CPU 槽 |

推荐：fault / syscall class → fixed；设备 → alloc + direct。
**陷阱：** `register_fixed_trap(TRAP_CLASS_IRQ)` 在 x86 反向扫描几乎只碰到 **NMI(2)**，**不会**绑 ≥32 设备向量。

### 4.5 `trap_handler` 不变量

```text
查 irq_vector[TRAP_ID(tf->trap_info)]
  有 handler → 调用
  无 → arch_unknown_trap_handler + kernel_panic
若 NEED_EOI → arch_eoi_irq(...)
若 !arch_int_from_kernel(tf) && core_tm → schedule(core_tm)
```

`arch_int_from_kernel`：**x86 看 `cs == kernel CS`；aarch64 看 `SPSR.M != EL0`**（不要把 aarch64 `trap_info` 里 OR 的 SRC_EL 位当权威——Lower EL 入口也会写 SRC_EL_1）。

### 4.6 `trap_frame` 指针约定

内核栈上的现场块；入口把指针放进 `%rdi` / `x0`。

| | x86 | aarch64 |
|--|-----|---------|
| 身份 | `trap_info`=向量；通用寄存器；error_code（真或假槽）；HW 的 rip / cs / rflags / rsp / ss | `trap_info` 先类型后改写为 EC 或 IRQ id；`REGS[31]`；SPSR / ELR / SP / ESR / FAR |
| 用户入口 | **swapgs**；特权切换用 **TSS.RSP0**（**IST=0**，不是已用 IST1–7） | 入口 DAIF 被硬件屏蔽；返回 `eret` |
| SP 字段 | HW 压的用户 / 内核 rsp | entry 存的是异常后的 **SP_EL1（内核栈）**，不是 `SP_EL0` |

上层改「应回用户」的 PC / 返回值；栈切换已由 entry 完成。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `trap_common.h` / `trap/trap.h` | class、TRAP_COMMON、公开 API |
| `kernel/trap/trap.c` | 池、`trap_handler`、EOI、schedule |
| `arch/x86_64/trap/*` | IDT、`x86_trap_class_map`、populate、`trap_vec.S`、**`tss.c`** |
| `arch/x86_64/trap/kernel_entry.S` | **syscall MSR 旁路** |
| `arch/aarch64/trap/*` | VBAR 16 槽、`trap_vec.S`、EC 映射、GIC 读号 |
| `arch/*/boot/start_arch.c` | 装表 / enable / `init_syscall` 顺序 |
| `kernel/system/syscall.c` | weak `syscall` |

---

## 6. 流程（硬件链）

### 6.1 何时能捕 trap

Boot 汇编（`boot.S`）**不**提前装 IDT / VBAR。可捕窗口从 `arch_start_core` → `init_interrupt` 起。compat 的 PF fixed 更晚（initcall）——此前未注册向量会 panic。

**开中断时序（架构不对称，必写）：**

| | x86 | aarch64 |
|--|-----|---------|
| 装表 | `init_interrupt` → lidt | `set_vbar_el1` |
| 开 IRQ | **`init_syscall`（装 LSTAR）之后**才 `arch_enable_irq` | **`arch_init_interrupt` 内立刻 enable**（早于 GIC CPU interface 与 syscall 注册） |

### 6.2 x86：IDT 链（Intel SDM：IDT / interrupt gates / TSS）

```text
硬件异常/IRQ
  → IDT[vector]（中断门，DPL=kernel，IST=0→TSS.RSP0）
  → trap_N / trap_entry(id, error_code_num)
       · error_code_num==1：无硬件 error_code 的向量，软件先 sub 8B 占槽
       · error_code_num==0：#DF/#TS/#NP/#SS/#GP/#PF/#AC 等硬件已压 error_code
       · 存寄存器；用户 CPL（CS.RPL≠0）：swapgs；trap_info=向量
  → trap_handler → [handler] → 可选 EOI → 用户来源则 schedule
  → trap_exit：用户 swapgs；iretq
```

映射要点：`#PF→PAGE_FAULT`，`#UD→ILLEGAL_INSTR`，`#GP→GP_FAULT`，…；**`#NMI(2)→TRAP_CLASS_IRQ`**（无独立 NMI class）；向量 ≥32 在 populate 时标 IRQ。

#### TSS.RSP0（`tss.c`）——用户→内核切栈的硬件钉

SDM：特权级提升进入中断 / 异常时，若 IDT gate 的 **IST=0**，CPU 从当前任务的 **TSS.RSP0** 取内核栈，再压 SS:RSP / RFLAGS / CS:RIP（及可选 error_code）。

本仓库：

- `DEFINE_PER_CPU(struct TSS, cpu_tss)`；`prepare_per_cpu_tss` 把 RSP0 写成该核 `boot_stack_bottom`，再 **`ltr`** 装 TSS 选择子。
- 之后线程切换时 `switch_to` **改写 TSS.RSP0** 为新线程 `kstack_bottom`（见任务篇 §6.6）——否则从用户进核会踩错栈。
- **IST1–7 未用**；双 fault 等也不走独立 IST 栈。

没有正确的 per-CPU TSS + 切换时更新 RSP0，用户态 IRQ / #PF 会在错误栈上建 `trap_frame`。

#### #PF error_code（SDM；old 笔记保留）

硬件压在栈上的 error_code 位（populate 会拆进 `TRAP_COMMON` / arch 字段）：

| bit | 名 | 含义 |
|-----|-----|------|
| 0 | P | 0=不存在，1=保护违反 |
| 1 | W/R | 0=读，1=写 |
| 2 | U/S | 0=内核，1=用户 |
| 3 | RSVD | 保留位违反 |
| 4 | I/D | 0=数据，1=取指 |
| 5+ | PK / SS / HV… | 较新扩展；本实现按需解析 |

故障线性地址在 **CR2**。

### 6.3 x86：syscall 旁路（不经 `trap_handler`）

```text
syscall → LSTAR=arch_enter_kernel
  → swapgs、切段、TSS 内核栈
  → 栈上拼类 trap_frame 槽
  → sti; call syscall; …; sysretq
```

**没有**统一末尾 `schedule`（除非 `syscall()` 或别处自己调）。与 aarch64 SVC **不对称**——同名 `TRAP_CLASS_SYSCALL`，生命周期不同。细节见系统调用入口篇。

### 6.4 aarch64：VBAR 链（ARM ARM：VBAR / ESR / eret）

VBAR_EL1 指向 2KB 对齐的表；每槽 0x80 字节。本仓库 `trap_vec.S` 接线：

| 偏移类 | 槽 | 入口 |
|--------|-----|------|
| Current EL, SP_EL0 | Sync/IRQ/FIQ/SError | 全 **`unexpected_trap`**（未建完整 frame） |
| Current EL, SP_ELx | Sync / IRQ / FIQ | `curr_el_*` → `get_curr_el_trap_info` → `trap_handler` |
| Current EL, SP_ELx | SError | `unexpected_trap` |
| Lower EL, AArch64 | Sync / IRQ / FIQ | `low_el_*`（用户路径；Sync 出口符号 `el0_sync_trap_exit` 供 Path B drop） |
| Lower EL, AArch64 | SError | `unexpected_trap` |
| Lower EL, AArch32 | 全部 | `unexpected_trap` |

```text
异常 → VBAR_EL1 + offset
  → el{0,1}_trap_entry(TRAP_TYPE_*)
  → get_curr_el_trap_info：
       SYNC: trap_info = ESR.EC
       IRQ:  trap_info = 64 + gic.read_irq_num()   // 读 GICC_IAR
       FIQ:  不改写（解码空）
  → trap_handler → EOI?/schedule?
  → eret
```

EC 映射：`0x20/21/24/25→PAGE_FAULT`（DFSC `0x21` 在 populate 可再标 ALIGNMENT）；`0x15`（EL0 SVC）与 `0x18`（same-EL 防御）→ `SYSCALL`；SError → `ASYNC_ABORT`。
**FIQ：** 入口在，解码空 → 易未注册 panic。`unexpected_trap` 直接调 unknown **未建 frame**（参数无效）。

### 6.5 SVC vs x86 syscall（schedule）

| 路径 | 经 `trap_handler`？ | 末尾 schedule？ |
|------|---------------------|-----------------|
| x86 `syscall` 指令 | **否** | **否**（统一路径） |
| aarch64 EL0 SVC | **是**（fixed wrapper → `arch_syscall_helper` → `syscall`） | **是**（用户来源） |
| 用户态 PF / 用户态 IRQ | 是 | 是 |
| 内核态 trap | 是 | **否** |

经 `trap_handler` 的用户路径还会顺带 `kalloc_process_cross_cpu_frees` / `ebr_try_reclaim`（见调度 / EBR 篇）。

### 6.6 Fault / Trap / Abort（硬件语感）

旧笔记仍有用：Fault 可重启（如 #PF）；Trap 报在指令后（如 #BP）；Abort / SError 异步。本篇分类表是**语义桶**，不是把 Intel / ARM 手册三类一一对应——写上层时用 `trap_class`，读手册时再回 arch map。

---

## 7. 公开 API

```c
void trap_handler(struct trap_frame *tf);
void init_interrupt(void);

error_t register_fixed_trap(enum trap_class c,
                            void (*handler)(struct trap_frame *),
                            u64 attr);
void register_irq_handler(int irq_num,
                          void (*handler)(struct trap_frame *),
                          u64 irq_attr);

/* arch */
void arch_populate_trap_info(struct trap_frame *tf, /* arch_trap_info * */);
bool arch_int_from_kernel(struct trap_frame *tf);
void arch_eoi_irq(u64 trap_info);
```

向量池 alloc / free / reserve → IRQ 篇。

---

## 8. 多架构

上表。riscv / loongarch 非 v0.1 主线验收。compat 真相：`linux_page_fault_irq` / `linux_unknown_trap` 用 fixed class——文档化此路径，勿文档化硬编码向量。

---

## 9. 测试

间接：PF、syscall、timer IRQ、SMP IPI。无单独「class 表」测例。本篇未复测。

---

## 10. 限制与后续

- 双入口不对称（x86 syscall 旁路）。
- NMI 并进 IRQ class；无独立 NMI 语义。
- aarch64 开中断早于 GIC CPU iface / syscall 注册。
- `fixed_trap_attrs[]` 只写不读（死存储）。
- FIQ / unexpected_trap 缺口。
- IST 未用。

---

## 11. 变更记录

- 2026-09-25：补遗漏模块——**TSS.RSP0 / `tss.c`**；VBAR 16 槽接线表；#PF error_code 位；划清 RTC 不属本篇；文首补 SDM / ARM ARM 对照入口。
- 2026-09-12：删除对已清除的 `interrupt_init` 死声明 / E12 的叙述。
- 2026-08-29：整篇重做——硬件深度（IDT / VBAR / CPL / EL）；双入口与 schedule 不对称；装表 / 开中断时序；NMI≡IRQ；EC 命名空间；纠正扁平 trap.h 路径与「x86 也 fixed SYSCALL」误写；IST=0。
- 2026-08-27：初稿（偏 API 清单）。
