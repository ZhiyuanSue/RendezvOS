# ELF 加载辅助模块

v0.1 · 2026-09-27

本篇覆盖：`modules/elf/elf.c`、`elf_print.c`、`include/modules/elf/*.h`（`elf.h` / `elf_common.h` / `elf_32.h` / `elf_64.h` / `elf_print.h`）。

**映射、栈、回用户、Path A/B、`gen_thread_from_elf` / `thread_loader`** 全在 `03-任务与调度/14-线程创建与ELF加载.md`。本篇只界定**格式库边界**，禁止再抄一套加载流程。

**格式对照：** System V ABI / ELF —— `e_ident` magic `0x7F 'E' 'L' 'F'`、`EI_CLASS` / `EI_DATA` / `EI_VERSION`、Program Header（`PT_LOAD` 等）、`e_machine`（`EM_X86_64` / `EM_AARCH64` / …）。本仓库把结构体与常量落在头文件里，供 loader 消费。

---

## 1. 概述

ELF 模块做三件事：校验文件头、按 class 读字段、（可选）打印 Ehdr/Phdr/Shdr。**不涉及 VSpace、不分配页、不写用户映射。**

`thread_loader` 调 `check_elf_header`、要求 `ELFCLASS64`、用 `for_each_program_header_64` 扫 Phdr 找 `PT_LOAD`；`PT_DYNAMIC` 在 loader 侧 stub。本模块不负责 relocate、动态链接、符号解析、Interpreter。

### 1.1 为什么拆成「薄格式库」

加载策略（从哪读文件、怎么 map、栈怎么摆）会随任务模型变；ELF 头布局相对稳定。把校验 / 遍历宏独立出来，loader 与调试打印共用同一套常量，避免在 `thread_loader.c` 里散落 magic number。

---

## 2. 目标与边界

**提供：** magic + `EI_VERSION==EV_CURRENT` 校验；class / data / osabi 查询；`e_type` / `e_machine` 读取；32/64 phdr·shdr 遍历宏；DEBUG 下打印。

**不做：** `load_elf_to_vs`、栈图像、drop、hooks（→ 14）；ELF32 加载（loader 直接拒绝）；完整动态链接；校验 `e_machine` 是否匹配当前 ISA（**loader 也不校验**——错架构镜像可能直到 map 后才触发异常）。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `kernel/task/thread_loader.c` | `check_elf_header` → `get_elf_class` → `for_each_program_header_64`；调试可调 `print_elf_ph64` |
| 调试 | `print_elf_header` / `print_elf_*`（默认无输出，见下） |

无独立 `single_elf_*` 测试用例。

---

## 4. 数据结构与不变量

- 类型与常量：`elf_common.h`（magic、`ET_*`、`EM_*`、`PT_*`、`SHT_*`…）；`Elf32_*` / `Elf64_*` 在分头。
- `check_elf_header`：**只**查 `EI_MAG0..3` 与 `EI_VERSION`；不查 class、endian、machine、phentsize。
- 遍历宏假定调用方已确认 class，且 ph/sh 表落在可读缓冲内（loader 另做 `e_phoff` 越界检查）。
- `elf64_hash`：SysV 哈希算法有实现，**头文件未导出、运行路径未用**——近似死代码。

### 4.1 ELF 文件头布局（Ehdr）

ELF 文件最前面是固定格式的文件头。`e_ident` 占前 16 字节，其后字段在 ELF32 / ELF64 中长度不同（地址与偏移字段由 32 位扩为 64 位），因此 ELF64 Ehdr 比 ELF32 多 8 字节：

