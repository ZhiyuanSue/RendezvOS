# PCI 枚举与配置空间

v0.1 · 2026-09-25

本篇覆盖：`modules/pci/pci_ops.c`、`pci_dev_tree.c`、`include/modules/pci/*.h`，以及 x86 `arch_start_platform` 里的 `pci_scan_all`。

**仅 x86 legacy PIO（`0xCF8` / `0xCFC`）；无 MMIO ECAM；aarch64 无 pci 模块链路。**  
构建：`config_x86_64.json` 可 `modules.pci.use=true`；头文件硬依赖 `arch/x86_64/io.h`。

ACPI 同次平台启动中先跑，但 **PCI 不读 MCFG**。DTB 里的 `pci-host-ecam-generic` **未接线**（见 DTB 篇）。

**硬件对照：** PCI Local Bus — Configuration Mechanism #1（地址口 `CF8h`、数据口 `CFCh`）；Type 0/1 头、BAR size probe（写全 1 再读）。无 IOAPIC/MSI 路由实现。

---

## 1. 概述

x86 bring-up 级发现：从 bus 0 递归扫 → 建 `pci_node` 树 → 打印。配置空间走 **legacy PIO**，不是 ECAM。

能看见设备、测 BAR 长度并复用 BIOS 已写地址；`pci_enable_device` 骨架被 **IRQ 分配 stub** 卡死——开 IO/MEM/MASTER 之后 `pci_assign_irq` 恒返回 -1，再回滚 command。

旧叙述「PIO 或 ECAM」「aarch64 virt ECAM」「与 DTB 双源冲突」——相对当前源码 **全部作废**。

---

## 2. 目标与边界

**提供：** 递归扫描、PCI-PCI bridge 总线号配置、BAR size probe、树回调、`pci_get_device`、command 位 enable/disable 骨架。

**不做：** ECAM；BAR 资源真正分配（`pci_assign_bar_resource` 返回 0）；IRQ 路由（`pci_assign_irq` 恒失败）；电源管理真实现；aarch64 扫描；MSI/MSI-X。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `arch_start_platform`（x86） | `pci_scan_all(pci_tree_build_callback, root)` |
| 测例 | `single_pci_test`（`#ifdef PCI`） |

驱动若调 `pci_enable_device`：`power_up(stub)` → command(IO\|MEM\|MASTER) → **`assign_irq` 恒 -1 → 失败并 disable command**。

---

## 4. 数据结构与不变量

- 树：`pci_node`（bus/dev/func、BAR 信息、`dev_node` 挂孩子）。  
- Bridge：`class_code==0x06` 且 subclass `0x04`/`0x07` → 写配置空间偏移 `0x18`（primary/secondary/subordinate）并递归。  
- BAR：probe 后 `len` / `start_addr`；`pci_bar_resource_assigned` 恒 true → **总是复用**读到的 origin（当 BIOS 已分配）。  
- `pci.h` 有 `/*MMIO way*/` 空注释——**未实现**。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `pci.h` | CF8/CFC 编址宏、`pci_config_read/write_IO_dword`、头结构 |
| `pci_ops.c` | `pci_scan_*`、BAR probe、enable 骨架 |
| `pci_dev_tree.c` | 树节点 / 打印 / 查找 |
| `arch/.../start_arch.c` | 平台阶段调用扫描 |

---

## 6. 流程

### 6.1 硬件：Configuration Mechanism #1

访问一次 dword 配置寄存器：

1. 向 **`0xCF8`** 写出 32 位地址：bit31=1（enable）\| bus<<16 \| device<<11 \| func<<8 \| (offset & ~3)。  
2. 从 / 向 **`0xCFC`** 读 / 写数据。

`pci_device_exists`：读 vendor/device dword，`0xFFFFFFFF` 视为空槽。

### 6.2 扫描

```text
pci_scan_all:
  next_bus_number=1; scan_bus(0)
scan_bus:
  for device 0..31:
    若 func0 存在 → scan_device(..., func=0)
    若 header_type multi-function → 再扫 func 1..7
scan_device:
  读 64 字节头 → bridge? 配总线号并递归 : callback 挂叶
  （BAR 多在 callback / 测例里再 pci_scan_bar）
```

### 6.3 BAR size probe

对每个 BAR：读 origin → 写 `0xFFFFFFFF` → 读回算 size → **写回 origin**。64-bit MEM BAR 再处理高 dword。不重新分配资源。

### 6.4 已知源码缺口（以 `.c` 为准）

- **`pci_scan_device` 读头时 function 写死为 `0`**：`pci_config_read_IO_dword(bus, device, 0, …)`，即使入参 `func!=0`。多功能设备的非 0 功能可能读到 **func0 的头**——实 bug。  
- **type1 头 BAR 上界**：`last_bar_offset` 用了 `type0.bar[1]`，疑似笔误。  
- enable 路径被 IRQ stub 永久挡住。

---

## 7. 公开 API

```c
error_t pci_scan_all(pci_scan_callback cb, struct pci_node *root);
error_t pci_scan_bus(...); error_t pci_scan_bar(...);
error_t pci_enable_device(struct pci_node *);
/* pci_get_device / print_pci_tree — 见 pci_dev_tree.h */
u32 pci_config_read_IO_dword(u8 bus, u8 dev, u8 func, u8 off);
```

仅 `#ifdef PCI` 构建有意义。

---

## 8. 多架构

**仅 x86_64** 启用。aarch64：无本模块消费；无 ECAM 实现。

---

## 9. 测试

`single_pci_test`（PCI 开时）。本篇未复测。

---

## 10. 限制与后续

- 无 ECAM / MSI / 热插拔 / IOAPIC 路由。  
- enable 被 IRQ stub 挡住。  
- BAR 分配 API stub。  
- 多功能读头写死 func0；type1 BAR 上界笔误风险。

---

## 11. 变更记录

- 2026-09-25：语言轮——CF8/CFC Mechanism #1；扫描/BAR/enable 真相；func0 与 type1 缺口。  
- 2026-08-29：整篇重做——删 ECAM/aarch64/双源；钉 PIO-only。  
- 2026-08-27：初稿（叙事过时）。
