# ACPI 与 MADT（x86_64）

v0.1 · 2026-09-27

本篇覆盖：`modules/acpi/acpi.c`（RSDP 探测 / 签名表）、`modules/acpi/acpi_madt.c`（MADT 条目游标）、`arch/x86_64/acpi/acpi.c`（`acpi_init`）、`arch/x86_64/acpi/madt.c`（`parser_apic`）、`include/modules/acpi/*.h`。

SMP 如何消费 `NR_CPU` / `CPU_STATE` 见 `28-SMP启动与处理器拓扑.md`；`0xFEE00000` 检查与 IOAPIC 空壳见 `26-平台中断-x86_64-APIC与PIC.md`；`reserve_arch_region` 落在 `phy_mm_init` 的时序见物理内存篇与 `04-平台启动-x86_64.md`。

**规范对照：** ACPI Specification — RSDP 签名 `"RSD PTR "`、BIOS 区搜索、RSDT（32 位表指针）、MADT（APIC）表头与 Local APIC / IOAPIC / Interrupt Source Override 结构。本仓库只实现 **ACPI 1.0 风格 RSDP（revision 0）+ RSDT**；不做 XSDT / AML。

---

## 1. 概述

在无 UEFI 运行时服务的 QEMU / BIOS 路径上，x86 用 **ACPI 表**发现「有几颗带 Local APIC 的 CPU、LAPIC MMIO 基址写在哪」。这些信息供后续 SMP 唤醒序列（INIT IPI → Startup IPI，简称 INIT-SIPI）与 xAPIC（eXtended APIC，64 位 MMIO APIC 接口，相对 x2APIC 而言）映射使用，并非用于电源管理。

真正消费的只有一项：**MADT 里的 Local APIC 条目** → 填 `NR_CPU` / `CPU_STATE[apic_id]`。FACP 仅记录全局指针，parser 空实现仍返回成功。IOAPIC、Source Override、其余条目全部 **空 `break`**——与中断篇「IOAPIC 空壳」一致。

### 1.1 两段式为何拆开

1. **`phy_mm_init` → `reserve_arch_region`：** 还在早期，页表 / 堆不完整。任务是在物理低区找到 RSDP，把 `setup_info->rsdp_addr` 记录下来，并（revision 0 时）把 RSDT 所在 2 MiB 窗从 buddy 可用区 **reserve** 掉，免得后面被当作普通页分配出去。
2. **`arch_start_platform` → `acpi_init`：** 此时 BSP 已有 VSpace / Map_Handler。再按需 map RSDT，walk 表，调 `parser_apic`。

AML 解释器、设备热插拔、sleep 状态机都不在 v0.1 边界内——需要「有几颗核」时，MADT Local APIC 列表就够用。

---

## 2. 目标与边界

**提供：** BIOS 区 RSDP 扫描；RSDT rev0 walk；MADT Local APIC → 拓扑表；FACP 挂表（空 parser）；MADT `Local_int_ctrl_address` 供 xAPIC 基址核对。

**不做：**

- XSDT / RSDP revision ≥ 1（代码直接 `[ ACPI ] unsupported vision` 失败）。
- RSDP / 表 **checksum** 校验（只比签名字符串）。
- 读 Local APIC 条目的 **enable / online_capable flags**（**完全不看**——firmware 标 disabled 的条目也会 `NR_CPU++` 并被 SIPI 尝试）。
- IOAPIC 编程、Interrupt Source Override、x2APIC MADT 条目（type 9）。
- 任何 AML / DSDT。

---

## 3. 分层与调用方

| 阶段 | 谁 | 产出 |
|------|-----|------|
| RSDP 探测 + reserve | `arch/.../mm/pmm.c` → `acpi_probe_rsdp` | `setup_info->rsdp_addr`；可能扣掉 RSDT 页 |
| 表解析 | `arch_start_platform` → `acpi_init` | `madt_table` / `fadt_table`；`parser_apic` |
| 消费拓扑 | `arch_start_smp` | 对 `cpu_disable` 核发 INIT-SIPI |
| 消费 LAPIC 基址 | `init_irq` → `xapic_check_base_addr` | 必须 `== 0xFEE00000` |

