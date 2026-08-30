# ELF 加载辅助模块

v0.1 · 2026-08-29

本篇覆盖：`modules/elf/elf.c`、`elf_print.c`、`include/modules/elf/*.h`。

**映射、栈、回用户、Path A/B、`gen_thread_from_elf`** 全在 `03-任务与调度/线程创建与ELF加载.md`——本篇只做格式库边界，禁止再抄一套加载流程。

---

## 1. 概述

校验/遍历/打印 ELF 头与 Phdr；不碰 VSpace。

`thread_loader` 调 `check_elf_header`、ELFCLASS64、`for_each_program_header_64` 做 PT_LOAD；PT_DYNAMIC 在 loader 侧 stub。本模块不负责 relocate、动态链接、符号解析。

---

## 2. 目标与边界

**提供：** magic/version 校验；32/64 class 查询；phdr/shdr 遍历宏；DEBUG 下打印。

**不做：** `load_elf_to_vs`、栈图像、drop、hooks（→ 03）；ELF32 加载（loader 拒绝）；完整动态链接。

---

## 3. 分层与调用方

`thread_loader` / 调试打印。无独立 `single_elf_*` 测例。

---

## 4. 数据结构与不变量

头文件：`Elf32/64_Ehdr`、`for_each_program_header_*`、`for_each_section_header_*`。  
`get_elf_class|data_encode|osabi|…` inline。  
`elf64_hash`：**有实现、头文件未必导出、运行路径未用**。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `elf.c` | check / type / machine / hash |
| `elf_print.c` | `#ifdef DEBUG` 才真打；默认常 `pr_off` |
| `thread_loader.c` | **消费者**（03 篇） |

---

## 6. 流程

`check_elf_header` → 调用方判 CLASS64 → `for_each` Phdr。loader **不校验 `e_machine`**。

---

## 7. 公开 API

以 `include/modules/elf/elf.h` 为准：`check_elf_header`、`get_elf_*`、遍历宏、`print_elf_*`（debug）。

---

## 8. 多架构

格式无关；endian/class 由头字段描述。

---

## 9. 测试

无独立测例；随 ELF 加载路径间接覆盖。本篇未复测。

---

## 10. 限制与后续

- 打印默认关闭。  
- hash 半死。  
- 加载语义永远回链 03。

---

## 11. 变更记录

- 2026-08-29：整篇重做——薄边界；与 03 硬分工；API 诚实。
- 2026-08-27：初稿。
