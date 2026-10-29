# ACPI 与 MADT（x86_64）

v0.1 · 2026-09-27

本篇覆盖：`modules/acpi/`（探测辅助、签名、`acpi_init`、MADT 游标）、`arch/x86_64/acpi/`（`arch_parser_fadt` / `arch_parser_madt`）。

RSDP 探测调用点见启动篇 `04`；表所占物理区是否进 buddy 见物理内存篇 `06`（只插入 Multiboot `AVAILABLE`，**无**按 RSDP/RSDT 的专项 reserve）；拓扑消费见 `28`；LAPIC 基址核对与 IOAPIC 见 `26`。

**规范对照：** ACPI Specification — RSDP / RSDT / MADT（表签名 `"APIC"`）等。本仓库只实现 **ACPI 1.0 风格 RSDP（revision 0）+ RSDT**；不做 XSDT / AML。

---

## 1. 概述

在现行 QEMU / legacy BIOS 路径上，x86 用 ACPI 表做**板级发现**：有几颗带 Local APIC 的 CPU、LAPIC 基址写在哪。这些结果供 SMP 唤醒与中断控制器初始化使用，**不是**电源管理或 AML 解释。

生命周期分两段——**探测根指针 → 解析表**——探测在启动篇，本篇负责 RSDT walk 与 arch 侧消费。表物理页不被 buddy 分掉，靠装载器 mmap 把 ACPI/reserved/NVS 标成非 `AVAILABLE`（见 `06`），不在 PMM 里再按表地址 reserve。真正填拓扑的是 MADT 里的 Local APIC 条目。

**`acpi_init(setup_info)`：** 在 `arch_start_platform`（VSpace / Map_Handler 已就绪）内完成——`acpi_probe_rsdp`（legacy 路径自己 ensure 探测窗再扫），再 `ensure` RSDT/子表并 walk。今日不读 `setup_info`；将来 UEFI 可在其中放 RSDP 提示并走另一探测分支（不映射 BIOS 两窗）。

---

## 2. 目标与边界

**提供：** legacy BIOS 窗上的 RSDP 扫描辅助；RSDT rev0 walk 与按签名分发；`arch_parser_madt` 用 Local APIC 条目填 `NR_CPU` / `CPU_STATE`；挂上 FADT 指针（parser 现为空壳）；暴露 MADT `Local_int_ctrl_address` 供中断篇核对。

**不做：**

- XSDT / RSDP revision ≥ 1；RSDP / 次级表 checksum（只比签名字符串）
- Local APIC **online_capable** 热插拔（boot 只认 `Enabled`；未置位则跳过）
- IOAPIC **RTE 编程**、Interrupt Source Override、x2APIC MADT 条目（type 9）——ISO/x2APIC 仍空操作；IOAPIC Type=1 **已发现**并由 `ioapic_init` bring-up（见 `26`）
- AML / DSDT、设备热插拔、sleep 状态机

---

## 3. 分层与调用方

| 阶段 | 谁 | 产出 |
|------|-----|------|
| 可用内存 | `arch_init_pmm`（物理内存篇） | 只插入 mmap `AVAILABLE`；ACPI 等类型不进 buddy（**无**按 RSDP 专项 reserve） |
| 探测+解析 | `arch_start_platform` → `acpi_init(setup_info)` | probe（legacy：ensure 窗 + 扫）→ ensure 表 → walk；挂 `fadt_table` / `madt_table`；`arch_parser_*` |
| 消费拓扑 | `arch_start_smp`（`28`） | 对 `cpu_disable` 核发 INIT-SIPI |
| 消费 LAPIC 基址 | `init_irq`（`26`） | 要求 `Local_int_ctrl_address == 0xFEE00000` |

同一次 `arch_start_platform` 里还有 PCI 扫描，但**不读** ACPI MCFG（PCI 走 PIO，见 PCI 篇）。

module 提供 walk / 游标 / 契约头 `acpi_arch_hooks.h`；arch 实现 `arch_parser_*` 并拥有表全局指针。

