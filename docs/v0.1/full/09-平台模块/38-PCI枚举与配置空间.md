# PCI 枚举与配置空间

v0.1 · 2026-09-27

本篇覆盖：`modules/pci/pci_ops.c`、`pci_dev_tree.c`、`include/modules/pci/*.h`，以及 x86 `arch_start_platform` 里的 `pci_scan_all`。

**仅 x86 legacy PIO（`0xCF8` / `0xCFC`）；无 MMIO ECAM；aarch64 无 pci 模块链路。**
构建：`config_x86_64.json` 中 `modules.pci.use=true`，`features:["PCI"]` 经 `script/config/configure.py` 拼进全局 `CFLAGS`（`-D PCI`），测试用例的 `#ifdef PCI` 由此生效。头文件第一行硬编码 `#include <arch/x86_64/io.h>`——别的架构根本编不过，配置也只在 x86_64 启用本模块。

ACPI 在同一次平台启动中先执行，但 **PCI 不读 MCFG**。DTB 里的 `pci-host-ecam-generic` **未接入**（见 DTB 篇（`36-DTB与设备树-aarch64.md`））。

**硬件对照：** PCI Local Bus — Configuration Mechanism #1（地址口 `CF8h`、数据口 `CFCh`）；Type 0/1 头、BAR size probe（写全 1 再读）。无 IOAPIC/MSI 路由实现。

---

## 1. 概述

x86 拉起阶段只做发现：从 bus 0 递归扫，建 `pci_node` 树，再打印。配置空间采用 **legacy PIO**，不是 ECAM。

现有代码能看见设备、测量 BAR 长度并复用 BIOS 已写地址；`pci_enable_device` 骨架被 **IRQ 分配 stub** 阻断——开 IO/MEM/MASTER 之后 `pci_assign_irq` 恒返回 -1，随即回滚 command。

旧叙述「PIO 或 ECAM」「aarch64 virt ECAM」「与 DTB 双源冲突」——相对当前源码 **均已作废**。

---

## 2. 目标与边界

**提供：** 递归扫描、PCI-PCI bridge 总线号配置、BAR size probe、树回调、`pci_get_device`、command 位 enable/disable 骨架。

**不做：** ECAM；BAR 资源真正分配（`pci_assign_bar_resource` 返回 0）；IRQ 路由（`pci_assign_irq` 恒失败）；电源管理真实现；aarch64 扫描；MSI/MSI-X。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `arch_start_platform`（`arch/x86_64/boot/start_arch.c`） | `acpi_init` → `ioapic_init` → `m_alloc` 出 `pci_root` → `pci_scan_all(pci_tree_build_callback, pci_root)`，扫描完打印整棵树 |
| 测试用例 | `test_pci_scan`（`single_pci_test.c`，`#ifdef PCI`），注册在 `single_test.c` 的用例表里 |

平台路径的 `pci_root` 与 `pci_tree_build_callback` 里的节点分配都没有判空（`m_alloc` 契约保证清零，成功即字段从 0 起；失败则直接崩，见 §6.4）；测试用例路径的两处分配反而都判空，但 callback 失败时返回的 NULL 仍会撞进同一处插树。

驱动若调 `pci_enable_device`：先有 `ref_count > 1` 门槛（被两个以上持有者占用直接拒绝）；再 `power_up(stub)`，写 command(IO\|MEM\|MASTER)；因 **`assign_irq` 恒为 -1，会失败并 disable command、`power_down` 回滚**。

---

## 4. 数据结构：软件树节点 vs 配置空间头

§1 的目标是枚举总线上的功能，并把结果留下来给后面用。实现上分两层：硬件上的配置空间头（读出来的 64 字节布局，见 §4.1），以及软件里一棵孩子/兄弟树（`pci_node`），方便打印和按厂商/设备号查找。

