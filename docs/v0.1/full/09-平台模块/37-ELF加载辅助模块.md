# ELF 加载辅助模块

v0.1 · 2026-09-27

本篇覆盖：`modules/elf/elf.c`、`elf_print.c`、`include/modules/elf/*.h`（`elf.h` / `elf_common.h` / `elf_32.h` / `elf_64.h` / `elf_print.h`）。

**映射、栈、回用户、Path A/B、`gen_thread_from_elf` / `thread_loader`** 全在 `03-任务与调度/14-线程创建与ELF加载.md`。本篇只钉**格式库边界**，禁止再抄一套加载流程。

**格式对照：** System V ABI / ELF —— `e_ident` magic `0x7F 'E' 'L' 'F'`、`EI_CLASS` / `EI_DATA` / `EI_VERSION`、Program Header（`PT_LOAD` 等）、`e_machine`（`EM_X86_64` / `EM_AARCH64` / …）。本仓库把结构体与常量落在头文件里，供 loader 消费。

---

## 1. 概述

ELF 模块做三件事：校验文件头、按 class 读字段、（可选）打印 Ehdr/Phdr/Shdr。**不碰 VSpace、不分配页、不写用户映射。**

`thread_loader` 调 `check_elf_header`、要求 `ELFCLASS64`、用 `for_each_program_header_64` 扫 Phdr 做 `PT_LOAD`；`PT_DYNAMIC` 在 loader 侧 stub。本模块不负责 relocate、动态链接、符号解析、Interpreter。

### 1.1 为什么拆成「薄格式库」

加载策略（从哪读文件、怎么 map、栈怎么摆）会随任务模型变；ELF 头布局相对稳定。把校验 / 遍历宏独立出来，loader 与调试打印共用同一套常量，避免在 `thread_loader.c` 里散落 magic number。

---

## 2. 目标与边界

**提供：** magic + `EI_VERSION==EV_CURRENT` 校验；class / data / osabi 查询；`e_type` / `e_machine` 读取；32/64 phdr·shdr 遍历宏；DEBUG 下打印。

**不做：** `load_elf_to_vs`、栈图像、drop、hooks（→ 14）；ELF32 加载（loader 直接拒绝）；完整动态链接；校验 `e_machine` 是否匹配当前 ISA（**loader 也不校验**——错架构镜像可走到 map 后再炸）。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `kernel/task/thread_loader.c` | `check_elf_header` → `get_elf_class` → `for_each_program_header_64`；调试可调 `print_elf_ph64` |
| 调试 | `print_elf_header` / `print_elf_*`（默认无输出，见下） |

无独立 `single_elf_*` 测例。

---

## 4. 数据结构与不变量

- 类型与常量：`elf_common.h`（magic、`ET_*`、`EM_*`、`PT_*`、`SHT_*`…）；`Elf32_*` / `Elf64_*` 在分头。  
- `check_elf_header`：**只**查 `EI_MAG0..3` 与 `EI_VERSION`；不查 class、endian、machine、phentsize。  
- 遍历宏假定调用方已确认 class，且 ph/sh 表落在可读缓冲内（loader 另做 `e_phoff` 越界检查）。  
- `elf64_hash`：SysV 哈希算法有实现，**头文件未导出、运行路径未用**——半死代码。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `elf.c` | `check_elf_header` / `get_elf_type` / `get_elf_machine` / `elf64_hash` |
| `elf.h` | inline `get_elf_class|data_encode|osabi|…`；遍历宏 |
| `elf_print.c` | 打印；默认 `debug → pr_off` |
| `thread_loader.c` | **唯一正式消费者**（14 篇） |

---

## 6. 流程

```text
loader:
  check_elf_header(elf_start)     // magic + version
  get_elf_class == ELFCLASS64     // 否则拒绝
  （可选）边界检查 ph 表
  for_each_program_header_64:
    PT_LOAD  → map（14）
    PT_DYNAMIC → stub / 忽略策略（14）
  不调用 get_elf_machine 做 ISA 门闩
```