---

## 4. 数据结构与不变量

**命名：** 表用规范名 **FADT / MADT**；RSDT 条目上的四字节签名仍是 `"FACP"` / `"APIC"`（枚举 `ACPI_FACP` / `ACPI_APIC`）。Local APIC、IOAPIC、xAPIC 等指**硬件或 MADT 条目类型**，不要和表名 MADT 混用。

### 4.1 RSDP / RSDT（ACPI 1.0）

根指针与表目录布局见 `acpi_table.h`：

```c
struct acpi_table_rsdp {
        char signature[8]; /* "RSD PTR "（含尾空格） */
        u8 checksum;
        char oemid[6];
        u8 revision; /* 0 = 仅前 20 字节有效 */
        u32 rsdt_address; /* 32 位物理 */
};

struct acpi_table_head {
        char signature[4];
        u32 length;
        u8 revision;
        u8 checksum;
        char OEMID[6];
        char OEM_table_ID[8];
        u32 OEM_revision;
        u32 Creator_ID;
        u32 Creator_revision;
};

struct acpi_table_rsdt {
        /* ACPI_TABLE_HEAD 展开为上表字段 */
        u32 *entry; /* 物理指针数组；条目数 = (length - header) / 4 */
};
```

| 字段 | 为什么要有 |
|------|-----------|
| RSDP.`signature` / `revision` / `rsdt_address` | 在 BIOS 区扫到 `"RSD PTR "` 后，靠 revision 判断只信前 20 字节，并用 32 位物理地址找到 RSDT。 |
| `acpi_table_head` | 每张次级表共用的头：签名四字符、整表长度等；本仓库主要用 `signature` + `length` 走路，**不算** checksum。 |
| RSDT.`entry[]` | 指向 MADT / FADT（签名 `"FACP"`）等表的 u32 物理指针数组；条目数 = `(length - 头长) / 4`。 |

本仓库拒绝带 `xsdt_address` 的 RSDP rev≥1 路径。

#### 4.1.1 ACPI 表层级结构

ACPI 用一棵「根指针、表目录、各类表」的三级结构组织板级信息。根指针 RSDP 唯一作用是指出「表目录在哪、是 RSDT 还是 XSDT」；表目录是一张指针数组，每条指针指向一张带标准表头的次级表（MADT、FADT、DSDT 等）。

```text
        BIOS 区 (0xE0000 ~ 0x100000 等)
   ┌──────────────────────────────────────┐
   │  扫 "RSD PTR " 16字节对齐步进         │
   └──────────────┬───────────────────────┘
                  ▼
          ┌───────────────┐  revision 0
          │   RSDP (20B)  │ ──rsdt_address (u32)──┐
          └───────────────┘                      │
              │ revision ≥ 1                      │
              ▼  + xsdt_address (u64)            │
          ┌───────────────┐                      │
          │   RSDP (36B)   │ ──xsdt_address (u64)─┤  (本仓库拒绝)
          └───────────────┘                      │
                                                 ▼
                              ┌──────────────────────────────┐
                              │  RSDT / XSDT  (标准表头 + 指针数组) │
                              │  entry[i] ──► 各次级表 (MADT/FADT/…)  │
                              └──────────────────────────────┘
```

#### 4.1.2 RSDT vs XSDT 差异

| 项 | RSDT（ACPI 1.0，rev 0） | XSDT（ACPI 2.0+，rev ≥ 1） |
|----|--------------------------|------------------------------|
| 表目录指针宽度 | u32 物理地址 | u64 物理地址 |
| 可寻址位置 | 仅 4 GiB 以下 | 64 位全空间 |
| RSDP 字段 | `rsdt_address`（偏移 16，u32） | `xsdt_address`（偏移 24，u64） |
| RSDP 有效长度 | 前 20 字节 | 前 36 字节 |
| 本仓库 | 实现 | 直接 `[ ACPI ] unsupported vision` 失败 |