同一次 `arch_start_platform` 里随后还有 PCI 扫描，但 **不读** ACPI MCFG——PCI 走 PIO（Port I/O，x86 端口 I/O）配置空间，见 PCI 篇。

---

## 4. 数据结构与不变量

### 4.1 RSDP / RSDT（ACPI 1.0）

`struct acpi_table_rsdp`：签名 8 字节 `"RSD PTR "`（含尾空格）、checksum、OEM、**revision**、**rsdt_address**（32 位物理）。revision 0 = 仅前 20 字节有效；本仓库拒绝带 `xsdt_address` 的新版路径。

RSDT：标准表头 + 一串 **u32** 物理指针（`ACPI_RSDT_ENTRY_SIZE`）。条目数 = `(length - header) / 4`。

#### 4.1.1 ACPI 表层级结构

ACPI 用一棵「根指针 → 表目录 → 各类表」的三级结构组织板级信息。根指针 RSDP 唯一作用是指出「表目录在哪、是 RSDT 还是 XSDT」；表目录是一张指针数组，每条指针指向一张带标准表头的次级表（MADT、FACP、DSDT 等）。

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
                              │  entry[i] ──► 各次级表 (MADT/FACP/…)  │
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

每张次级表（RSDT、MADT、FACP 等）共用 `acpi_table_head`：`signature[4]`、`length`（u32，整表含头）、`revision`、`checksum`（u8）、`oem_id[6]`、`oem_table_id[8]`、`oem_revision`、`creator_id`、`creator_revision`。规范要求 `sum(整表所有字节) & 0xFF == 0`。本仓库**不**对次级表算 checksum，只比 `signature` 字符串——表头里的 `length` 仅用于推 RSDT 条目数与 MADT 游标边界，未做完整性校验。

### 4.2 MADT

```text
acpi_table_madt:
  ACPI_TABLE_HEAD
  Local_int_ctrl_address   // 期望 0xFEE00000（xAPIC MMIO）
  flags                    // MADT_PCAT_COMPAT 等（PC/AT 兼容标志，本仓库不消费）
  int_ctrl_structure[]     // 变长 type/length 记录
```

Local APIC 记录（type 0）：`_ACPI_P_UID`、`_APIC_ID`、`flags`。
**不变量（实现）：** `apic_id` 直接当逻辑 `cpu_id` 下标（可稀疏）；`NR_CPU` = 合法条目**个数**（`apic_id < MAX` 时 `++`），**不是** `max_id+1`。
`CPU_STATE[apic_id]` 先置 `cpu_disable`，SIPI 成功后再变 `cpu_enable`。

#### 4.2.1 MADT 项类型一览

MADT 的 `int_ctrl_structure[]` 是一串 `{u8 type; u8 length; …}` 的变长记录，游标按 `length` 推进。规范定义了十余种 type，本仓库只对 Local APIC 真正消费，其余一律空 `break`：

| type | 名称 | 字段（除头外） | 本仓库行为 |
|------|------|----------------|------------|
| 0 | Local APIC | `acpi_processor_uid`、`apic_id`、`flags` | `NR_CPU++`、`STATE[apic_id]=cpu_disable` |
| 1 | IOAPIC | `ioapic_id`、`reserved`、`ioapic_addr`、`gsi_base` | 空 `break`（IOAPIC 空壳，见 `26`） |
| 2 | Interrupt Source Override | `bus_source`、`irq_source`、`gsi`、`flags` | 空 `break` |
| 3 | Local APIC NMI | `acpi_processor_uid`、`flags`、`lintin` | 空 `break` |
| 4 | Local APIC Address Override | `reserved`、`lapic_addr`（u64） | 空 `break`（非默认基址机器会挂在 §6.5 检查） |
| 5 | IOAPIC Source Override | 类似 type 2 | 空 `break` |
| 9 | x2APIC | `uid`、`apic_id`（u32）、`flags`、`clk_domain` | 空 `break`（x2APIC 走 MSR（Model-Specific Register）访问，不走 MADT 基址） |

`flags` 里的 bit 0（`enabled`）与 bit 1（`online_capable`，ACPI 4.0+）规范含义是「该 LAPIC 是否可用 / 可在线」，本仓库**完全不读**——firmware 标 disabled 的条目也会 `NR_CPU++` 并被 SIPI 尝试（见 §2 与 §10）。

