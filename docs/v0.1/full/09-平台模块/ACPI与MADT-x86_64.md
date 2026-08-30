# ACPI 与 MADT（x86_64）

v0.1 · 2026-08-29

本篇覆盖：`modules/acpi/acpi.c`（RSDP 探测 helper）、`arch/x86_64/acpi/acpi.c`（`acpi_init`）、`arch/x86_64/acpi/madt.c`（`parser_apic`）、相关头文件。

SMP 消费 `NR_CPU`/`CPU_STATE` 见拓扑篇；FEE00000 检查见 APIC 篇；平台时序见 `01-启动/平台启动-x86_64`。IOAPIC 空壳交叉引用 APIC 篇。

---

## 1. 概述

x86 在无 UEFI 运行时下，用 **ACPI 1.0 RSDP/RSDT** 发现 CPU 拓扑，喂给 SMP INIT/SIPI。

只认真读 MADT 里的 Local APIC；其余多半跳过。

两段式：

1. **早期** `reserve_arch_region`：扫物理区找 RSDP，可能 reserve RSDT 所在页。  
2. **`arch_start_platform`**：`acpi_init` → 遇 MADT 调 **`parser_apic()`** 填 `NR_CPU` / `CPU_STATE[apic_id]`。

不做 AML、hotplug、sleep 状态机。IOAPIC / Source Override 等条目 **空 `break`**——与 IOAPIC 真空壳一致。

---

## 2. 目标与边界

**提供：** RSDP 扫描；RSDT rev0 walk；MADT Local APIC → 拓扑表；FACP 挂表（parser 空成功）。

**不做：** XSDT/ACPI 2+；checksum 严格策略（以源码为准）；读 Local APIC **enable flags**（**完全不看**——disabled 条目也会进计数）；IOAPIC 编程。

---

## 3. 分层与调用方

| 阶段 | 谁 |
|------|-----|
| RSDP 探测 | `modules` + pmm `reserve_arch_region` |
| 表解析 | `arch/.../acpi_init` |
| 填 CPU 表 | `parser_apic` |
| 消费 | `arch_start_smp`、xAPIC 基址检查 |

PCI 在同一次 `arch_start_platform` 随后扫描，但 **不读** ACPI/MCFG。

---

## 4. 数据结构与不变量

- **APIC id ≡ 逻辑 `cpu_id` 下标**（可稀疏）。  
- **`NR_CPU`** = 合法 Local APIC 条目**个数**（`apic_id < MAX` 时 `++`），**不是** `max_id+1`。  
- `CPU_STATE[apic_id] = cpu_disable`，SIPI 后变 enable。  
- MADT `Local_int_ctrl_address` 须 **`== 0xFEE00000`**（xAPIC 路径），否则 init_irq 直接 return。  
- `Local_APIC_flags_online_capable` 一类宏若写成 `(1<1)` 且未用——不是运行契约。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/acpi/acpi.c` | `acpi_probe_rsdp`、签名分类 |
| `arch/.../acpi/acpi.c` | `acpi_init`、RSDT walk |
| `arch/.../acpi/madt.c` | `parser_apic`、`for_each_madt_ctrl_head` |

---

## 6. 流程

```text
acpi_probe_rsdp：扫 [0x80000,0x80400) 与 [0xE0000,0x100000)，步进16，"RSD PTR "
  → setup_info->rsdp_addr；rev==0 才 reserve RSDT 页
acpi_init：rev!=0 失败；仅 RSDT
  → APIC → parser_apic：NR_CPU=0；清 STATE；Local_APIC → ++NR_CPU / disable
  → FACP → parser_facp 空成功
  → IOAPIC/ISO/… → break
```

RSDP 找不到时 reserve 仍可能 SUCCESS、`rsdp_addr=0`——后续 `acpi_init(0)` 危险（启动篇已点，本篇回链）。

---

## 7. 公开 API

以 `acpi_probe_rsdp` / `acpi_init` / `parser_apic` / MADT walk 宏为准（arch/modules 头）。上层勿直接改 `NR_CPU`。

---

## 8. 多架构

仅 x86_64。aarch64 用 DTB，见下一篇。

---

## 9. 测试

间接：SMP 启动。本篇未复测。

---

## 10. 限制与后续

- 忽略 enable flags；稀疏 id 与 `cpu_id_is_online` 冲突见拓扑篇。  
- 无 XSDT；IOAPIC 未消费。  
- ECDT 等签名表可能有脏数据（弱相关）。

---

## 11. 变更记录

- 2026-08-29：整篇重做——两段式；消费/忽略表；忽略 enable；FEE00000；与 SMP id 模型。
- 2026-08-27：初稿。
