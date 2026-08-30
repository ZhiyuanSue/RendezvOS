# DTB 与设备树（aarch64）

v0.1 · 2026-08-29

本篇覆盖：`modules/dtb/*`（libfdt 裁剪、`dev_tree`、property）、以及 **`build_device_tree` 的真实落点** `arch/aarch64/boot/start_arch.c`。

Early map / 拷贝时序见 `01-启动/平台启动-aarch64`；GIC/PSCI/UART/CPU 消费者见各篇。本篇**不**假装 aarch64 有 PCI ECAM 扫描器。

---

## 1. 概述

aarch64 无 ACPI；**FDT 是硬件描述真源**。分两层：

| 层 | 何时 | 用途 |
|----|------|------|
| **MMU-off raw FDT** | boot_map / early pmm | PL011 UART、`memory` 物理区 |
| **`device_node` 树** | `arch_start_platform` | PSCI、GIC、`chosen/bootargs`、cpu 节点 |

先 raw 救急，后建树给驱动 probe。

`build_device_tree` 在 **arch start_arch.c**，**不**在 `modules/dtb`（旧稿 `dev_tree_build` 名也不对）。

---

## 2. 目标与边界

**提供：** header 校验；建树；按 name/type/compatible 查找；属性 endian 安全读取。

**不做：** OF overlay、live 改树、完整 libfdt、Linux `of_*`。  
**不做：** 规定 PCI 来源——aarch64 **无** pci 模块；DT 里可有 `pci-host-ecam-generic` 但**无人解析**。删掉「与 ECAM 并存须防冲突」恐吓。

---

## 3. 分层与调用方

| 消费者 | 查找 |
|--------|------|
| early UART | raw `arm,pl011` |
| pmm | raw `device_type=memory` |
| PSCI | `arm,psci`（前缀可命中 `-0.2`） |
| GIC | `arm,cortex-a15-gic`（写死；`gic-400` 对不上） |
| SMP | `device_type=cpu` + `enable-method` |
| cmdline | `chosen` / `bootargs` |

典型：`dev_node_find_by_compatible` → 读 `reg` / `method`。

---

## 4. 数据结构与不变量

- `device_root`：平台后全局根。  
- compatible 按 `'\0'` 分条；匹配逻辑实质 **前缀匹配**（搜 `arm,psci` 可命中 `arm,psci-0.2`）。  
- 未知名属性 → `PROPERTY_TYPE_OTHER` 仍保留。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/dtb/dtb.c` | libfdt 裁剪 |
| `dev_tree.c` / `property.c` | 查找与读属性 |
| `arch/.../start_arch.c` | **`build_device_tree`**、cmdline、调 PSCI/GIC |
| `boot_map.c` | early raw 读 |

---

## 6. 流程

```text
boot_map：raw pl011 → 映 UART；拷贝 DTB
prepare_arch：map_dtb + fdt_check_header
arch_init_pmm：raw memory
arch_start_platform：
  device_root = build_device_tree(...)
  chosen → cmdline
  psci_init；gic.probe；gic.init_distributor
```

---

## 7. 公开 API

```c
struct device_node *build_device_tree(...);  /* arch 调用；声明以头为准 */
struct device_node *dev_node_find_by_compatible(...);
struct device_node *dev_node_find_by_type/name(...);
/* property_read_* ；fdt_check_header / raw_get_prop_from_dtb */
```

---

## 8. 多架构

仅 aarch64 主线消费。x86 用 ACPI。

---

## 9. 测试

间接：virt 启动。本篇未复测。

---

## 10. 限制与后续

- compatible 前缀匹配可能过宽。  
- GIC compatible 写死。  
- PCI host 节点未接线。  
- early 与 device_root 两阶段勿混。

---

## 11. 变更记录

- 2026-08-29：整篇重做——raw vs 树；`build_device_tree` 真位置；删 PCI 双源；前缀匹配；消费者表。
- 2026-08-27：初稿。
