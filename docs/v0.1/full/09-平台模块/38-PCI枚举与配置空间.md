# PCI 枚举与配置空间

v0.1 · 2026-09-27

本篇覆盖：`modules/pci/pci_ops.c`、`pci_dev_tree.c`、`include/modules/pci/*.h`，以及 x86 `arch_start_platform` 里的 `pci_scan_all`。

**仅 x86 legacy PIO（`0xCF8` / `0xCFC`）；无 MMIO ECAM；aarch64 无 pci 模块链路。**
构建：`config_x86_64.json` 可 `modules.pci.use=true`；头文件硬依赖 `arch/x86_64/io.h`。

ACPI 在同一次平台启动中先执行，但 **PCI 不读 MCFG**。DTB 里的 `pci-host-ecam-generic` **未接入**（见 DTB 篇）。

**硬件对照：** PCI Local Bus — Configuration Mechanism #1（地址口 `CF8h`、数据口 `CFCh`）；Type 0/1 头、BAR size probe（写全 1 再读）。无 IOAPIC/MSI 路由实现。

---

## 1. 概述

x86 拉起阶段只做发现：从 bus 0 递归扫 → 建 `pci_node` 树 → 打印。配置空间采用 **legacy PIO**，不是 ECAM。

能看见设备、测量 BAR 长度并复用 BIOS 已写地址；`pci_enable_device` 骨架被 **IRQ 分配 stub** 阻断——开 IO/MEM/MASTER 之后 `pci_assign_irq` 恒返回 -1，再回滚 command。

旧叙述「PIO 或 ECAM」「aarch64 virt ECAM」「与 DTB 双源冲突」——相对当前源码 **均已作废**。

---

## 2. 目标与边界

**提供：** 递归扫描、PCI-PCI bridge 总线号配置、BAR size probe、树回调、`pci_get_device`、command 位 enable/disable 骨架。

**不做：** ECAM；BAR 资源真正分配（`pci_assign_bar_resource` 返回 0）；IRQ 路由（`pci_assign_irq` 恒失败）；电源管理真实现；aarch64 扫描；MSI/MSI-X。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `arch_start_platform`（x86） | `pci_scan_all(pci_tree_build_callback, root)` |
| 测试用例 | `single_pci_test`（`#ifdef PCI`） |

驱动若调 `pci_enable_device`：`power_up(stub)` → command(IO\|MEM\|MASTER) → **`assign_irq` 恒 -1 → 失败并 disable command**。

---

## 4. 数据结构与不变量

- 树：`pci_node`（bus/dev/func、BAR 信息、`dev_node` 挂孩子）。
- Bridge：`class_code==0x06` 且 subclass `0x04`/`0x07` → 写配置空间偏移 `0x18`（primary/secondary/subordinate）并递归。
- BAR：probe 后 `len` / `start_addr`；`pci_bar_resource_assigned` 恒 true → **总是复用**读到的 origin（当 BIOS 已分配）。
- `pci.h` 有 `/*MMIO way*/` 空注释——**未实现**。

### 4.1 配置空间头（64 字节）

每个 bus/device/function 暴露 256 字节（PCIe 扩展到 4KB）配置空间，前 64 字节是头。`header_type`（偏移 `0x0E` bit7）区分单功能（0）与多功能（1），低 7 位区分头类型：

```text
偏移   Type 0（普通设备）              Type 1（PCI-PCI 桥）
0x00   vendor_id  / device_id          vendor_id  / device_id
0x04   command / status                command / status
0x08   revision_id / class_code         revision_id / class_code
0x0C   cache_line / lat / hdr / BIST   cache_line / lat / hdr / BIST
0x10   BAR[0]                          BAR[0]
0x14   BAR[1]                          BAR[1]
0x18   BAR[2]                          primary_bus / secondary_bus / subordinate_bus / sec_lat
0x1C   BAR[3]                          io_base / io_limit / secondary_status
0x20   BAR[4]                          memory_base / memory_limit
0x24   BAR[5]                          prefetch_base / prefetch_limit
0x28   cardbus_cis_ptr                 prefetch_base_upper_32
0x2C   subsystem_vid / subsystem_id    prefetch_limit_upper_32
0x30   expansion_rom_base              io_base_upper_16 / io_limit_upper_16
0x34   capabilities_ptr / reserved     capabilities_ptr / reserved
0x38   reserved                        expansion_rom_base
0x3C   int_line / int_pin / min_gnt / max_lat   int_line / int_pin / bridge_control
```

Type 0 有 6 个 BAR；Type 1（桥）只有 2 个 BAR，剩余空间放桥的总线号与转发窗口。扫描时 `pci_scan_device` 硬编码 func=0 读这 64 字节头——多功能设备的非 0 功能可能读到 func0 的头（见 §6.4 缺口）。