QEMU 默认 `virt` 机器的 BIOS 提供的 RSDP 通常是 revision 2，但本仓库的探测路径只在 rev 0 时成功——这是与 QEMU 真实固件行为对齐的简化边界。

#### 4.1.3 RSDP 签名与校验

RSDP 的 8 字节签名是字符串 `"RSD PTR "`（注意第 8 字节是空格 `0x20`，不是 `\0`），由 ACPI 规范定死。本仓库只做字符串比较（`acpi_table_sig_check`），**不**算 20 字节累加 checksum——规范要求 `sum(bytes[0..19]) & 0xFF == 0`，否则 RSDP 视为损坏。这意味着一块写错 OEM/revision 字段但签名凑对的内存可能被误认成 RSDP；正常 QEMU 不会触发，但换真机或自造固件时需注意。

#### 4.1.4 表头与表校验和

每张次级表（RSDT、MADT、FADT 等）共用 `acpi_table_head`：`signature[4]`（FADT 为 `"FACP"`、MADT 为 `"APIC"`）、`length`（u32，整表含头）、`revision`、`checksum`（u8）、`oem_id[6]`、`oem_table_id[8]`、`oem_revision`、`creator_id`、`creator_revision`。规范要求 `sum(整表所有字节) & 0xFF == 0`。本仓库**不**对次级表算 checksum，只比 `signature` 字符串——表头里的 `length` 仅用于推 RSDT 条目数与 MADT 游标边界，未做完整性校验。

### 4.2 MADT

`acpi_madt.h`：

```c
struct acpi_table_madt {
        /* ACPI_TABLE_HEAD */
        u32 Local_int_ctrl_address; /* 期望 0xFEE00000（xAPIC MMIO） */
        u32 flags; /* MADT_PCAT_COMPAT 等；本仓库不消费 */
        char int_ctrl_structure[]; /* 变长 type/length 记录 */
};

struct madt_int_ctrl_head {
        u8 type;
        u8 length;
};

struct madt_Local_APIC { /* type = 0 */
        u8 type;
        u8 length;
        u8 _ACPI_P_UID;
        u8 _APIC_ID;
        u32 flags;
};
```

| 字段 | 为什么要有 |
|------|-----------|
| `Local_int_ctrl_address` | firmware 声称的 LAPIC MMIO 基址；实现里核对此是否为 `0xFEE00000`。 |
| `flags` | 规范里的 PC/AT 兼容等位；本仓库**不消费**。 |
| `int_ctrl_structure[]` | 变长记录流；每条先有 `type`/`length`，再按类型解释。 |
| Local APIC 的 `_APIC_ID` / `flags` | 发现有几颗带 LAPIC 的 CPU；`apic_id` **直接当**逻辑 `cpu_id` 下标（可稀疏）。 |

实现约定：`NR_CPU` = 合法条目**个数**（`apic_id < MAX` 时 `++`），**不是** `max_id+1`。`CPU_STATE[apic_id]` 先置 `cpu_disable`，SIPI 成功后再变 `cpu_enable`。
#### 4.2.1 MADT 项类型一览

MADT 的 `int_ctrl_structure[]` 是一串 `{u8 type; u8 length; …}` 的变长记录，游标按 `length` 推进。规范定义了十余种 type；本仓库消费 Local APIC 与 IOAPIC（发现），其余空 `break`：