```text
偏移   ELF32 字段             ELF64 字段            说明
0x00   e_ident[16]            e_ident[16]          magic + class + data + version + osabi
0x10   e_type    (Half)       e_type    (Half)      ET_EXEC=2 ET_DYN=3 ...
0x12   e_machine (Half)       e_machine (Half)      EM_X86_64=62 EM_AARCH64=183 EM_RISCV=243
0x14   e_version (Word)       e_version (Word)      EV_CURRENT=1
0x18   e_entry   (Addr32)     e_entry   (Addr64)    入口虚拟地址
0x1C   e_phoff   (Off32)      e_phoff   (Off64)    Program Header 表文件偏移
0x20   e_shoff   (Off32)      e_shoff   (Off64)    Section Header 表文件偏移
0x24   e_flags   (Word)       e_flags   (Word)      架构标志（aarch64 多见）
0x28   e_ehsize  (Half)       e_ehsize  (Half)      Ehdr 本身大小（32=52 / 64=64）
0x2A   e_phentsize (Half)     e_phentsize (Half)    单个 Phdr 大小（32=32 / 64=56）
0x2C   e_phnum   (Half)       e_phnum   (Half)     Phdr 条数
0x2E   e_shentsize (Half)     e_shentsize (Half)
0x30   e_shnum   (Half)       e_shnum   (Half)
0x32   e_shstrndx (Half)      e_shstrndx (Half)     段名字符串表索引
                                                    ELF64 至此共 64 字节
```

`e_ident` 固定 16 字节，前 4 字节为 magic `0x7F 'E' 'L' 'F'`：

```text
索引   字段             取值
0..3   EI_MAG0..3       0x7F 'E' 'L' 'F'
4      EI_CLASS         1=ELFCLASS32  2=ELFCLASS64
5      EI_DATA          1=ELFDATA2LSB(little-endian)  2=ELFDATA2MSB(big-endian)
6      EI_VERSION       1=EV_CURRENT
7      EI_OSABI         0=System V  3=Linux  97=ARM ...
8      EI_ABIVERSION    通常 0
9..15  EI_PAD           保留填充
```

`check_elf_header` 只校验 `EI_MAG0..3` + `EI_VERSION`；class / endian / osabi 由 `get_elf_*` 单独读，loader 自己再判 `ELFCLASS64`。`e_phoff` 越界检查由 loader 负责，本格式库不做。

### 4.2 Program Header（Phdr）

每个 `PT_LOAD` 描述一段「文件偏移 → 虚拟地址」的映射。ELF32 / ELF64 字段顺序不同，遍历宏各自按对应布局步进：

```text
ELF64 Phdr（56 字节，p_offset 在 p_flags 前）：
  p_type    (Word)   PT_LOAD=1 PT_DYNAMIC=2 PT_INTERP=3 PT_NOTE=4 PT_GNU_STACK=0x6474e551 ...
  p_flags   (Word)   PF_X=1 PF_W=2 PF_R=4
  p_offset  (Off64)  文件内偏移
  p_vaddr   (Addr64)  虚拟地址
  p_paddr   (Addr64)  物理地址（裸机/核心镜像才有意义）
  p_filesz  (Word64) 文件中实际长度
  p_memsz   (Word64) 内存中长度（≥ filesz，差额为 .bss 零填充）
  p_align   (Word64) 对齐

ELF32 Phdr（32 字节，p_flags 在最后）：
  p_type / p_offset / p_vaddr / p_paddr / p_filesz / p_memsz / p_flags / p_align
```

常见 `p_type`：

| `p_type` | 含义 | 本模块处理 |
|----------|------|-----------|
| `PT_LOAD` | 可加载段，loader 映射到用户 VSpace | loader 在 14 篇处理映射 |
| `PT_DYNAMIC` | 动态链接信息 | loader 侧 stub |
| `PT_INTERP` | 解释器路径（动态链接） | 不处理 |
| `PT_NOTE` | 附注段（build-id / ABI 提示） | 不处理 |
| `PT_GNU_STACK` | 栈可执行性提示 | 不处理 |

### 4.3 段到内存的映射

`PT_LOAD` 把文件中的一段拷贝到内存，并按 `p_memsz` 零填充超出 `p_filesz` 的部分（`.bss`）：

```text
文件:  [ ... | p_offset ............ p_offset+p_filesz | ... ]
                       |  p_filesz 字节
                       v
内存:  [ ... | p_vaddr ............. p_vaddr+p_filesz ... p_vaddr+p_memsz | ... ]
                       |←  filesz  →|←  memsz - filesz（零填充）  →|
                       ←  p_align 对齐边界  →
```

