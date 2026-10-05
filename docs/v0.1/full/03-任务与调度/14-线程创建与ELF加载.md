# 线程创建与 ELF 加载

v0.1 · 2026-09-26

本篇覆盖：`kernel/task/thread_loader.c`、`include/rendezvos/task/thread_loader.h`、`kernel/task/thread.c`（`create_thread` / `copy_thread` / `run_copied_thread`）、`include/arch/*/thread_arch.h` 与各 arch `arch_thread.c` / `arch_user_switch.S` / `arch_run_thread.S`。

loader **消费** `modules/elf` 格式库（`check_elf_header` / `for_each_program_header_64` / `print_elf_*`），但**不拥有**该模块——边界见 `09-平台模块/37-ELF加载辅助模块.md`。通用调度与 `thread_entry` 见 `13-线程与Task_Manager.md`。VSpace 所有权 / clear / schedule 换根见 `15-VSpace所有权与调度切换.md`。`page_slice` 见 `10-page_slice稀疏页索引.md`。

---

## 1. 概述

core **没有进程对象**。能调度的是线程；用户镜像进地址空间靠 loader helper；怎么回到用户态则分两条原语（下面叫 Path A / B）。加载与回到用户态可以拆开组合——写文档时最容易把它们混在一起。

按「你想做的事」选 API：

| 你想做 | 用 |
|--------|-----|
| 内核 server / 测试用例 kthread | `gen_thread_from_func` |
| 底层自管 vs / 入队 / flags | `create_thread` + 自己 add |
| bare-core / incbin 一次创建用户线程（harness） | `gen_thread_from_elf` → 体跑 `run_elf_program` |
| fork / clone 类复制用户上下文 | `copy_thread` → 自己入队 → `run_copied_thread` |
| 只把 ELF 装入已有 vs | `load_elf_to_vs`（+ 自管 clear / stack / 回用户） |

**Path A / B 说的是「怎么回到用户态」，不是「怎么 load」：**

| | Path A（同线程、已在 syscall） | Path B（新线程 / 无 in-flight syscall 帧） |
|--|--------------------------------|--------------------------------------------|
| 典型 | personality `execve` 原地换镜像 | `run_elf_program`、`run_copied_thread`、PID1 首次 drop |
| 提交 PC / SP | `arch_syscall_set_user_return(syscall_ctx, …)` | 合成 / 拷贝 trap frame + 同 API |
| 是否再 drop | **否**（由 syscall 返回路径承担） | **是** — `arch_return_to_user` |

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
| `create_thread` | 传入活引用 | **不得**再 put | 未赋值前 caller 仍持有 |
| `gen_thread_from_func` | 内部 get root | N/A | create 失败 put root；add 失败 `del_thread_structure`（含 put root） |
| `gen_thread_from_elf` | 内部 create + register | N/A | 回滚（见 §6.3） |
| `copy_thread` | 传入 | 成功不得 put | **任意失败路径 core 会 put 传入的 vs** |

`create_thread` 成功后 status 仍为 `init`，直到 `add_thread_to_manager` CAS → `ready`。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `thread_loader.c` / `.h` | `gen_thread_from_*`、`run_elf_program`、`load_elf_to_vs`、`generate_user_stack` |
| `thread.c` | `create_thread`、`copy_thread`、`run_copied_thread` |
| `modules/elf/*`（**→ 37**） | 格式校验 / Phdr 遍历宏 / 打印；本篇只调用 |
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

**func：** get root → create(`reserve=false`) → 名字拷贝 → `add_thread_to_manager(tm)`。失败：create 失败 put root；add 失败 `del_thread_structure`（含 put root）。成功才写可选 out 指针。

**elf：** create_vspace + register → create(`run_elf_program`, reserve=true, slice) → `generate_user_stack` + `arch_set_thread_user_sp` → flags=USER → add 本核。失败：按阶段 `del_thread_structure` / put 尚未移交的 vs。

### 6.4 `load_elf_to_vs`

ELF64 头与程序头布局（本 loader 只接受 ELFCLASS64；Phdr 通过 `page_slice_copy_to_buffer` 读出，不要求物理连续可 deref）：