### 4.2 BAR 与 size probe

BAR（Base Address Register）描述设备需要的内存或 I/O 窗口。bit0 区分类型：

```text
内存 BAR：  bit0=0  bit1..2:type(0=32bit 2=64bit)  bit3:prefetchable  bit4..31:基址
I/O BAR：   bit0=1  bit1=保留  bit2..31:基址
64-bit 内存 BAR 占两个连续寄存器（低 dword + 高 dword）。
```

探测 BAR 长度的标准做法是「写全 1 再读回」：可写位读回为 1，编码 size；不可写的基址位读回为原值。本仓库 `pci_scan_bar` 据此算 `len`/`start_addr`，然后**写回 origin**，复用 BIOS 已分配地址，不重新分配：

```text
1. 读 origin = BAR 当前值
2. 写 0xFFFFFFFF → 读回 mask
3. size = (~mask & 可写位掩码) + 1     （内存 BAR 清低 4 位，I/O BAR 清低 2 位）
4. 写回 origin                    ← 不重新分配，复用 BIOS 写好的地址
```


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

PCI 规范定义两种配置空间访问机制：Mechanism #1（现代 x86 必备）与已淘汰的 Mechanism #2。本仓库只用 Mechanism #1，使用两个 32 位 I/O 端口：

```text
地址口 0xCF8（32 位）          数据口 0xCFC（32 位）
┌─────────────────────────┐    ┌──────────────────┐
│31│30..24│23..16│15..11│10..8│7..2│1..0│        │  读写 dword
│ ↑│  保留 │ bus  │ dev  │func│ reg │ 0  │ ───►  │  0xCFC
│En│       │      │      │    │     │    │        │
└─────────────────────────┘    └──────────────────┘
   bit31 = enable；offset 低 2 位清零（dword 对齐）
```

访问一次 dword 配置寄存器：

1. 向 **`0xCF8`** 写出 32 位地址：bit31=1（enable）\| bus<<16 \| device<<11 \| func<<8 \| (offset & ~3)。
2. 从 / 向 **`0xCFC`** 读 / 写数据。

`pci_device_exists`：读 vendor/device dword，`0xFFFFFFFF` 视为空槽。

> ECAM（Memory-mapped Enhanced Configuration Access Mechanism）是 PCI Express 引入的替代方案：用 MMIO 区域 `base + (bus<<20 | dev<<15 | func<<12 | offset)` 直接访问，每个 bus 占 1MB。aarch64 virt 与现代 x86 都用 ECAM，但本仓库未接入（`pci.h` 仅有 `/*MMIO way*/` 空注释）。

### 6.2 扫描

PCI 拓扑是三级树：**bus（0..255）→ device（0..31）→ function（0..7）**。每个 function 是一个独立配置空间。扫描从 bus 0 开始，遇到 PCI-PCI 桥就分配新 bus 号并递归进入下游：

```text
bus 0  ┌─ dev0 ─ func0 ─── (bridge) ──┐
       │        └ func1..7 (若 multi-func)
       ├─ dev1 ─ func0 ─── (endpoint, callback 挂叶)
       ...
       └─ dev31
                  bridge 分配新 bus 号（next_bus_number++）
                  写入桥 Type1 头 0x18: primary=当前bus
                                   0x19: secondary=新bus
                                   0x1A: subordinate=子树最大bus
                  → 递归 scan_bus(secondary)
```

```text
pci_scan_all:
  next_bus_number=1; scan_bus(0)
scan_bus:
  for device 0..31:
    若 func0 存在 → scan_device(..., func=0)
    若 header_type multi-function → 再扫描 func 1..7
scan_device:
  读 64 字节头 → bridge? 配置总线号并递归 : callback 挂叶
  （BAR 多在 callback / 测试用例里再 pci_scan_bar）
```

### 6.3 BAR size probe

对每个 BAR：读 origin → 写 `0xFFFFFFFF` → 读回算 size → **写回 origin**。64-bit MEM BAR 再处理高 dword。不重新分配资源。

### 6.4 已知源码缺口（以 `.c` 为准）

- **`pci_scan_device` 读头时 function 写死为 `0`**：`pci_config_read_IO_dword(bus, device, 0, …)`，即使入参 `func!=0`。多功能设备的非 0 功能可能读到 **func0 的头**——实际缺陷。
- **type1 头 BAR 上界**：~~`last_bar_offset` 用了 `hdr->type0.bar[1]`（应为 `hdr->type1.bar[1]`）。字段名笔误——但 `type0.bar[1]` 与 `type1.bar[1]` 在 `pci_header_t` union 中偏移相同（均 0x14），编译后地址一致，**运行时行为正确**~~ **已修复**（2026-10-05）：`pci_ops.c:158` 改为 `hdr->type1.bar[1]`，字段名与分支类型一致。
- enable 路径被 IRQ stub 永久阻断。

