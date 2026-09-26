# 线程创建与 ELF 加载

v0.1 · 2026-09-25

本篇覆盖：`kernel/task/thread_loader.c`、`include/rendezvos/task/thread_loader.h`、`kernel/task/thread.c`（`create_thread` / `copy_thread` / `run_copied_thread`）、`modules/elf/*`、`include/arch/*/thread_arch.h` 与各 arch `arch_thread.c` / `arch_user_switch.S` / `arch_run_thread.S`。

通用调度与 `thread_entry` 见 `13-线程与Task_Manager.md`。VSpace 所有权 / clear / schedule 换根见 `15-VSpace所有权与调度切换.md`。`page_slice` 见 `10-page_slice稀疏页索引.md`。纯 ELF 头解析模块见 `09-平台模块/37-ELF加载辅助模块.md`。

---

## 1. 概述

core **没有进程对象**。能调度的是线程；用户镜像进地址空间靠 loader helper；怎么回到用户态则分两条回用户原语（下面叫 Path A / B）。加载和回用户可以拆开组合——写文档时最容易把它们揉成一团。

按「你想做的事」选 API：

| 你想做 | 用 |
|--------|-----|
| 内核 server / 测例 kthread | `gen_thread_from_func` |
| 底层自管 vs / 入队 / flags | `create_thread` + 自己 add |
| bare-core / incbin 一次造用户线程（harness） | `gen_thread_from_elf` → 体跑 `run_elf_program` |
| fork / clone 类复制用户上下文 | `copy_thread` → 自己入队 → `run_copied_thread` |
| 只把 ELF 塞进已有 vs | `load_elf_to_vs`（+ 自管 clear / stack / 回用户） |

**Path A / B 说的是「怎么回用户」，不是「怎么 load」：**

| | Path A（同线程、已在 syscall） | Path B（新线程 / 无 in-flight syscall 帧） |
|--|--------------------------------|--------------------------------------------|
| 典型 | personality `execve` 原地换镜像 | `run_elf_program`、`run_copied_thread`、PID1 首次 drop |
| 提交 PC / SP | `arch_syscall_set_user_return(syscall_ctx, …)` | 合成 / 拷贝 trap frame + 同 API |
| 是否再 drop | **否**（syscall 返回路径带走） | **是** — `arch_return_to_user` |

core **没有** `task_exec_enter_user`。Path A 的编排在 personality；core 只提供 `load_elf_to_vs` / `generate_user_stack` / arch 原语。现网 Linux PID1 往往是「同款 `linux_exec_replace_image`（clear + load + stack）+ Path B drop」——**不走** `gen_thread_from_elf`。树内 `gen_thread_from_elf` **零调用方**，仍是保留的 harness API，不是默认启动路径。

---

## 2. 目标与边界

**提供：** TCB + kstack + arch ctx 工厂；内核 / 用户便捷封装；ELF64 PT_LOAD → VSpace；user stack 映射；Path B drop 所需 arch 钩子；`copy_thread` 复制用户 trap frame。

**不做：** 完整 execve（shebang、interpreter、标准 AT_* auxv）；动态链接（`PT_DYNAMIC` stub）；ELF32；自动跨核（`gen_thread_from_elf` 固定本核 TM）；`page_slice_destroy`（slice 生命周期归 caller）。

---

## 3. 分层与调用方

**内核 / initcall** — `gen_thread_from_func(&p, fn, name, tm, arg)`；`tm` = `percpu(core_tm)` 或 `task_manager_for_cpu`。

**harness / 将来 incbin** — `gen_thread_from_elf(&p, hooks, slice)`；slice 须已 populate（含 header 的 pgoff 0）。

**兼容层** — 通常：`create_vspace` → `create_thread(..., USER 自设, reserve=true)` → 入队；exec 路径复用 `load_elf_to_vs` + 自建栈图像 + Path A 或 Path B drop。fork：`clone_vspace` 或共享 AS → `copy_thread` → `add_*`。

**append：** `init` 仅 `run_elf_program`（现 Linux hooks 常为 NULL）；`copy` 在 `copy_thread`；`fini` 见 EBR 篇。`create_thread` **不**调 init。

常见错误：create 成功后再 put 同一 vs；对用户线程忘 `THREAD_FLAG_USER`；`copy_thread` 源非 USER；对正在 syscall 返回的线程再调 `arch_return_to_user`。

---

## 4. 数据结构与不变量

### 4.1 Thread_Init_Para

`thread_func_ptr` + `int_para[NR_ABI_PARAMETER_INT_REG]`。varargs 超限忽略。ELF 路径：`int_para[0] = page_slice*`；copy 路径：`int_para[0] = custom_return_value`。

### 4.2 elf_load_info_t

`slice` / `entry_addr` / `max_load_end`（页对齐）/ `user_sp` / `phnum` / `phentsize`。**仅** `run_elf_program` 填好传给 `append_hooks->init`。Path A / Linux replace_image **不**构造此结构。