```c
struct pci_resource {
        u64 start_addr;
        u64 len;
        u64 flags; /* EXIST / IO / MEM / MEM_64 / PREFETCH */
};

struct pci_node {
        struct tree_node dev_node;
        u8 bus, device, func, pedding;
        u16 vendor_id, device_id;
        u8 prog_if, subclass, class_code, header_type;
        u32 usable_resource_number;
        struct pci_resource bar[MAX_PCI_BAR_NUMBER];
        u8 primary, secondary, subordinate, padding;
        u32 pci_device_flags;
        i32 ref_count;
};

typedef struct {
        u16 vendor_id, device_id;
        u16 command, status;
        u8 revision_id, prog_if, subclass, class_code;
        u8 cache_line_size, latency_timer, header_type, bist;
} pci_common_header_t;
/* Type0 / Type1 与 union pci_header_t 见 pci.h */
```

| 字段 | 为什么要有 |
|------|-----------|
| `dev_node` | 挂进 PCI 设备树（桥下面挂孩子）。 |
| `bus` / `device` / `func` | 配置空间寻址三维坐标（Mechanism #1 的 CF8 编码用）。 |
| `vendor_id` / `device_id` / class 三元组 / `header_type` | 从配置空间头抄来，供识别与桥判断。 |
| `bar[]` | 每个 BAR 探测后的基址、长度、IO/MEM 等标志（6 个槽位；64-bit BAR 高低 dword 合占一个槽）。 |
| `usable_resource_number` | 名义上表示「可用 BAR 个数」，实际**无人读写**，纯占位（记档）。 |
| `primary` / `secondary` / `subordinate` | **桥**才有意义：本桥连接的总线号范围，递归扫描 secondary。 |
| `ref_count` | `pci_get_device` 命中时 +1，`pci_put_device` −1（减成负数会被钳回 0）。 |

行为摘要：桥（`class_code==0x06` 且 subclass `0x04`/`0x07`）写配置空间 `0x18` 总线号并递归；BAR probe 后 **复用** BIOS 已写好的基址（`pci_bar_resource_assigned` 恒 true）。`pci.h` 里 MMIO 配置通路只留下一句 `MMIO way — not implemented` 注释——**未实现**，只用 IO 口 CF8/CFC。

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
1. 读 origin = BAR 当前值（==0 或 ==0xFFFFFFFF 直接跳过：该 BAR 不存在或 BIOS 未分配）
2. 写 0xFFFFFFFF → 读回 mask
3. size = ~(mask 清掉低位类型位) + 1   （内存 BAR 清低 4 位，I/O BAR 清低 2 位）
4. 写回 origin                    ← 不重新分配，复用 BIOS 写好的地址
```

64-bit 内存 BAR 紧接着把**相邻下一个 BAR 槽位**当高 dword，重复一轮「写全 1→读回→写回」，高低合成同一个 `bar[]` 条目（`len`、`start_addr` 各自 `hi<<32 | lo`）；若高 dword 已越过本头类型的 BAR 上界，直接 `return -1`。

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

两个读写函数的 `offset` 参数是 `u8`，进宏后再 `& 0xFC`（`PCI_ADDR_REG_OFF_MASK`），能寻址的只有每个 function 头 **256 字节**里的 dword——PCIe 扩展到 4KB 的配置空间，光靠 PIO 是够不着的，只能用 ECAM 映射。

> ECAM（Memory-mapped Enhanced Configuration Access Mechanism）是 PCI Express 引入的替代方案：用 MMIO 区域 `base + (bus<<20 | dev<<15 | func<<12 | offset)` 直接访问，每个 bus 占 1MB。aarch64 virt 与现代 x86 都用 ECAM，但本仓库未接入（`pci.h` 里只有一句 `/* MMIO way — not implemented */` 注释）。

### 6.2 扫描

PCI 拓扑是三级树：**bus（0..255）、device（0..31）、function（0..7）**。每个 function 是一个独立配置空间。扫描从 bus 0 开始，遇到 PCI-PCI 桥就分配新 bus 号并递归进入下游：

```text
bus 0  ┌─ dev0 ─ func0 ─── (bridge) ──┐
       │        └ func1..7 (若 multi-func)
       ├─ dev1 ─ func0 ─── (endpoint, callback 挂叶)
       ...
       └─ dev31
                  bridge 分配新 bus 号（next_bus_number++，全局递增）
                  写入桥 Type1 头 0x18 一个 dword: primary=当前bus
                                                secondary=新bus
                                                subordinate=0xFF（先写死）
                  → 递归 scan_bus(secondary)
                  → 递归结束回填 subordinate=子树实际最大 bus