| type | 名称 | 字段（除头外） | 本仓库行为 |
|------|------|----------------|------------|
| 0 | Local APIC | `acpi_processor_uid`、`apic_id`、`flags` | `NR_CPU++`、`STATE[apic_id]=cpu_disable` |
| 1 | IOAPIC | `ioapic_id`、`reserved`、`ioapic_addr`、`gsi_base` | 两轮：计数 → 分配 `ioapics[]` → 填描述；**计数 0 合法**（无 IOAPIC）；Version/`nr_pins` 在 `ioapic_init`（`26`） |
| 2 | Interrupt Source Override | `bus_source`、`irq_source`、`gsi`、`flags` | 空 `break`；将来填 IOAPIC 侧 ISO 表（ISA→GSI），不进 irq_space |
| 3 | Local APIC NMI | `acpi_processor_uid`、`flags`、`lintin` | 空 `break` |
| 4 | Local APIC Address Override | `reserved`、`lapic_addr`（u64） | 空 `break`（非默认基址机器会挂在 §6.5 检查） |
| 5 | IOAPIC Source Override | 类似 type 2 | 空 `break` |
| 9 | x2APIC | `uid`、`apic_id`（u32）、`flags`、`clk_domain` | 空 `break`（x2APIC 走 MSR（Model-Specific Register）访问，不走 MADT 基址） |

`flags` 里 bit 0（`Enabled`）与 bit 1（`Online Capable`，ACPI 4.0+）：本仓库 boot 路径**只认 Enabled**；未置位则跳过该条目。Online Capable 热插拔未做。

#### 4.2.2 MADT 游标推进

```text
  ┌──────────┬────────┬───────────────────────────┐
  │ type (u8)│ len(u8)│  len-2 字节类型相关字段      │
  └──────────┴────────┴───────────────────────────┘
       ▲                                     ▲
       │                                     │  curr = (head*)(((u8*)curr) + len)
       └── for_each_madt_ctrl_head 起始        └── final_madt_int_ctrl_head 判越界
```

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/acpi/acpi.c` | `acpi_probe_rsdp`（内调 legacy BIOS 窗）；签名工具；`acpi_init`（RSDT walk + 分发） |
| `modules/acpi/acpi_madt.c` | `get_next_ctrl_head` / `final_madt_int_ctrl_head` / `for_each_madt_ctrl_head` |
| `modules/acpi/acpi_arch_hooks.h` | module 侧契约：须提供的 `arch_parser_*` / 表指针 |
| `arch/.../acpi/arch_fadt.h` + `arch_fadt.c` | `arch_parser_fadt`（占位；SCI/复位等尚未消费） |
| `arch/.../acpi/arch_madt.h` + `arch_madt.c` | `arch_parser_madt`；消费 Local APIC 条目，填 `NR_CPU` / `CPU_STATE` |
| `arch/.../PIC/IRQ.c` | 经 `arch_madt.h` 读 `madt_table->Local_int_ctrl_address`（实现细节见 `26`） |

RSDP 探测调用点在启动篇；mmap 如何排除非 Available 在物理内存篇。本表不列那些源文件。

---

## 6. 流程

### 6.1 探测：RSDP 在哪

ACPI 要求在特定 BIOS 区域按 **16 字节对齐** 搜 `"RSD PTR "`。本实现（`acpi_probe_rsdp` → legacy BIOS 辅助）：先 `ensure` 物理窗（identity），再在 `KERNEL_PHY_TO_VIRT` 上扫——与 `ensure_kernel_identity_range` 同一 VA 约定，**不再**另传 virt base。

| 窗 | 物理区间 |
|----|----------|
| 第一段 | `[0x80000, 0x80400)`（简化的 EBDA 附近窗；非完整「读 BDA 0x40E 再跟 EBDA」） |
| 第二段 | `[0xE0000, 0x100000)`（BIOS ROM 区） |

```text
   ensure(PA) → 扫 KERNEL_PHY_TO_VIRT(PA)
   0x80000 ┌─────────────────────┐  ← 第一段（EBDA 附近）
           │ step 16 字节对齐扫描 │
   0x80400 └─────────────────────┘
   0xE0000 ┌─────────────────────┐  ← 第二段（BIOS ROM）
           │ step 16 字节对齐扫描 │
   0x100000└─────────────────────┘
              命中 → 比签名 "RSD PTR "（不校验 checksum）