```text
   Elf64_Ehdr（file offset 0，由 page_slice_lookup(0) 取 KVA）
   ┌──────────────────────────────────────────────┐
   │ e_ident[EI_MAG]: 7F 45 4C 46 (ELF)            │
   │ e_ident[EI_CLASS]: ELFCLASS64                 │
   │ e_type / e_machine / e_version                │
   │ e_entry ─────────── 用户入口 PC（run_elf_program 读此）│
   │ e_phoff ─────────── program header 表的文件偏移│
   │ e_shoff / e_flags / e_ehsize                  │
   │ e_phentsize ─────── 每个 Phdr 的大小          │
   │ e_phnum ─────────── program header 数量       │
   │ e_shentsize / e_shnum / e_shstrndx            │
   └──────────────────────────────────────────────┘
                │ e_phoff
                ▼
   Elf64_Phdr[e_phnum]（每个 e_phentsize 字节）
   ┌──────────────────────────────────────────────┐
   │ p_type:  PT_LOAD / PT_DYNAMIC / PT_INTERP ... │
   │ p_flags: R | W | X                           │
   │ p_offset ──── 段在文件中的偏移                │
   │ p_vaddr  ──── 段在虚地址空间中的目标 VA        │
   │ p_paddr / p_filesz / p_memsz / p_align        │
   └──────────────────────────────────────────────┘
   loader 第一遍只处理 PT_LOAD：把 [p_offset, p_offset+p_filesz) 拷到
   [p_vaddr, p_vaddr+p_filesz)；p_memsz > p_filesz 的部分按 0 填充（fill）。
```

1. ELFCLASS64；坏头 / 越界失败。
2. 第一遍 `PT_LOAD`：radix big-lock → `mm_user_utils_set_range_and_fill` → 有 `p_filesz` 则 `page_slice_copy_to_user`；跟踪 `max_load_end`。
3. 第二遍 `PT_DYNAMIC` → **stub**（打印 + SUCCESS）。
4. 输出页对齐 `max_load_end_out`。

Phdr 遍历：page0 KVA 做指针算术，内容经 copy_to_buffer（不要求 phdr 物理连续可 deref）。

### 6.5 `generate_user_stack` 与双重 −8

映射：`USER_SPACE_TOP` 向下 `thread_ustack_page_num` 页；USER | VALID | R | W。返回初值 SP ≈ 高地址 **− 8**。

`run_elf_program` 再 `user_sp -= 8` 并写 `*(u64*)=0`（最简 null 哨兵）。personality Path A 用自己的栈图像 builder，不是这套。

用户栈图像（Path B，`run_elf_program` 落地时）：

```text
   USER_SPACE_TOP ─┐
                  ▼
   ┌─────────────────────────────────┐
   │  user stack (8 页 = 32 KiB)      │  generate_user_stack 映射
   │                                 │  R | W | USER | VALID
   │                                 │
   │   ┌─────────────────────────┐  │  SP 初值 = 顶 − 8（generate_user_stack）
   │   │ 0x0000_0000_0000_0000   │  │  ← null word（run_elf_program 再 −8 写 0）
   │   └─────────────────────────┘  │
   │                                 │
   │   (后续 argv / envp / auxv 由    │
   │    compat 自建，core 不负责)     │
   └─────────────────────────────────┘  ← user_sp 传给 arch_syscall_set_user_return
```

两次 −8 的来源不同：第一次是 `generate_user_stack` 给「哨兵区」留 8 字节；第二次是 `run_elf_program` 在该哨兵位写一个 null word，作为最简栈底（无 argc/argv）。compat 若要真正的 argv/auxv，自建栈图像后用 Path A 提交，不走这套双重 −8。

### 6.6 `run_elf_program`（Path B）

在 **当前** `thread->vs` 上：`load_elf_to_vs` → 读 `e_entry` → 写 null word → 可选 `append_hooks->init`（失败只 `pr_error`，**仍继续 drop**）→ 重读 SP（hook 可改）→ `arch_empty_drop_trap_frame` + `arch_syscall_set_user_return` + `arch_return_to_user`。设计为不返回；走到 return 即错误。

### 6.7 `copy_thread` / `run_copied_thread`