```

```text
pci_scan_all:
  next_bus_number=1; recursion_depth=0; scan_bus(0)
  扫完 print_pci_tree 打印整棵树
scan_bus:
  若 recursion_depth > 16（PCI_MAX_RECURSION_DEPTH）→ return -1
  for device 0..31:
    若 func0 存在 → scan_device(..., func=0) → 返回节点插入父树（不判空，见 §6.4）
    若 header_type bit7 多功能 → func 1..7 逐个查存在再 scan_device
scan_device:
  读 64 字节头 → bridge? 先 callback 建节点 → 配总线号并递归 → 回填节点总线号字段
               : callback 挂叶
  （BAR 多在 callback 里再 pci_scan_bar）
```

### 6.3 BAR size probe

对每个 BAR：先读 origin（空槽/未分配跳过），写 `0xFFFFFFFF`，读回算 size，再 **写回 origin**。64-bit MEM BAR 再处理高 dword（见 §4.2）。不重新分配资源。`pci_scan_bar` 由 callback 调用——平台路径在 `pci_tree_build_callback` 末尾，测试路径在 `simple_print_callback` 末尾。

### 6.4 已知源码缺口（以 `.c` 为准）

- **`pci_scan_device` 读头时 function 写死为 `0`**：`pci_config_read_IO_dword(bus, device, 0, …)`，即使入参 `func!=0`。多功能设备的非 0 功能可能读到 **func0 的头**——实际缺陷。
- **`pci_scan_bar` 判头类型不掩 `header_type` 的 bit7**：直接 `==0` / `==1` 比较，而多功能设备的实际值是 `0x80`/`0x81`，两个分支都进不去，`bar_offset` 和 `last_bar_offset` 都停在 0——循环只跑一次，把 `0x00` 处的 vendor/device dword 当成 BAR0 探测。vendor 寄存器只读，写全 1 硬件无副作用，但 `bar[0]` 里会记进一条垃圾条目并被打印。
- **`pci_scan_bus` 对扫描返回值不判空**：`pci_scan_device` 的结果直接 `tree_node_insert` 挂树；「callback 返回 NULL（测试用例分配失败）」「桥递归失败或超深度上限」都会返回 NULL，随后就是空指针解引用。`arch_start_platform` 的 `pci_root` 分配、`pci_tree_build_callback` 里的节点分配同样没有判空（`m_alloc` 契约保证清零，但失败即崩）。测试用例路径的两处分配（`test_pci_scan` 的 root、`simple_print_callback` 的节点）**有**判空——没判空的是插树处：callback 返回 NULL 时，崩的还是 `pci_scan_bus`。
- **`is_pci_bridge` 是死代码**：只看 `class_code==0x06` 不看 subclass，零调用者；真正生效的判断是 `pci_bridge_need_scan`（0x06 且 subclass 0x04/0x07）。
- **`pci_assign_bar_resource` 永远执行不到**：两处调用点都被 `!pci_bar_resource_assigned(…)` 守卫，而该函数恒 true——它连同返回值一起是死代码。
- `pci_scan_device`、`pci_assign_irq`、`pci_assign_bar_resource`、`pci_power_up`/`pci_power_down` 都**不在任何头文件声明**，却都不是 `static`——外部无人调用，但链接期全局符号可见。
- `pci_enable_command` 读 offset 4 的 dword 取低 16 位当 command，写回时高 16 位（status）被写成 0——status 是 RO/W1C 语义，写 0 无效，**无害**（记档）。
- **type1 头 BAR 上界**：~~`last_bar_offset` 用了 `hdr->type0.bar[1]`（应为 `hdr->type1.bar[1]`）。字段名笔误——但 `type0.bar[1]` 与 `type1.bar[1]` 在 `pci_header_t` union 中偏移相同（均 0x14），编译后地址一致，**运行时行为正确**~~ **已修复**（2026-10-05）：`pci_ops.c:158` 改为 `hdr->type1.bar[1]`，字段名与分支类型一致。
- enable 路径被 IRQ stub 永久阻断。

---

## 7. 公开 API

接口分布在三个头文件：Mechanism #1 配置读写（`pci.h`）、`pci_scan_*` / `pci_enable_device`（`pci_ops.h`）、`pci_node` 树与查找（`pci_dev_tree.h`）。以头文件注释为准（三者均已与 `.c` 核对）。

**本篇不涉及：** `arch_start_platform` 编排（见 `04-平台启动-x86_64.md`）；ACPI/MCFG（见 `35-ACPI与MADT-x86_64.md`，**不读**）；中断路由 / IOAPIC（见 `26-平台中断-x86_64-APIC与PIC.md`）；aarch64 ECAM / DTB pci-host（**不存在**）。

全篇只对启用了 `PCI` 特性的构建有意义（`modules.pci.use` 与 `-D PCI` 的来源见篇首）。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 平台 | `arch_start_platform`：`acpi_init` → `ioapic_init` → 分配 `pci_root`（**不判空**）→ **`pci_scan_all(cb, pci_root)`**；callback 内节点分配同样没判空 |
| callback | 填 `pci_node`，末尾 **`pci_scan_bar`**，然后 return 节点 |
| 查找 | **`pci_get_device(vid, did, start)`**（`start` 为 NULL 时从 `pci_root` 起） |
| enable | **`pci_enable_device`** — **当前恒失败**（IRQ stub），回滚见 §6 |

### 7.2 配置空间（PIO）

```c
u32  pci_config_read_IO_dword(u8 bus, u8 dev, u8 func, u8 off);
void pci_config_write_IO_dword(u8 bus, u8 dev, u8 func, u8 off, u32 val);
bool pci_device_exists(u8 bus, u8 dev, u8 func);  /* ≠ 0xFFFFFFFF */
```

地址口 `0xCF8`、数据口 `0xCFC`（`io_port.h` 中 `_X86_PCI_ADDR_REG` / `_X86_PCI_DATA_REG`）。`off` 是 `u8`，进宏后再 `& 0xFC`，够到的只有 legacy 256 字节头。无 MMIO ECAM（`pci.h` 里 `/* MMIO way — not implemented */`）。三个内联函数直接写在 `pci.h` 里，不是 `.c` 符号。

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
| `pci_scan_all` | `next_bus_number=1`、`recursion_depth=0`，从 bus 0 递归，深度上限 `PCI_MAX_RECURSION_DEPTH`=16；扫完 `print_pci_tree`。Bridge（class 0x06, subclass 0x04/07）在 `0x18` 写一个 dword：subordinate 先 0xFF、递归后回填。 |
| `pci_scan_bus` | device 0..31；多功能（`header_type` bit7）再试 func 1..7；返回节点**不判空**即插树（§6.4）。 |
| `pci_scan_bar` | 空槽/未分配跳过；写全 1 测量 size，写回 origin；64-bit BAR 合并相邻槽位；**不**新分配资源。 |
| 缺口 | `pci_scan_device` 读头时 **func 硬编码为 0**——多功能非 0 功能可能读到 func0 头。 |

### 7.4 树与 enable

```c
extern struct pci_node *pci_root;
struct pci_node *pci_get_device(u16 vendor, u16 device, struct pci_node *start);
void pci_put_device(struct pci_node *);
void print_pci_tree(struct pci_node *, int level);
error_t pci_enable_device(struct pci_node *);  /* IRQ stub → 失败回滚 command */
```

`pci_get_device` 从 `start`（NULL 则 `pci_root`）做 DFS，**先匹配 start 自身**再下探子树；命中则 `ref_count += 1`。`pci_put_device` 减一，**减成负数会被钳回 0**。`print_pci_tree` 按层级缩进打印 bus:device.func、Vendor/Device、Class/Subclass/ProgIF、各 BAR 的 start/len/flags。

`pci_enable_device` 入口先查 `ref_count > 1`（设备已被他人持有则直接 `-1`），随后 power_up（stub）→ 置 command 的 IO|MEM|MASTER → assign_irq（恒 `-1`）→ 逐级回滚。**当前必然失败**。`pci_assign_irq` / `pci_assign_bar_resource` / `pci_power_up` / `pci_power_down`：**内部 stub**，均不在头文件声明，不对外承诺。

---

## 8. 多架构

**仅 x86_64** 启用。aarch64：无本模块消费；无 ECAM 实现。

---

## 9. 测试

`test_pci_scan`（`single_pci_test.c`，注册在 `single_test.c` 用例表）。测试体包在 `#ifdef PCI` 里——`features:["PCI"]` 经 `configure.py` 拼进全局 `CFLAGS` 才编译，否则直接返回 `REND_SUCCESS`。它调 `pci_scan_all(simple_print_callback, …)`：callback 与平台的 `pci_tree_build_callback` 同型，但打印在前——先打 bus:device.func 与 vendor/device/class，对 AMD（0x1022/0x1002）的 vendor id 另有设备类型注解，再分配节点（**有**判空，失败 `pr_error` 后返回 NULL——这个 NULL 会直撞 `pci_scan_bus` 的插树缺口，见 §6.4）、填总线号与 vendor/device/class，末尾 `pci_scan_bar` 后返回节点。