```

步进 16。找到则写出 identity VA；**不校验** checksum。探测窗 ensure 只在 **legacy BIOS 辅助**里做。失败则 `acpi_init` 失败。

### 6.2 与物理内存（无专项 reserve）

PMM **不**按 RSDP/RSDT 做专项 reserve。保护来自 Multiboot mmap：非 `AVAILABLE`（含 ACPI reclaim / NVS / reserved）不进 buddy，见 `06`。walk 时补 **map** ≠ reserve。

**映射：** legacy probe 内 ensure 探测窗；`acpi_init` ensure RSDT/子表。常驻 ENSURE、4 KiB、无 radix。驱动 empty map 与 ACPI 无关（见页表篇）。

### 6.3 `acpi_init`（平台阶段）

```text
acpi_probe_rsdp(&rsdp)
  └─ legacy: ensure(EBDA/ROM PA) → 扫 KERNEL_PHY_TO_VIRT → 写出 rsdp / 失败
rev==0:
  ensure(RSDT 头) → 验 "RSDT" → ensure(整表 length)
  for each u32 entry:
    ensure(表头) → ensure(整表 length)
    FACP→arch_parser_fadt / APIC→arch_parser_madt；未知签名跳过
else: unsupported revision → 失败
```

**UEFI（未实现）：** 经 `setup_info` 传入 RSDP（或等价提示）后另开探测分支（ensure 该页 → VA），不走 legacy 两窗 ensure。

没有 MADT 时 `madt_table` 仍可能为 NULL（未强制要求存在）——`init_irq` 解引用会崩；正常 QEMU 带 MADT。

### 6.4 `arch_parser_madt`

```text
NR_CPU = 0
CPU_STATE[*] = no_cpu
for_each_madt_ctrl_head:
  Local_APIC → 若 apic_id < MAX: NR_CPU++; STATE[id]=cpu_disable
  IO_APIC / Source_Override / default → break
