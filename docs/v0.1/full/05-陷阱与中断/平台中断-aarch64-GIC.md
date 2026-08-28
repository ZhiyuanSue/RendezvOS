# 平台中断：aarch64 GIC

v0.1 · 2026-08-27

本篇覆盖：`arch/aarch64/gic/gic_v2.c`、`include/arch/aarch64/gic/gic_v2.h`、`include/arch/aarch64/gic/gic_v3.h`、`arch/aarch64/time/generic_time.c`、`include/arch/aarch64/time.h`。

DTB 中 GIC 节点见 `09-平台模块/DTB与设备树-aarch64.md`；IRQ 向量框架见 `IRQ向量分配与处理.md`；PSCI/SMP 见 `09-平台模块/PSCI与处理器电源-aarch64.md`。

---

## 1. 概述

aarch64 virt 机器在 v0.1 以 **GICv2** 为主路径：`gic_v2_probe` 从设备树 **`compatible`** 读 `reg`，`map_gic_mem` 把 **GICD/GICC** 映射为 **DEVICE** 内存，随后配置 mask/unmask、优先级、EOI。`gic_v2.h` 定义 distributor / CPU interface 寄存器布局；**GICv3/v4** 头文件存在，frozen bring-up 以 v2 为准。

**EOI** — handler 返回后 trap 层 `arch_eoi_irq` 写 GICC_EOIR（实现见 gic 与 trap 协作）。**SGI** 用于 IPI；**SPI** 用于共享外设中断。

---

## 2. 目标与边界

core 提供 **virt 平台可启动** 的 GICv2 最小驱动，不是 GIC 完整抽象层（无 Linux irqchip 层级）。**`gicd_v2_set_affinity`** 等 API 供 arch 内部 SMP 使用，非 general 公共 API。

GICv3 ITS/MSI 不在 v0.1 范围。

---

## 3. 分层与调用方

**早期 boot** — DTB 解析后 `gic_v2_probe`；`start_arch.c` 中 **`gic.init_cpu_interface()`**（函数指针表 `gic` 结构）在 AP 上重复 init。

**驱动** — unmask SPI：`gicd_v2_unmask_irq(irq_num)`；确保对应 trap id 已在 `irq_vector` 注册。

**timer** — `generic_time.c` 使用 arch timer + GIC 中断线（与 GIC SPI 号配置一致）。

---

## 4. 数据结构与不变量

- **全局 `gic`** — 含 `gicd`、`gicc` 虚拟指针与 ops（init、EOI 等）。
- **IRQ 号空间** — SGI 0–15、PPI、SPI 自 32 起（GIC 惯例）；与 `trap_info` 编码一致。
- **映射属性** — DEVICE nGnRnE，避免 cache 维护误用。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `gic_v2.c` | probe、map、mask/unmask、EOI、CPU if init |
| `gic_v2.h` | 寄存器结构、inline helper |
| `gic_v3.h` | 占位/未来扩展 |
| `generic_time.c` | arch timer + 中断 |

---

## 6. 流程

### 6.1 Probe 与映射

1. `dev_node_find_by_compatible(..., gic.compatible)`。
2. 读 `reg` 四元组：GICD phys/len、GICC phys/len。
3. `map_gic_mem` → 设置 `gic.gicd` / `gic.gicc` 内核虚拟地址。

### 6.2 中断处理链

```mermaid
flowchart LR
  A[GIC IRQ] --> B[exception vector]
  B --> C[trap_handler]
  C --> D[ISR]
  D --> E[arch_eoi_irq GICC]
```

### 6.3 Affinity（arch 内部）

`gicd_v2_set_affinity` 写 GICD_ITARGETSR — AP 启动时绑定 PPI/SGI/SPI 路由；详见 SMP 与 PSCI 篇交叉引用。

---

## 7. 公开 API

GIC 函数多数为 **arch 内部**；rendezvos 公开中断 API 仍为 `register_irq_handler` 等 trap 层。模块通过 DTB + unmask helper 间接使用。

---

## 8. 多架构

本篇 **仅 aarch64 GIC**。x86 见 APIC/PIC 篇。

---

## 9. 测试

```bash
cd core && make ARCH=aarch64 config && make all && make run
```

QEMU virt + GICv2：timer、SMP、IPI 测例。

与当前源码一致，尚未复测。

---

## 10. 限制与后续

- **GICv3 primary** — virt 默认仍常配 GICv2；迁移 v3 须重写 probe/EOI。
- **LPI/ITS** — 未实现。
- **与 PCI INTx 路由** — DTB 与 PCI 节点并存问题见 DTB 篇。

---

## 11. 变更记录

| 日期 | 摘要 |
|------|------|
| 2026-08-27 | 初稿：GICv2 probe、映射、EOI 链 |