前置：src 必须 USER，否则 put vs 失败。create(`run_copied_thread`, src hooks, vs, true, ret) → 拷 `*(kstack_bottom-1)` trap frame → `arch_ctx_refresh` + `arch_ctx_merge_from_src` → flags / name / copy hook。返回时仍为 **init**。子线程：`run_copied_thread` 写返回值后 `arch_return_to_user(kstack_bottom, NULL, ret)`。

### 6.8 回用户：软件原语与硬件出口

各 ISA 共用同一套 C API，对应到不同的「离开内核」指令（下文按现行主线路径分述）。

**共同步骤（Path B）：**

1. 在内核栈顶预留的 `trap_frame` 上填好用户 PC / SP / 返回值（`arch_empty_drop_trap_frame` 或拷贝父帧）。
2. `arch_syscall_set_user_return` 把「用户可见」的 PC / SP / ret 写进 frame（及 live 寄存器 / scratch）。
3. `arch_return_to_user` → `arch_drop_to_user`：把 RSP / SP 指到该 frame，再走与「从 syscall / sync trap 返回用户」同一条出口。

**x86_64（Intel SDM：SYSCALL / SYSRET、IA-32e）：**

- 用户 PC 写在 trap frame 的 `rcx`（SYSRET 用 RCX 当返回 RIP；见 Intel SDM Vol.2 `SYSCALL` / `SYSRET`——`SYSCALL` 把下一条指令地址存入 RCX，`SYSRET` 从 RCX 恢复 RIP）；返回值在 `rax`。
- 用户 RSP 走 `user_rsp_scratch`（及 `ctx->user_rsp`），出口路径再装回。`SYSCALL` 把用户 RSP 存入 `MSR_LSTAR` 配套的内核入口约定里，本实现用 per-CPU scratch 寄存。
- `arch_drop_to_user`：`cli`，`mov %rdi, %rsp`，跳 `arch_exit_kernel`；后者最终 **`sysretq`** 回环 3。与正常 syscall 返回共用出口，避免另造一套段 / RFLAGS 恢复。

**aarch64（ARM ARM：Exception return、SP_EL0、ELR_EL1）：**

- 用户 PC 写在 `tf->ELR`（`ELR_EL1` 保存陷入 EL1 时的返回地址，`eret` 从 `ELR_EL1` 恢复 PC）；返回值在 `REGS[0]`（x0）。
- 用户 SP 进 `SP_EL0`（及 `ctx->sp_el0`）；`tf->SP` 存的是**内核** trap save-area 指针，不是 EL0 SP。陷入路径 `SP_EL0` 由硬件自动保存到 `SP_EL0`，内核用 `SP_EL1` 作内核栈。
- `arch_drop_to_user`：`mov SP, X0`，跳 `el0_sync_trap_exit`；出口用 **`eret`** 回到 EL0（同时从 `SPSR_EL1` 恢复 PSTATE）。与 EL0 sync 返回共用向量尾。

Path A 不调 `arch_return_to_user`：正在处理的 syscall 返回时自然走同一条 `sysretq` / `eret`，只是 PC / SP 已被 `arch_syscall_set_user_return` 改成新镜像的入口。

---

## 7. 公开 API

本篇涉及的接口分布在：`thread_loader.h` 全套；`thread.h` 的 `create_thread` / `copy_thread` / `run_copied_thread`（创建路径以本篇为主；调度侧摘要见 `13`）；arch `thread_arch.h` 上 Path A/B 回用户与首次 ctx 钩子。说明改写自头文件 Doxygen，并已与 `.c` / `.S` 核对。

**本篇不涉及：** `check_elf_header` / `print_elf_*` → `37`；`schedule` / 入队原语细节 → `13`/`16`；VSpace create/register → `15`/`07`。

### 7.1 编排顺序（调用方必须遵守）