---

## 10. 限制与后续

- 无 ECAM / MSI / 热插拔 / IOAPIC 路由。PIO 只能到 256 字节头。
- enable 被 IRQ stub 阻断（command 位被翻一轮后回滚）。
- BAR 分配 API stub 且调用点恒不可达。
- 多功能读头硬编码 func0；`pci_scan_bar` 判头不掩 bit7，多功能设备 BAR 探测错位；§6.4 全列（含判空链缺失、`is_pci_bridge` 死代码、`usable_resource_number` 占位）。
- ~~type1 BAR 上界字段名笔误（`type0.bar[1]` 应为 `type1.bar[1]`，union 偏移相同故运行时无害）~~ 已修复（2026-10-05）。
- 头文件第一行硬编码 `#include <arch/x86_64/io.h>`，模块实质 x86-only。

---

## 11. 变更记录
- 2026-11-19：底子重评批次——§7 opener。事实沿用 11-15。
- 2026-11-15：全文可读性通读（篇章架构 / 篇章引用 / 词句）。架构大体过关，格式一处：§4.2 末 `---` 前一空行多余，并一。引用一处：篇首「见 DTB 篇」补号（`36-DTB与设备树-aarch64.md`）。事实修正两处：① `pci.h` 的 MMIO 配置通路注释实为 `MMIO way — not implemented`，原 §4.2「注释为空」与 §6.1「仅有 `/*MMIO way*/` 空注释」不实，按原文改写；② 测试路径判空写反——`simple_print_callback` 的节点分配与 `test_pci_scan` 的 root **都**判空（失败 `pr_error` 后返回 NULL / `-E_REND_TEST`），原 §6.4「两个 callback 的节点分配同样没有判空」与 §9「同样不判空」不实，改为「分配有判空、插树无判空：callback 返回的 NULL 仍崩在 `pci_scan_bus`」；§3 同步限定「平台路径」。词句五处：§1「能看见设备…」残句补主语「现有代码」；§7 首「分布在：x86 PCI 发现——…」电报体改为按三个头文件列举；§7「仅 `#ifdef PCI`（…）构建有意义」残句补全；§6.1 / §7.2「是 `u8` 再与 `0xFC`」改「进宏后再 `& 0xFC`」（「与」易读成连词；掩码实际发生在 `SET_PCI_ADDR_REG_VAL`，`PCI_ADDR_REG_OFF_MASK`=0xFC）；§3「直接拒」补「绝」。§6.4 其余缺口逐条复核属实：读头 func 硬编码 0（`pci_config_read_IO_dword(bus, device, 0, …)`）、`pci_scan_bar` 判头不掩 bit7、插树不判空（`pci_ops.c:92`）、`is_pci_bridge` 与 `usable_resource_number` 全仓零引用、`pci_assign_irq` 恒 -1、`pci_assign_bar_resource` 恒 0 且两处调用点被恒 true 守卫挡住、command 写回把 status 写 0（无害）、DFS 先匹配自身、`pci_put_device` 钳 0、`test_pci_scan` 注册在 `single_test.c` 用例表、`-D PCI` 确为 `configure.py` 拼出的带空格形式（`' -D ' + feature`），均与文一致。余沿 11-14 精读，未变。