打印：`elf_print.c` 顶部 `// #define DEBUG` 默认注释掉 → `debug` 宏为 `pr_off`，`print_elf_*` 调用仍安全但无字。需要时取消注释并开 `_LOG_DEBUG_`。

---

## 7. 公开 API

本篇拥有：ELF **格式库**——`check_elf_header` / `get_elf_*`、32/64 Phdr·Shdr 遍历宏（`elf.h`）、调试打印（`elf_print.h`）、常量与结构体（`elf_common.h` / `elf_32.h` / `elf_64.h`）。以头文件注释为准（`elf.h` / `elf_print.h`；已与 `.c` 核对）。

**本篇不拥有：** `load_elf_to_vs` / `gen_thread_from_elf` / `run_elf_program` / 栈 / Path A·B → `14`；`page_slice` 拷贝 → `10`；VSpace map → `07`/`08`。

**不存在于公开面：** `elf64_hash`（`.c` 内实现，未导出、无调用方）。

### 7.1 编排顺序（调用方须遵守）

| 步骤 | API | 说明 |
|------|-----|------|
| 1 | **`check_elf_header`** | 仅 magic + `EI_VERSION` |
| 2 | **`get_elf_class` == `ELFCLASS64`** | loader 拒绝 32-bit |
| 3 | （可选）`e_phoff` 越界检查 | loader 侧，非本篇 |
| 4 | **`for_each_program_header_64`** | 扫 `PT_LOAD` / `PT_DYNAMIC`（映射语义在 `14`） |

本篇**不**校验 `e_machine` / endian 与宿主一致——错架构可走到 map 后再炸。

### 7.2 校验与字段

```c
bool check_elf_header(vaddr elf_header_ptr);
u8  get_elf_class / get_elf_data_encode / get_elf_osabi / get_elf_abi_version(…);
u16 get_elf_type(vaddr);
u16 get_elf_machine(vaddr);   /* 坏 class 时返回 ET_NONE（历史哨兵） */
```

| 接口 | 说明 |
|------|------|
| `check_elf_header` | `EI_MAG0..3` + `EV_CURRENT`；不查 class/machine。 |
| `get_elf_type` / `_machine` | 按 class 读 32 或 64 头。 |

### 7.3 遍历宏

```c
for_each_program_header_32/64(elf_header_ptr) { /* phdr_ptr */ }
for_each_section_header_32/64(elf_header_ptr) { /* shdr_ptr */ }
```

假定 class 已确认、表在可读缓冲内。正式加载路径只用 **64** 程序头宏。

### 7.4 打印（`elf_print.h`）

```c
void print_elf_header(vaddr);
void print_elf_ph32/64(...); void print_elf_sh32/64(...);
void print_elf_machine(u16);
```

默认 `DEBUG` 关闭 → `pr_off`；调用无害。loader 调试可偶发 `print_elf_ph64`。

---

## 8. 多架构

格式层与 ISA 无关；`e_machine` 常量覆盖 x86_64 / AArch64 / RISC-V / LoongArch。真正「只跑本机 ELF」的约束应由 loader 补——**当前未补**。

---

## 9. 测试

无独立测例；随用户 ELF / harness 加载路径间接覆盖。本篇未复测。

---

## 10. 限制与后续

- 打印默认关闭。  
- `elf64_hash` 半死。  
- 不校验 `e_machine` / endian 与宿主一致。  
- 加载语义永远回链 14，勿在本篇膨胀。

---

## 11. 变更记录

- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——`elf.h`/`elf_print.h` Doxygen；与 `14` 硬分工；标明 `elf64_hash` 未导出。  
- 2026-09-25：语言轮——ELF 格式字段与 loader 消费边界；明确不校验 machine；打印默认 `pr_off`。  
- 2026-08-29：整篇重做——薄边界；与 03/14 硬分工。  
- 2026-08-27：初稿。