### 4.3 page_slice 与 ELF

slice = 内核侧稀疏文件镜像，**不是**用户 VSpace。loader：`page_slice_lookup(0)` 取 header KVA；Phdr 用 `page_slice_copy_to_buffer`；文件字节用 `page_slice_copy_to_user`。core **从不** destroy slice。

### 4.4 栈常量

`thread_kstack_page_num = 2`；`thread_ustack_page_num = 8`（`thread_boot.c`）。

### 4.5 vs 所有权

| API | vs 来源 | 成功后 caller | 失败 |
|-----|---------|---------------|------|
| `create_thread` | 传入 live ref | **不得**再 put | 未赋值前 caller 仍持有 |
| `gen_thread_from_func` | 内部 get root | N/A | create 失败则 put root |
| `gen_thread_from_elf` | 内部 create + register | N/A | 回滚（见 §6.3） |
| `copy_thread` | 传入 | 成功不得 put | **任意失败路径 core 会 put 传入的 vs** |

`create_thread` 成功后 status 仍为 `init`，直到 `add_thread_to_manager` CAS → `ready`。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `thread_loader.c` / `.h` | `gen_thread_from_*`、`run_elf_program`、`load_elf_to_vs`、`generate_user_stack` |
| `thread.c` | `create_thread`、`copy_thread`、`run_copied_thread` |
| `modules/elf/*` | header 校验、phdr 遍历 helper、打印 |
| `mm_user_utils` / radix | 用户 range map |
| `page_slice_copy` | 文件 → user |
| arch `arch_thread` / `arch_user_switch` / `arch_run_thread` | ctx、drop、syscall return、ABI call |

---

## 6. 流程

### 6.1 四条创建 API 对照

| | `create_thread` | `gen_thread_from_func` | `gen_thread_from_elf` | `copy_thread` |
|--|-----------------|------------------------|----------------------|---------------|
| 入口 | 任意 | kthread | 固定 `run_elf_program` | 固定 `run_copied_thread` |
| reserve_trap_frame | 参数 | **false** | **true** | **true** |
| USER flag | 不设 | 不设 | `thread_set_flags(USER)`（**整字赋值**，清其它位） | 拷 `src->flags` |
| user stack | 否 | 否 | enqueue **前** `generate_user_stack`（PT_LOAD 更晚） | 否 |
| 入队 | **否** | **是**（指定 tm） | **是**（固定 `percpu(core_tm)`） | **否** |
| hooks.init | 不调 | 无 hooks | 晚到 `run_elf_program` | 调 **copy** |

### 6.2 `create_thread` 骨架

`new_thread_structure` → tid / kstack / `arch_set_new_thread_ctx(..., thread_entry, …, reserve_trap_frame)` → 填 `init_parameter` → `thread->vs = vs`。失败已赋值则 `del_thread_structure`（会 put vs）。

### 6.3 `gen_thread_from_func` / `gen_thread_from_elf`

**func：** get root → create(`reserve=false`) → 名字拷贝 → `add_thread_to_manager(tm)`。注意：若 **add 失败不回滚**（thread 已持有 root vs）——与 ELF 路径不同。

**elf：** create_vspace + register → create(`run_elf_program`, reserve=true, slice) → `generate_user_stack` + `arch_set_thread_user_sp` → flags=USER → add 本核。失败：按阶段 `del_thread_structure` / put 尚未移交的 vs。

### 6.4 `load_elf_to_vs`

1. ELFCLASS64；坏头 / 越界失败。
2. 第一遍 `PT_LOAD`：radix big-lock → `mm_user_utils_set_range_and_fill` → 有 `p_filesz` 则 `page_slice_copy_to_user`；跟踪 `max_load_end`。
3. 第二遍 `PT_DYNAMIC` → **stub**（打印 + SUCCESS）。
4. 输出页对齐 `max_load_end_out`。

Phdr 遍历：page0 KVA 做指针算术，内容经 copy_to_buffer（不要求 phdr 物理连续可 deref）。

### 6.5 `generate_user_stack` 与双重 −8

映射：`USER_SPACE_TOP` 向下 `thread_ustack_page_num` 页；USER | VALID | R | W。返回初值 SP ≈ 高地址 **− 8**。

`run_elf_program` 再 `user_sp -= 8` 并写 `*(u64*)=0`（minimal null）。personality Path A 用自己的栈图像 builder，不是这套。

### 6.6 `run_elf_program`（Path B）

在 **当前** `thread->vs` 上：`load_elf_to_vs` → 读 `e_entry` → 写 null word → 可选 `append_hooks->init`（失败只 `pr_error`，**仍继续 drop**）→ 重读 SP（hook 可改）→ `arch_empty_drop_trap_frame` + `arch_syscall_set_user_return` + `arch_return_to_user`。设计为不返回；落到 return 即错误。