- 2026-11-14：全文对照代码复核（pci.h/pci_ops.c/pci_dev_tree.c/single_pci_test.c/start_arch.c/config_x86_64.json/configure.py）。修正 `usable_resource_number` 旧表述「能用的 BAR 个数」——全仓无人读写，纯占位；补 §6.4 六条新缺口：`pci_scan_bar` 判头不掩 `header_type` bit7（0x80/0x81 两头落空，把 0x00 vendor dword 当 BAR0）、`pci_scan_bus` 插树不判空与 `pci_root`/callback 分配不判空链、`is_pci_bridge` 死代码、`pci_assign_bar_resource` 调用点恒不可达、若干符号 non-static 却不头文件声明、command dword 写回把 status 写 0（无害）。§3/§7.1 补 `arch_start_platform` 真实链路与 `ref_count>1` 门槛；§4.2 补 64-bit BAR 高 dword 合并；§6.1 补 `u8` offset 只够 256 字节头；§6.2 重写扫描流程（深度上限 16、subordinate 先 0xFF 后回填、桥分支先 callback 后配总线号）；§7.4 补 DFS 先匹配自身、put 钳 0、打印格式；§9 补 `#ifdef PCI` 生效机制与 AMD 特判；篇首补 `-D PCI` 构建来源与 x86-only 头依赖。
- 2026-10-05：修复 `pci_ops.c:158` type1 BAR 上界字段名笔误——`hdr->type0.bar[1]` → `hdr->type1.bar[1]`（`pci_header_t` union 偏移相同故运行时无害，改以保持字段名与分支类型一致）；§6.4 同步标记已修复。
- 2026-10-05：核对 §6.4 "type1 BAR 上界笔误"——确认 `pci_ops.c:158` 写成 `hdr->type0.bar[1]`（应为 `hdr->type1.bar[1]`），但 `pci_header_t` union 中 `type0.bar[1]` 与 `type1.bar[1]` 偏移相同（均 0x14），编译后地址一致，运行时行为正确。§6.4 描述由"疑似笔误"改为明确"字段名笔误，union 偏移相同故运行时无害"。建议改 `type1.bar[1]` 提高可读性（待 maintainer 确认）。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：硬件事实核对（对照 PCI Local Bus Spec 3.0 / Linux pci_regs.h）——修正 §4.1 配置头表 Type 1 列 0x1C–0x38 整体错位（secondary_bus 应在 0x19 非 0x1C、subordinate_bus 应在 0x1A 非 0x20 等，与 §6.2 正文对齐）；修正 Type 0 列 0x30/0x34/0x38/0x3C 字段错位（rom_base 在 0x30、cap_ptr 在 0x34、0x38 reserved、0x3C 含 min_gnt/max_lat）；补 BIST(0x0F)、Bridge Control(0x3E-3F)。
- 2026-10-05：任务 1/3/5 精读——「测例」→「测试用例」（§3、§6.2）；改翻译腔——「配置空间走 legacy PIO」→「配置空间采用 legacy PIO」、「走两个 32 位 I/O 端口」→「使用两个 32 位 I/O 端口」、「测 BAR 长度」→「测量 BAR 长度」、「IRQ 分配 stub 卡死」→「IRQ 分配 stub 阻断」、「实 bug」→「实际缺陷」、「func 写死 0 / 写死 func0」→「func 硬编码为 0 / 硬编码 func0」、「测 size」→「测量 size」、「PCI 开时」→「PCI 启用时」、「全部作废」→「均已作废」、「ACPI 同次平台启动中先跑」→「ACPI 在同一次平台启动中先执行」、「递归进下游」→「递归进入下游」。
- 2026-10-04：补硬件知识——§4.1 Type 0/Type 1 配置头 64 字节布局对照、§4.2 BAR 编码与 size probe 流程图、§6.1 CF8/CFC 端口编址位域图与 ECAM 说明、§6.2 bus/dev/func 三级枚举与桥总线号递归图；改翻译腔——「本篇拥有」→「本篇涉及的接口分布在」。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——pci.h/ops/dev_tree Doxygen；PIO-only；enable 失败与 func0 缺口。
- 2026-09-25：语言轮——CF8/CFC Mechanism #1；扫描/BAR/enable 真相；func0 与 type1 缺口。
- 2026-08-29：整篇重做——删 ECAM/aarch64/双源；限定为 PIO-only。
- 2026-08-27：初稿（叙事过时）。