---

## 7. 公开 API

本篇涉及的接口分布在：x86 PCI 发现——Mechanism #1 配置读写（`pci.h`）、`pci_scan_*` / `pci_enable_device`（`pci_ops.h`）、`pci_node` 树与查找（`pci_dev_tree.h`）。以头文件注释为准（见上列头文件；已与 `.c` 核对）。

**本篇不涉及：** `arch_start_platform` 编排 → `04`；ACPI/MCFG → `35`（**不读**）；中断路由 / IOAPIC → `26`；aarch64 ECAM / DTB pci-host → **不存在**。

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
| `pci_scan_bar` | 写全 1 测量 size，写回 origin；**不**新分配资源。 |
| 缺口 | `pci_scan_device` 读头时 **func 硬编码为 0**——多功能非 0 功能可能读到 func0 头。 |

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

`single_pci_test`（PCI 启用时）。

---

## 10. 限制与后续

- 无 ECAM / MSI / 热插拔 / IOAPIC 路由。
- enable 被 IRQ stub 阻断。
- BAR 分配 API stub。
- 多功能读头硬编码 func0；~~type1 BAR 上界字段名笔误（`type0.bar[1]` 应为 `type1.bar[1]`，union 偏移相同故运行时无害）~~ 已修复（2026-10-05）。

---

## 11. 变更记录

- 2026-10-05：修复 `pci_ops.c:158` type1 BAR 上界字段名笔误——`hdr->type0.bar[1]` → `hdr->type1.bar[1]`（`pci_header_t` union 偏移相同故运行时无害，改以保持字段名与分支类型一致）；§6.4 同步标记已修复。
- 2026-10-05：核对 §6.4 "type1 BAR 上界笔误"——确认 `pci_ops.c:158` 写成 `hdr->type0.bar[1]`（应为 `hdr->type1.bar[1]`），但 `pci_header_t` union 中 `type0.bar[1]` 与 `type1.bar[1]` 偏移相同（均 0x14），编译后地址一致，运行时行为正确。§6.4 描述由"疑似笔误"改为明确"字段名笔误，union 偏移相同故运行时无害"。建议改 `type1.bar[1]` 提高可读性（待 maintainer 确认）。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：硬件事实核对（对照 PCI Local Bus Spec 3.0 / Linux pci_regs.h）——修正 §4.1 配置头表 Type 1 列 0x1C–0x38 整体错位（secondary_bus 应在 0x19 非 0x1C、subordinate_bus 应在 0x1A 非 0x20 等，与 §6.2 正文对齐）；修正 Type 0 列 0x30/0x34/0x38/0x3C 字段错位（rom_base 在 0x30、cap_ptr 在 0x34、0x38 reserved、0x3C 含 min_gnt/max_lat）；补 BIST(0x0F)、Bridge Control(0x3E-3F)。
- 2026-10-05：任务 1/3/5 精读——「测例」→「测试用例」（§3、§6.2）；改翻译腔——「配置空间走 legacy PIO」→「配置空间采用 legacy PIO」、「走两个 32 位 I/O 端口」→「使用两个 32 位 I/O 端口」、「测 BAR 长度」→「测量 BAR 长度」、「IRQ 分配 stub 卡死」→「IRQ 分配 stub 阻断」、「实 bug」→「实际缺陷」、「func 写死 0 / 写死 func0」→「func 硬编码为 0 / 硬编码 func0」、「测 size」→「测量 size」、「PCI 开时」→「PCI 启用时」、「全部作废」→「均已作废」、「ACPI 同次平台启动中先跑」→「ACPI 在同一次平台启动中先执行」、「递归进下游」→「递归进入下游」。
- 2026-10-04：补硬件知识——§4.1 Type 0/Type 1 配置头 64 字节布局对照、§4.2 BAR 编码与 size probe 流程图、§6.1 CF8/CFC 端口编址位域图与 ECAM 说明、§6.2 bus/dev/func 三级枚举与桥总线号递归图；改翻译腔——「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——pci.h/ops/dev_tree Doxygen；PIO-only；enable 失败与 func0 缺口。
- 2026-09-25：语言轮——CF8/CFC Mechanism #1；扫描/BAR/enable 真相；func0 与 type1 缺口。
- 2026-08-29：整篇重做——删 ECAM/aarch64/双源；钉 PIO-only。
- 2026-08-27：初稿（叙事过时）。