| 场景 | 顺序 |
|------|------|
| 内核 kthread | `gen_thread_from_func`（内部 get root → create → name → add）或自管 `create_thread` + `add_*` |
| harness 用户线程 | populate `page_slice` → **`gen_thread_from_elf`** → 调度后体跑 **`run_elf_program`**（load → drop） |
| 只装镜像 | （可选 clear）→ **`load_elf_to_vs`** → 自建栈 / Path A 或 B |
| fork 类 | 准备 vs（clone/share+get）→ **`copy_thread`** → `add_*` → 子跑 **`run_copied_thread`** |
| Path B drop | 填 / 拷 trap frame → `arch_syscall_set_user_return` → **`arch_return_to_user`** → `arch_drop_to_user` |
| Path A（compat） | 同线程 syscall 内 `arch_syscall_set_user_return`；**不**调 `arch_return_to_user` |

四 API 对照表见 §6.1。

### 7.2 创建工厂（`thread.h` / `thread_loader.h`）

```c
Thread_Base *create_thread(void *__func, const thread_append_hooks_t *hooks,
                           VSpace *vs, bool reserve_trap_frame,
                           int nr_parameter, ...);
error_t gen_thread_from_func(Thread_Base **out, kthread_func fn, char *name,
                             Task_Manager *tm, void *arg);
error_t gen_thread_from_elf(Thread_Base **out, const thread_append_hooks_t *hooks,
                            struct page_slice *slice);
Thread_Base *copy_thread(Thread_Base *src, VSpace *vs, u64 custom_return_value);
void run_copied_thread(u64 syscall_return_value);
```

| 接口 | 说明 |
|------|------|
| `create_thread` | `vs` 非 NULL；成功则**接管**活引用。设置好 `thread_entry`、kstack、`arch_set_new_thread_ctx`、tid 与参数。**不**入队、**不**调 hooks.init。失败不接管 `vs`。 |
| `gen_thread_from_func` | get `root_vspace` → create(`reserve=false`) → 名字堆拷贝 → `add_thread_to_manager(tm)`。create 失败 put root；add 失败 `del_thread_structure`（含 put root）。成功才写可选 out 指针。 |
| `gen_thread_from_elf` | create/register vs → create(`run_elf_program`, reserve=true, slice) → `generate_user_stack` → `thread_set_flags(USER)`（**整字赋值**）→ 挂到**本核** `core_tm`。失败按阶段回滚。**不** destroy slice。树内现网无调用方。 |
| `copy_thread` | src 必须是 USER；成功接管 `vs`，**任意失败都会 put 传入的 `vs`**。拷 trap frame 并 refresh/merge ctx；调 hooks.copy。返回时仍为 `init`，由调用方入队。 |
| `run_copied_thread` | 调 `arch_return_to_user(kstack, NULL, ret)`；若意外返回则 OR exit 再 `schedule`。 |

### 7.3 ELF / 栈 helper（`thread_loader.h`）

```c
error_t load_elf_to_vs(struct page_slice *slice, VSpace *vs, vaddr *max_load_end_out);
vaddr generate_user_stack(VSpace *vs);
error_t run_elf_program(struct page_slice *slice);
```

| 接口 | 说明 |
|------|------|
| `load_elf_to_vs` | 只接受 ELF64；`check_elf_header` → 扫 `PT_LOAD`（radix big-lock + fill + `page_slice_copy_to_user`）→ `PT_DYNAMIC` stub。可选输出页对齐的 `max_load_end`。 |
| `generate_user_stack` | 从 `USER_SPACE_TOP` 向下映射 `thread_ustack_page_num` 页；返回高地址再 **− 8**；失败返回 0。 |
| `run_elf_program` | 在当前线程 vs 上 load → 写 null word（再 −8）→ 可选 init（失败仍继续 drop）→ Path B。成功路径**不会**返回 `REND_SUCCESS`。 |

`elf_load_info_t`：传给 hooks.init 的元数据（slice / entry / max_load_end / user_sp / phnum / phentsize）。

### 7.4 arch 回用户与首次 ctx

```c
void arch_set_new_thread_ctx(Arch_Thread_Context *, void *func, void *kstack,
                             bool reserve_trap_frame);
vaddr arch_get_thread_user_sp(Arch_Thread_Context *);
void arch_set_thread_user_sp(Arch_Thread_Context *, vaddr);
void arch_empty_drop_trap_frame(struct trap_frame *, vaddr entry);
void arch_syscall_set_user_return(struct trap_frame *, Arch_Thread_Context *,
                                  vaddr pc, vaddr sp, u64 ret);
void arch_return_to_user(u64 kstack_bottom, const struct trap_frame *template,
                         u64 syscall_ret);
void arch_drop_to_user(struct trap_frame *tf);
void arch_ctx_refresh(Arch_Thread_Context *);
void arch_ctx_merge_from_src(Arch_Thread_Context *dst, const Arch_Thread_Context *src);
extern void run_thread(Thread_Init_Para *); /* asm；见 thread.h */
```

