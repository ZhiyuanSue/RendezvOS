# PCI 枚举与配置空间

v0.1 · 2026-09-27

本篇覆盖：`modules/pci/pci_ops.c`、`pci_dev_tree.c`、`include/modules/pci/*.h`，以及 x86 `arch_start_platform` 里的 `pci_scan_all`。

**仅 x86 legacy PIO（`0xCF8` / `0xCFC`）；无 MMIO ECAM；aarch64 无 pci 模块链路。**  
构建：`config_x86_64.json` 可 `modules.pci.use=true`；头文件硬依赖 `arch/x86_64/io.h`。

ACPI 同次平台启动中先跑，但 **PCI 不读 MCFG**。DTB 里的 `pci-host-ecam-generic` **未接线**（见 DTB 篇）。

**硬件对照：** PCI Local Bus — Configuration Mechanism #1（地址口 `CF8h`、数据口 `CFCh`）；Type 0/1 头、BAR size probe（写全 1 再读）。无 IOAPIC/MSI 路由实现。

---

## 1. 概述

x86 拉起阶段只做发现：从 bus 0 递归扫 → 建 `pci_node` 树 → 打印。配置空间走 **legacy PIO**，不是 ECAM。

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

本篇拥有：x86 PCI 发现——Mechanism #1 配置读写（`pci.h`）、`pci_scan_*` / `pci_enable_device`（`pci_ops.h`）、`pci_node` 树与查找（`pci_dev_tree.h`）。以头文件注释为准（见上列头文件；已与 `.c` 核对）。

**本篇不拥有：** `arch_start_platform` 编排 → `04`；ACPI/MCFG → `35`（**不读**）；中断路由 / IOAPIC → `26`；aarch64 ECAM / DTB pci-host → **不存在**。

仅 `#ifdef PCI`（`modules.pci.use`）构建有意义。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 平台 | `acpi_init` → 分配 `pci_root` → **`pci_scan_all(cb, pci_root)`** |
| callback | 填 `pci_node` → 可选 **`pci_scan_bar`** → return |
| 查找 | **`pci_get_device(vid, did, start)`**（NULL start → `pci_root`） |
| enable | **`pci_enable_device`** — **当前恒失败**（IRQ stub） |

### 7.2 配置空间（PIO）

```c
u32  pci_config_read_IO_dword(u8 bus, u8 dev, u8 func, u8 off);
void pci_config_write_IO_dword(u8 bus, u8 dev, u8 func, u8 off, u32 val);
bool pci_device_exists(u8 bus, u8 dev, u8 func);  /* ≠ 0xFFFFFFFF */
```

地址口 `0xCF8`、数据口 `0xCFC`。无 MMIO ECAM。

### 7.3 扫描与 BAR

```c
typedef struct pci_node *(*pci_scan_callback)(u8 bus, u8 device, u8 func,
                                              const pci_header_t *hdr);
error_t pci_scan_all(pci_scan_callback cb, struct pci_node *root);
error_t pci_scan_bus(cb, u8 bus, struct pci_node *parent);
error_t pci_scan_bar(struct pci_node *dev, const pci_header_t *hdr);
```

| 接口 | 说明 |
|------|------|
| `pci_scan_all` | `next_bus_number=1`；从 bus 0 递归。Bridge（class 0x06, subclass 0x04/07）写 `0x18` 总线号。 |
| `pci_scan_bar` | 写全 1 测 size，写回 origin；**不**新分配资源。 |
| 缺口 | `pci_scan_device` 读头时 **func 写死 0**——多功能非 0 功能可能读到 func0 头。 |

### 7.4 树与 enable

```c
extern struct pci_node *pci_root;
struct pci_node *pci_get_device(u16 vendor, u16 device, struct pci_node *start);
void pci_put_device(struct pci_node *);
void print_pci_tree(struct pci_node *, int level);
error_t pci_enable_device(struct pci_node *);  /* IRQ stub → 失败回滚 command */
```

`pci_assign_irq` / `pci_assign_bar_resource`：**内部 stub**，不对外承诺。

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

- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——pci.h/ops/dev_tree Doxygen；PIO-only；enable 失败与 func0 缺口。  
- 2026-09-25：语言轮——CF8/CFC Mechanism #1；扫描/BAR/enable 真相；func0 与 type1 缺口。  
- 2026-08-29：整篇重做——删 ECAM/aarch64/双源；钉 PIO-only。  
- 2026-08-27：初稿（叙事过时）。