`p_memsz > p_filesz` 的零填充由 14 篇 loader 用零页实现；本格式库只读字段，不分配页、不写映射。


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
  不调用 get_elf_machine 做 ISA 校验
```

打印：`elf_print.c` 顶部 `// #define DEBUG` 默认注释掉 → `debug` 宏为 `pr_off`，`print_elf_*` 调用仍安全但无输出。需要时取消注释并开 `_LOG_DEBUG_`。

---

## 7. 公开 API

本篇涉及的接口分布在：ELF **格式库**——`check_elf_header` / `get_elf_*`、32/64 Phdr·Shdr 遍历宏（`elf.h`）、调试打印（`elf_print.h`）、常量与结构体（`elf_common.h` / `elf_32.h` / `elf_64.h`）。以头文件注释为准（`elf.h` / `elf_print.h`；已与 `.c` 核对）。

**本篇不涉及：** `load_elf_to_vs` / `gen_thread_from_elf` / `run_elf_program` / 栈 / Path A·B → `14`；`page_slice` 拷贝 → `10`；VSpace map → `07`/`08`。

**不存在于公开面：** `elf64_hash`（`.c` 内实现，未导出、无调用方）。

### 7.1 编排顺序（调用方须遵守）

| 步骤 | API | 说明 |
|------|-----|------|
| 1 | **`check_elf_header`** | 仅 magic + `EI_VERSION` |
| 2 | **`get_elf_class` == `ELFCLASS64`** | loader 拒绝 32-bit |
| 3 | （可选）`e_phoff` 越界检查 | loader 侧，非本篇 |
| 4 | **`for_each_program_header_64`** | 扫 `PT_LOAD` / `PT_DYNAMIC`（映射语义在 `14`） |

本篇**不**校验 `e_machine` / endian 与宿主一致——错架构镜像可能直到 map 后才触发异常。

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

格式层与 ISA 无关；`e_machine` 常量覆盖 x86_64 / AArch64 / RISC-V / LoongArch。真正「只加载本机 ELF」的约束应由 loader 补——**当前未补**。

---

## 9. 测试

无独立测试用例；随用户 ELF / harness 加载路径间接覆盖。

---

## 10. 限制与后续

- 打印默认关闭。
- `elf64_hash` 近似死代码。
- 不校验 `e_machine` / endian 与宿主一致。
- 加载语义始终回链 14，勿在本篇扩展。

---

## 11. 变更记录

- 2026-10-05：最终词句顺畅。
- 2026-10-05：任务 1/3/5 精读——「测例」→「测试用例」（§3、§9）；改翻译腔——「不碰 VSpace」→「不涉及 VSpace」、「错架构可走到/可能走到 map 后才触发异常」→「错架构镜像可能直到 map 后才触发异常」、「做 ISA 门闩」→「做 ISA 校验」、「只跑本机 ELF」→「只加载本机 ELF」、「半死代码/半死」→「近似死代码」、「永远回链 14，勿在本篇膨胀」→「始终回链 14，勿在本篇扩展」、「loader 在 14 篇映射」→「loader 在 14 篇处理映射」。
- 2026-10-04：补硬件/格式知识——§4.1 ELF32/64 Ehdr 字段布局与 `e_ident` 字节图、§4.2 Phdr 结构（含 ELF32/64 字段顺序差异与常见 `p_type` 表）、§4.3 PT_LOAD 段到内存映射图；改翻译腔——「钉格式库边界」→「界定格式库边界」、「map 后再炸」→「map 后才触发异常」、「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——`elf.h`/`elf_print.h` Doxygen；与 `14` 硬分工；标明 `elf64_hash` 未导出。
- 2026-09-25：语言轮——ELF 格式字段与 loader 消费边界；明确不校验 machine；打印默认 `pr_off`。
- 2026-08-29：整篇重做——薄边界；与 03/14 硬分工。
- 2026-08-27：初稿。