| 接口 | 要点 |
|------|------|
| `arch_set_new_thread_ctx` | 首次入口；`reserve` 时预留 trap 槽（aarch64 另强制 16B 对齐）。 |
| `arch_*_thread_user_sp` | 用户 SP 存在 ctx（x86 热路径另有 `user_rsp_scratch`）。 |
| `arch_empty_drop_trap_frame` | 清零帧；x86 写 `rcx=entry`，aarch64 写 `ELR=entry`。 |
| `arch_syscall_set_user_return` | Path A/B 共用：提交用户 PC / SP / 返回值。 |
| `arch_return_to_user` | `template!=NULL` 则拷到 kstack 槽；写返回值后调 `arch_drop_to_user`。**不**换页表根。 |
| `arch_drop_to_user` | 汇编出口（`sysretq` / `eret`）；见 §6.8。 |
| `arch_ctx_refresh` / `merge_from_src` | copy 时同步 live SP / TLS。 |
| `run_thread` | 按 ABI 调用 `thread_func_ptr(int_para[])`。 |

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

现网测试用例大量 `gen_thread_from_func`；`gen_thread_from_elf` **无**当前 C 调用方。ELF 加载时可能 `print_elf_ph64`。

```bash
cd core && make ARCH=x86_64 config && make all && make run
```

---

## 10. 限制与后续

- ELF32 / PT_INTERP / 真动态链接：无。
- `run_elf_program` 栈图像只有 null word。
- `gen_thread_from_elf` 不可跨核。
- init hook 失败仍 drop 用户。
- 完整 exec / argv / auxv / PID1 编排属 compat，不进本篇约定。

---

## 11. 变更记录

- 2026-10-05：任务 1/3/5 精读——§1/§9「测例」→「测试用例」；§1「揉成一团」→「混在一起」、「两条回用户原语」→「两条原语」、「一次造用户线程」→「一次创建用户线程」、「怎么回用户」→「怎么回到用户态」；§1 表「syscall 返回路径带走」→「由 syscall 返回路径承担」；§6.6「落到 return」→「走到 return」；§6.8 标题「回用户」→「回到用户态」、「落到不同的」→「对应到不同的」；§7.2「装好」→「设置好」。
- 2026-10-04：补硬件/架构知识——§6.4 加 Elf64_Ehdr / Elf64_Phdr 结构 ASCII 图与字段说明；§6.5 加用户栈图像布局图，写清两次 −8 的来源；§6.8 扩写 SYSRET 用 RCX 当返回 RIP、`eret` 从 ELR_EL1/SPSR_EL1 恢复 PC/PSTATE 的硬件语义（Intel SDM Vol.2 / ARM ARM）。语言润色：塞进→装入。
- 2026-10-02：`gen_thread_from_func` add 失败改为 `del_thread_structure` 回滚（与 elf 对齐）；成功才写 out 指针。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：与 ch37 硬分工——篇首/§5 不再把 `modules/elf/*` 写成「本篇覆盖」；loader 只消费格式库。
- 2026-09-26：§7 全文审阅——`thread_loader.h` / create·copy / Path B arch Doxygen；纠正 `run_elf_program`「成功返回 SUCCESS」、`gen_thread_from_func`「name 不拷贝」；写清编排与 add 回滚不对称。
- 2026-09-25：语言整理；§6.8 补 Path A/B 与 `sysretq` / `eret` 硬件出口对照；日期对齐本轮。
- 2026-08-29：整篇重做——Path A/B 叙述；四 API 对照表；harness 零调用方；双重 −8；copy 失败 put vs；`thread_set_flags` 赋值；init 失败仍 drop；纠正「默认用 gen_thread_from_elf」叙事。
- 2026-08-27：初稿。
- 2026-10-05：最终词句顺畅。
