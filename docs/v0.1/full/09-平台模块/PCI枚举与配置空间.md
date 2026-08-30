# PCI 枚举与配置空间

v0.1 · 2026-08-29

本篇覆盖：`modules/pci/*`（若启用）、`include/modules/pci/*.h`、x86 `arch_start_platform` 里的 `pci_scan_all`。

**仅 x86 PIO（CF8/CFC）；无 MMIO ECAM；aarch64 无 pci 模块。**  
config：`config_x86_64.json` 可 `modules.pci.use=true`；aarch64 JSON 通常无。头文件硬依赖 `arch/x86_64/io.h`。

ACPI 在同次平台启动中先跑，但 **PCI 不读 MCFG**。DTB 里的 `pci-host-ecam-generic` **未接线**。

---

## 1. 概述

x86 bring-up 级发现：扫总线 → 建 `pci_node` 树 → 打印/查找。配置空间走 **legacy PIO**，不是 ECAM。

能看见设备；enable 骨架被 IRQ stub 卡死；BAR 能测 size，不能分配。

旧稿「PIO 或 ECAM」「aarch64 virt ECAM」「与 DTB 双源」——**全部作废**。

---

## 2. 目标与边界

**提供：** 递归扫描、bridge 总线号、BAR size probe、树回调、`pci_get_device`、command 位 enable/disable 骨架。

**不做：** ECAM；BAR 资源分配；IRQ 路由（`pci_assign_irq` 恒失败）；电源管理真实现；aarch64 扫描。

---

## 3. 分层与调用方

`arch_start_platform`：`pci_scan_all(pci_tree_build_callback, root)`。  
测例：`single_pci_test` 包在 `#ifdef PCI`。

驱动若要 `pci_enable_device`：现路径 `power_up(stub)` → command(IO|MEM|MASTER) → **`assign_irq` 恒 -1 → 失败回滚**。

---

## 4. 数据结构与不变量

- 树：`pci_node` + BAR 信息（probe size，复用 BIOS 地址）。  
- Bridge（class 06, sub 04/07）：配 0x18 总线号并递归。  
- `pci.h` 有 `/*MMIO way*/` 注释——**未实现**。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/pci` 扫描/树 | `pci_scan_all` / `pci_scan_bus` / `pci_scan_device` |
| config 访问 | x86 PIO |
| `pci_enable_device` | 骨架；IRQ stub |

---

## 6. 流程

```text
bus0 → scan_bus → scan_device
  bridge：设 secondary/subordinate，递归
  leaf：callback 填节点 + scan_bar
```

**已知代码味：** `pci_scan_device` 读 config 时 function 曾写死 0（多功能风险）；type1 BAR 上界字段疑似笔误——文档标为缺口，以源码为准。

---

## 7. 公开 API

以 `include/modules/pci` 为准：`pci_scan_all`、`pci_get_device`、`pci_enable_device`、command helpers。仅 `#ifdef PCI` 构建有意义。

---

## 8. 多架构

**仅 x86_64** 启用。aarch64：无模块；ECAM 未支持。

---

## 9. 测试

`single_pci_test`（PCI 开时）。本篇未复测。

---

## 10. 限制与后续

- 无 ECAM / MSI / 热插拔。  
- enable 被 IRQ stub 挡住。  
- BAR 分配 API stub。  
- 多功能 / type1 笔误风险。

---

## 11. 变更记录

- 2026-08-29：整篇重做——删 ECAM/aarch64/双源；钉 PIO-only；enable/IRQ/BAR 真相；func0 风险。
- 2026-08-27：初稿（叙事过时）。