#### 4.2.2 MADT 游标推进

```text
  ┌──────────┬────────┬───────────────────────────┐
  │ type (u8)│ len(u8)│  len-2 字节类型相关字段      │
  └──────────┴────────┴───────────────────────────┘
       ▲                                     ▲
       │                                     │  curr = (head*)(((u8*)curr) + len)
       └── for_each_madt_ctrl_head 起始        └── final_madt_int_ctrl_head 判越界
```

### 4.3 签名表瑕疵

`acpi_table_sigs[]` 里 `ACPI_ECDT` 一行绑的是 `ACPI_SIG_DSDT`（复制笔误）。影响「未知表分类」打印，不挡 MADT 主路径；改码时勿当规范。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/acpi/acpi.c` | `acpi_probe_rsdp`；`acpi_table_sig_check`；签名枚举 |
| `modules/acpi/acpi_madt.c` | `get_next_ctrl_head` / `final_madt_int_ctrl_head` |
| `arch/.../acpi/acpi.c` | `acpi_init`；RSDT map + walk；`parser_facp` 空 |
| `arch/.../acpi/madt.c` | `parser_apic`；填 `NR_CPU` / `CPU_STATE` |
| `arch/.../mm/pmm.c` | `reserve_arch_region` 调探测 |
| `arch/.../PIC/IRQ.c` | 读 `madt_table->Local_int_ctrl_address` |

---

## 6. 流程

### 6.1 硬件 / 规范：RSDP 在哪

ACPI 要求在特定 BIOS 区域按 **16 字节对齐** 搜 `"RSD PTR "`。本实现：

| 窗 | 地址（相对 identity / `KERNEL_VIRT_OFFSET`） |
|----|-----------------------------------------------|
| 第一段 | `[0x80000, 0x80400)`（简化的 EBDA 附近窗；非完整「读 BDA 0x40E 再跟 EBDA」） |
| 第二段 | `[0xE0000, 0x100000)`（BIOS ROM 区） |

```text
   物理低区 identity map
   0x80000 ┌─────────────────────┐  ← 第一段（EBDA 附近）
           │ step 16 字节对齐扫描 │
   0x80400 └─────────────────────┘
   0xE0000 ┌─────────────────────┐  ← 第二段（BIOS ROM）
           │ step 16 字节对齐扫描 │
   0x100000└─────────────────────┘
              命中 → 比签名 "RSD PTR "（不校验 checksum）
```

步进 16。找到即返回指针；**不校验** 20 字节 checksum。找不到时 `reserve_arch_region` 仍 `SUCCESS`，只打印 `not find any rsdp`，`rsdp_addr` 保持 0——随后 `acpi_init(0)` 会把 0 当 RSDP 解，属于危险缺口（启动篇已点过）。

revision ≠ 0：reserve 与 `acpi_init` 都失败返回。

### 6.2 reserve（早期）

```text
acpi_probe_rsdp(KERNEL_VIRT_OFFSET)
  → setup_info->rsdp_addr = rsdp 虚址
  → rev==0：ROUND_DOWN(rsdt_address, 2MiB) 起一窗 reserve
  → rev!=0：失败
```

只 reserve **RSDT 所在页**，不预留整棵表树。其它 ACPI 表默认它们仍落在已映射 / 未进 buddy 的物理区（QEMU 常见）；换真机若表落在可分配 RAM，有被覆盖风险。

### 6.3 `acpi_init`（平台阶段）

```text
rev==0:
  若 RSDT 高半未 map → BSP Map_Handler 映 2MiB（GLOBAL|R|W|V）
  验 RSDT 签名
  for each u32 entry:
    KERNEL_PHY_TO_VIRT → 表头
    按签名分类 → FACP / APIC 调 parser；其它打印或 -E（返回值被忽略）