```

稀疏 APIC id 与 `cpu_id_is_online` 的冲突、以及谁发 INIT-SIPI，见 `28`。

### 6.5 交给中断子系统

本篇产出 `madt_table->Local_int_ctrl_address`；`init_irq` 如何核对 `0xFEE00000`、x2APIC 是否跳过该检查、Address Override 未解析的后果，见 `26`。

---

## 7. 公开 API

以 `modules/acpi/*.h`、`arch_fadt.h` / `arch_madt.h` 注释为准（已与 `.c` 核对）。拓扑唤醒、LAPIC map、mmap 建区、`prepare_arch` 编排见对应专篇（§3 表）。

### 7.1 编排顺序（调用方须遵守）

| 阶段 | 顺序 |
|------|------|
| 物理内存（`06`） | `arch_init_pmm`：只插入 mmap `AVAILABLE`；无按 RSDP 的 ACPI reserve |
| 平台（`04` + 本篇） | VSpace 就绪后 **`acpi_init(setup_info)`**：probe（legacy 自 ensure 窗）+ ensure 表 + walk |
| MADT | **`arch_parser_madt`**（见签名 `"APIC"` 时调用）填拓扑 |
| 之后 | `init_irq` 读 `madt_table->Local_int_ctrl_address`；`arch_start_smp` 遍历 `cpu_disable` |

上层只**读** `NR_CPU` / `CPU_STATE` / `madt_table`；勿在业务路径改 `NR_CPU`。

### 7.2 探测与 init

```c
error_t acpi_probe_rsdp(struct acpi_table_rsdp **out);
bool acpi_table_sig_check(char *sig, char *expect);
enum acpi_table_sig_enum get_acpi_table_type_from_sig(struct acpi_table_head *);
error_t acpi_init(struct setup_info *arch_setup_info);
```

| 接口 | 说明 |
|------|------|
| `acpi_probe_rsdp` | 今日走 legacy：ensure 两物理窗 + 在 `KERNEL_PHY_TO_VIRT` 上 16 字节步进；**无** checksum。成功写出 `*out`；ensure 失败 / 未找到返回负错误。 |
| `acpi_init` | 接收 `setup_info`（今日未读）。probe → `revision==0` 则 ensure/walk 表；否则失败。无 MADT 不强制失败。 |
| `get_acpi_table_type_from_sig` | 未命中返回 **`-E_RENDEZVOS`**；walk 侧按此跳过未知表。 |
| `acpi_table_sig_check` | 返回 **`bool`**；RSDT 路径 `!check` 正确。 |

### 7.3 Arch parsers 与 MADT 游标

声明：`arch_fadt.h` / `arch_madt.h`（实现 `arch_fadt.c` / `arch_madt.c`）；module 契约另见 `acpi_arch_hooks.h`。游标在 `acpi_madt.h`。

```c
error_t arch_parser_fadt(void);   /* arch_fadt.h */
error_t arch_parser_madt(void);   /* arch_madt.h */
extern struct acpi_table_fadt *fadt_table;
extern struct acpi_table_madt *madt_table;
struct madt_int_ctrl_head *get_next_ctrl_head(...);
bool final_madt_int_ctrl_head(madt, curr);
/* macro */ for_each_madt_ctrl_head(madt_table)
```

| 接口 / 字段 | 说明 |
|-------------|------|
| `arch_parser_fadt` | 空壳；挂好 `fadt_table` 后即返回成功（SCI/复位等未消费）。 |
| `arch_parser_madt` | 清 `CPU_STATE`；Local APIC 且 **Enabled**：`NR_CPU++`、`STATE[id]=cpu_disable`；IOAPIC 两轮填 `ioapics[]`。 |
| `Local_int_ctrl_address` | 期望 `0xFEE00000`（消费在 `26`）。 |
| ISO / x2APIC / Addr Override | 空 `break`；不解析。 |

### 7.4 不变量（实现）

- `NR_CPU` = 合法 Local APIC 条目**个数**，不是 `max_id+1`。
- `apic_id` 直接当下标（可稀疏）——与 `cpu_id_is_online` 稠密假设冲突见 `28`/`16`。
- 找不到 RSDP 或 revision≠0 时 `acpi_init` 失败；平台启动随之失败。

---

## 8. 多架构

仅 x86_64。aarch64 用 DTB 枚举 CPU / GIC / UART，见下一篇 `36-DTB与设备树-aarch64.md`。MADT 头里虽枚举了 GICC/GICD 等 type，**x86 解析器不走那些分支**。

---

## 9. 测试

间接：多核 QEMU SMP 启动。无单独 ACPI 测试用例。

---

## 10. 限制与后续

**框架已就位、尚未填实：**

- `arch_parser_fadt` 空壳；ISO / Address Override 未消费（IOAPIC Type=1 已消费，见 `26`）。
- `acpi_probe_rsdp` 仅 legacy BIOS；无 UEFI / Multiboot2 hint。接入 UEFI 时在 `acpi_init` 内加分支即可。
- 只走 RSDT（rev0）；无 XSDT、无 RSDP/表 checksum。
- 表区不进 buddy 依赖 mmap 类型正确；若固件误把表标成 `AVAILABLE`，本仓库**不再**按 RSDT 窗兜底 reserve，存在被覆盖风险。
- 无 MADT 时不强制失败；`madt_table==NULL` 时中断路径仍危险。
- Local APIC `Online Capable` 热插拔未做。

远期项记 `v0.1/evolution/TODO.md`。

---

## 11. 变更记录

- 2026-10-29：§2/§10 与 IOAPIC bring-up 对齐——Type=1 已消费；缺口收窄为 RTE/ISO 等（见 `26`）。
- 2026-10-26：`arch_parser_madt` 消费 IOAPIC Type=1（两轮 + `ioapics[]`）；§4.2 / §7.3 同步。
- 2026-10-25：去掉 `search_start_vaddr`；legacy 扫窗改用 `KERNEL_PHY_TO_VIRT`，与 ensure identity 一致。
- 2026-10-24：`acpi_init(setup_info)` 传参预留 UEFI；今日 `(void)`；§1/§3/§6/§7 同步。
- 2026-10-24：legacy 探测窗 ensure 下沉到 `acpi_probe_rsdp_legacy_bios`；`acpi_probe_rsdp` 改为 `error_t` + out；§1/§6/§7 同步。
- 2026-10-24：`acpi_init`——probe + ensure 表；去掉 `setup_info.rsdp_addr`；§1/§3/§6/§7/§10 同步。
- 2026-10-24：表映射改调 `ensure_kernel_identity_range`；与 empty 安装分路。
- 2026-10-24：曾用本地 4 KiB ensure；禁止为 ACPI 新建 2 MiB 叶（已并入 `mm_kernel_utils`）。
- 2026-10-24：去掉 PMM `reserve_arch_region`；生命周期改为探测→解析；§1/§3/§6.2/§7.1/§10 与 `06` 对齐（靠 mmap 非 Available）。
- 2026-10-24：修 `acpi_init(0)`、`sig==-1`、parser 返回值、ECDT 签名、次级表 map、Local APIC Enabled；文档 §2/§6/§7/§10 同步。`online_capable` 宏本身已是 `(1<<1)`（旧文档误记）。
- 2026-10-24：§1 压成概述；命名进 §4；生命周期细节归 §3；中断接点细节归 `26` 交叉引用。
- 2026-10-24：全文对齐当时代码——`arch_fadt`/`arch_madt`；表名 FADT vs 签名 FACP；曾写三段式（含保留页面，已废止）。
- 2026-10-24：去掉空壳 `acpi_parser.h`；arch 侧改为 `arch_fadt.{h,c}` / `arch_madt.{h,c}`；`acpi_arch_hooks.h` 仅作 module 契约。
- 2026-10-24：arch hook 按**表名**命名：`arch_parser_fadt` / `arch_parser_madt`；RSDP 探测在 `prepare_arch`；`acpi_init` 在 module。
- 2026-10-04：补硬件知识——§4.1 加 ACPI 表层级结构图（RSDP→RSDT/XSDT→次级表）、RSDT vs XSDT 差异表、RSDP 签名与 checksum 说明、表头与表校验和说明；§4.2 加 MADT 项类型一览表（type 0~9）与游标推进图；§6.1 加 RSDP 搜索窗 ASCII 图。结构无大改。语言润色——「喂给」→「供…使用」、「认真消费」→「真正消费」、「扫」→「遍历」、「lock 住」→「reserve」、「被踩」→「被覆盖」、「炸」→「崩溃」、「挂在」→「卡在」、「脏点」→「瑕疵」；首次出现 INIT-SIPI 处补一句展开。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：核对 `.c`——补记 `get_acpi_table_type_from_sig` 返回 `-E_RENDEZVOS` vs `acpi_init` 判 `== -1`（已知 bug）；厘清 `acpi_table_sig_check` 为 `bool`。
- 2026-09-26：§7 全文审阅——`acpi.h` / `acpi_madt.h` Doxygen；两段式编排；vs `28`/`26`；flags 宏笔误标注。
- 2026-09-25：语言轮——补 ACPI 规范搜索窗 / RSDT / MADT 字段与 OS 依赖；两段式因果；enable 忽略与 FEE00000；签名表与 flags 宏脏点。
- 2026-08-29：整篇重做——两段式；消费/忽略表；与 SMP id 模型。
- 2026-08-27：初稿。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：任务 1/3/5 精读——§9「测例」→「测试用例」；§1「只有一件」→「只有一项」、「FACP 挂个」→「FACP 仅记录」；§1.1「记下来/分掉」→「记录下来/分配出去」；§6.1「属危险缺口」→「属于危险缺口」；§6.2「依赖它们」→「默认它们」；§6.5「MSR 访问」补「用 MSR 访问，不依赖 MMIO 基址」；任务 1 补首次出现的缩写全称——xAPIC（§1）、PIO（§3）、MADT_PCAT_COMPAT（§4.2）、MSR（§4.2.1）。