### 6.7 `copy_thread` / `run_copied_thread`

前置：src 必须 USER，否则 put vs 失败。create(`run_copied_thread`, src hooks, vs, true, ret) → 拷 `*(kstack_bottom-1)` trap frame → `arch_ctx_refresh` + `arch_ctx_merge_from_src` → flags / name / copy hook。返回时仍为 **init**。子线程：`run_copied_thread` 写返回值后 `arch_return_to_user(kstack_bottom, NULL, ret)`。

### 6.8 回用户：软件原语与硬件出口

两架构共用同一套 C API，落到不同的「离开内核」指令。

**共同步骤（Path B）：**

1. 在内核栈顶预留的 `trap_frame` 上填好用户 PC / SP / 返回值（`arch_empty_drop_trap_frame` 或拷贝父帧）。
2. `arch_syscall_set_user_return` 把「用户可见」的 PC / SP / ret 写进 frame（及 live 寄存器 / scratch）。
3. `arch_return_to_user` → `arch_drop_to_user`：把 RSP / SP 指到该 frame，再走与「从 syscall / sync trap 返回用户」同一条出口。

**x86_64（Intel SDM：SYSCALL / SYSRET、IA-32e）：**

- 用户 PC 写在 trap frame 的 `rcx`（SYSRET 用 RCX 当返回 RIP）；返回值在 `rax`。
- 用户 RSP 走 `user_rsp_scratch`（及 `ctx->user_rsp`），出口路径再装回。
- `arch_drop_to_user`：`cli`，`mov %rdi, %rsp`，跳 `arch_exit_kernel`；后者最终 **`sysretq`** 回环 3。与正常 syscall 返回共用出口，避免另造一套段 / RFLAGS 恢复。

**aarch64（ARM ARM：Exception return、SP_EL0、ELR_EL1）：**

- 用户 PC 写在 `tf->ELR`；返回值在 `REGS[0]`（x0）。
- 用户 SP 进 `SP_EL0`（及 `ctx->sp_el0`）；`tf->SP` 存的是**内核** trap save-area 指针，不是 EL0 SP。
- `arch_drop_to_user`：`mov SP, X0`，跳 `el0_sync_trap_exit`；出口用 **`eret`** 回到 EL0。与 EL0 sync 返回共用向量尾。

Path A 不调 `arch_return_to_user`：正在处理的 syscall 返回时自然走同一条 `sysretq` / `eret`，只是 PC / SP 已被 `arch_syscall_set_user_return` 改成新镜像的入口。

---

## 7. 公开 API

| API | 要点 |
|-----|------|
| `create_thread` | 吞 vs；不入队 |
| `gen_thread_from_func` | 内核；入队指定 tm |
| `gen_thread_from_elf` | harness；本核入队；不 destroy slice |
| `load_elf_to_vs` / `generate_user_stack` | 映射 helper |
| `run_elf_program` | Path B 体；应不返回 |
| `copy_thread` / `run_copied_thread` | 未入队；失败 put vs |

签名以头文件为准。

---

## 8. 多架构

| 符号 | 职责 |
|------|------|
| `arch_set_new_thread_ctx` | 首次内核入口 + 可选预留 trap slot |
| `arch_set/get_thread_user_sp` | 用户 SP（+ x86 scratch） |
| `arch_empty_drop_trap_frame` | 无父帧首次进用户 |
| `arch_syscall_set_user_return` | A / B 共用：提交 user PC / SP |
| `arch_return_to_user` | Path B / copy 实际 drop |
| `arch_ctx_refresh` / `merge_from_src` | copy 同步 live SP / TLS |
| `run_thread` | 按 ABI 调 `thread_func_ptr` |

硬件出口见 §6.8。aarch64 预留 trap frame 后强制 16B 对齐。riscv / loongarch 非 v0.1 主线。

---

## 9. 测试

现网测例大量 `gen_thread_from_func`；`gen_thread_from_elf` **无**当前 C 调用方。ELF 加载时可能 `print_elf_ph64`。本篇未复测。

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

---

## 10. 限制与后续

- ELF32 / PT_INTERP / 真动态链接：无。
- `run_elf_program` 栈图像只有 null word。
- `gen_thread_from_elf` 不可跨核；add 失败与 func 路径回滚不对称。
- init hook 失败仍 drop 用户。
- 完整 exec / argv / auxv / PID1 编排属 compat，不进本篇契约。

---

## 11. 变更记录

- 2026-09-25：语言整理；§6.8 补 Path A/B 与 `sysretq` / `eret` 硬件出口对照；日期对齐本轮。
- 2026-08-29：整篇重做——Path A/B 叙述；四 API 对照表；harness 零调用方；双重 −8；copy 失败 put vs；`thread_set_flags` 赋值；init 失败仍 drop；纠正「默认用 gen_thread_from_elf」叙事。
- 2026-08-27：初稿。