rev!=0: 失败
```

注意：`parser_acpi_tables` 的错误码在 walk 循环里 **未检查**；没有 MADT 时 `acpi_init` 仍可能 `SUCCESS`，`madt_table` 保持 NULL——`init_irq` 里解引用会崩溃。正常 QEMU 镜像会带 MADT。

**已知 bug（walk 未知表分支）：** `get_acpi_table_type_from_sig` 未命中时返回 **`-E_RENDEZVOS`（-1024）**，但 `acpi_init` 写的是 `if (sig == -1)`。条件永不成立 → 未知签名落入 `else`，把 `-1024` 当 `acpi_table_sig_enum` 交给 `parser_acpi_tables`（走 `default` 打印）。`acpi_table_sig_check` 本身返回 `bool`，RSDT 签名校验用 `!` 是对的——问题只在「枚举查找返回值 vs `== -1`」错位。

### 6.4 `parser_apic`

```text
NR_CPU = 0
CPU_STATE[*] = no_cpu
for_each_madt_ctrl_head:
  Local_APIC → 若 apic_id < MAX: NR_CPU++; STATE[id]=cpu_disable
  IO_APIC / Source_Override / default → break
```

之后 SMP 篇遍历 `0..MAX-1`，对 `cpu_disable` 状态的核发 INIT-SIPI。稀疏 APIC id（如 0,2）与 `cpu_id_is_online` 稠密假设的冲突见拓扑篇。

### 6.5 与中断控制器的接点

xAPIC 路径：`madt_table->Local_int_ctrl_address` 必须等于 **`xAPIC_MMIO_BASE`（0xFEE00000）**，否则 `init_irq` 直接 return，不 map LAPIC。x2APIC 路径不走这道基址检查（用 MSR 访问，不依赖 MMIO 基址）。MADT 里若有 Local APIC Address Override（type 5），本仓库 **不解析**——非默认基址的机器会卡在这道检查上。

---

## 7. 公开 API

本篇涉及的接口分布在：x86 ACPI 表发现与 MADT 消费——`acpi_probe_rsdp` / `acpi_init` / 签名分类、`parser_apic`、MADT 游标（`for_each_madt_ctrl_head`）、全局 `madt_table` / `fadt_table`。以头文件注释为准（`modules/acpi/acpi.h`、`acpi_madt.h`；已与 `.c` 核对）。

**本篇不涉及：** `NR_CPU` / `CPU_STATE` / `start_smp` 唤醒约定 → `28`；`Local_int_ctrl_address` 的 `0xFEE00000` 检查与 IOAPIC → `26`；`reserve_arch_region` 时序 → `06`/`04`；AML / XSDT / PCI MCFG。

### 7.1 编排顺序（调用方须遵守）

| 阶段 | 顺序 |
|------|------|
| 早期 PMM | `acpi_probe_rsdp(KERNEL_VIRT_OFFSET)` → 记 `setup_info->rsdp_addr` → rev0 时 reserve RSDT 2 MiB 窗 |
| 平台 | VSpace 就绪后 **`acpi_init(rsdp_addr)`** → walk RSDT → FACP/APIC |
| MADT | **`parser_apic`**（由 `acpi_init` 在见 APIC 签名时调用）→ 填拓扑 |
| 之后 | `init_irq` 读 `madt_table->Local_int_ctrl_address`；`arch_start_smp` 遍历 `cpu_disable` |

上层只**读** `NR_CPU` / `CPU_STATE` / `madt_table`；勿在业务路径改 `NR_CPU`。

### 7.2 探测与 init

```c
struct acpi_table_rsdp *acpi_probe_rsdp(vaddr search_start_vaddr);
bool acpi_table_sig_check(char *sig, char *expect);
enum acpi_table_sig_enum get_acpi_table_type_from_sig(struct acpi_table_head *);
error_t acpi_init(vaddr rsdp_addr);
```

| 接口 | 说明 |
|------|------|
| `acpi_probe_rsdp` | 两段 BIOS 窗、16 字节步进；**无** checksum。找不到返回 NULL。 |
| `acpi_init` | **仅 revision 0**；map RSDT；walk u32 条目。parser 返回值**被忽略**——无 MADT 仍可能 SUCCESS。 |
| `get_acpi_table_type_from_sig` | 未命中返回 **`-E_RENDEZVOS`**；`acpi_init` 却判 **`sig == -1`**（已知 bug，未知表误入 parser）。 |
| `acpi_table_sig_check` | 返回 **`bool`**；RSDT 路径 `!check` 正确。 |
| 签名分类 | ECDT 行绑 `ACPI_SIG_DSDT`（笔误，只影响打印）。 |

### 7.3 MADT

```c
error_t parser_apic(void);
struct madt_int_ctrl_head *get_next_ctrl_head(...);
bool final_madt_int_ctrl_head(madt, curr);
/* macro */ for_each_madt_ctrl_head(madt_table)
extern struct acpi_table_madt *madt_table;
```

| 接口 / 字段 | 说明 |
|-------------|------|
| `parser_apic` | 清 `CPU_STATE`→`no_cpu`；Local APIC：`NR_CPU++`、`STATE[id]=cpu_disable`。**不读** enable flags。 |
| `Local_int_ctrl_address` | 期望 `0xFEE00000`（消费在 `26`）。 |
| IOAPIC / ISO / x2APIC / Addr Override | 空 `break`；不解析。 |
| `online_capable` 宏 | `(1 < 1)` 笔误且未读。 |

### 7.4 不变量（实现）

- `NR_CPU` = 合法 Local APIC 条目**个数**，不是 `max_id+1`。
- `apic_id` 直接当下标（可稀疏）——与 `cpu_id_is_online` 稠密假设冲突见 `28`/`16`。
- `rsdp_addr==0` 仍调 `acpi_init` 会解引用——启动路径缺口。

---

## 8. 多架构

仅 x86_64。aarch64 用 DTB 枚举 CPU / GIC / UART，见下一篇 `36-DTB与设备树-aarch64.md`。MADT 头里虽枚举了 GICC/GICD 等 type，**x86 解析器不走那些分支**。

---

## 9. 测试

间接：多核 QEMU SMP 启动。无单独 ACPI 测试用例。

---

## 10. 限制与后续

- 忽略 Local APIC enable flags；disabled 条目仍可能被 SIPI。
- 无 XSDT；无 checksum；RSDP 缺失仍可能走到 `acpi_init(0)`。
- IOAPIC / ISO / Address Override 未消费。
- ECDT 签名表笔误；`online_capable` 宏笔误。
- RSDT walk 忽略 parser 返回值；无 MADT 时仍可能报成功。
- **已知 bug：** 未知表分支判 `sig == -1`，与 `get_acpi_table_type_from_sig` 返回 `-E_RENDEZVOS` 不一致。

远期项记 `v0.1/evolution/TODO.md`。

---

## 11. 变更记录

- 2026-10-04：补硬件知识——§4.1 加 ACPI 表层级结构图（RSDP→RSDT/XSDT→次级表）、RSDT vs XSDT 差异表、RSDP 签名与 checksum 说明、表头与表校验和说明；§4.2 加 MADT 项类型一览表（type 0~9）与游标推进图；§6.1 加 RSDP 搜索窗 ASCII 图。结构无大改。语言润色——「喂给」→「供…使用」、「认真消费」→「真正消费」、「扫」→「遍历」、「lock 住」→「reserve」、「被踩」→「被覆盖」、「炸」→「崩溃」、「挂在」→「卡在」、「脏点」→「瑕疵」；首次出现 INIT-SIPI 处补一句展开。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：核对 `.c`——补记 `get_acpi_table_type_from_sig` 返回 `-E_RENDEZVOS` vs `acpi_init` 判 `== -1`（已知 bug）；厘清 `acpi_table_sig_check` 为 `bool`。
- 2026-09-26：§7 全文审阅——`acpi.h` / `acpi_madt.h` Doxygen；两段式编排；vs `28`/`26`；flags 宏笔误标注。
- 2026-09-25：语言轮——补 ACPI 规范搜索窗 / RSDT / MADT 字段与 OS 依赖；两段式因果；enable 忽略与 FEE00000；签名表与 flags 宏脏点。
- 2026-08-29：整篇重做——两段式；消费/忽略表；与 SMP id 模型。
- 2026-08-27：初稿。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：任务 1/3/5 精读——§9「测例」→「测试用例」；§1「只有一件」→「只有一项」、「FACP 挂个」→「FACP 仅记录」；§1.1「记下来/分掉」→「记录下来/分配出去」；§6.1「属危险缺口」→「属于危险缺口」；§6.2「依赖它们」→「默认它们」；§6.5「MSR 访问」补「用 MSR 访问，不依赖 MMIO 基址」；任务 1 补首次出现的缩写全称——xAPIC（§1）、PIO（§3）、MADT_PCAT_COMPAT（§4.2）、MSR（§4.2.1）。
