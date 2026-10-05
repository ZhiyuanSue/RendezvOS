# DTB 与设备树（aarch64）

v0.1 · 2026-09-27

本篇覆盖：`modules/dtb/*`（裁剪 libfdt、`dev_tree`、`property`、`print_property`）、以及 **`build_device_tree` 的真实落点** `arch/aarch64/boot/start_arch.c`；early raw 消费点在 `arch/aarch64/boot/boot_map.c` 与 aarch64 `pmm`。

Early map / DTB（Device Tree Blob，设备树二进制块）拷贝时序见 `05-平台启动-aarch64.md`；memmap 的 `memory`+`reg` 见物理内存篇；GIC（Generic Interrupt Controller，通用中断控制器）/ PSCI（Power State Coordination Interface，电源状态协调接口）/ UART / SMP 消费者见对应专篇。本篇**不**假设 aarch64 有 PCI ECAM（Enhanced Configuration Access Mechanism，增强型配置访问机制）扫描器。

**规范对照：** Devicetree Specification（FDT 二进制：magic `0xd00dfeed`、struct/strings、大端属性）；[arm64 booting](https://docs.kernel.org/arch/arm64/booting.html)（DTB 对齐与传递约定）。节点 binding（`arm,pl011`、`arm,psci*`、GIC、`cpu`）以 DT 源与本仓库查找字符串为准。

---

## 1. 概述

aarch64 **不用** ACPI 枚举板级资源；**Flattened Device Tree（FDT）** 是板级硬件描述的权威来源。固件 / QEMU 按 arm64 booting 约定把 DTB 物理地址放进 `x0` 寄存器（详见 §4.1.4），内核入口将其存入 `setup_info`，再分两层消费：

| 层 | 何时 | 工具 | 用途 |
|----|------|------|------|
| **MMU-off / early raw FDT** | `boot_map`、early pmm | `raw_get_prop_from_dtb` / `fdt_*` | PL011 `reg`、`device_type=memory` 物理区 |
| **`device_node` 树** | `arch_start_platform`（已有 `kallocator`） | `build_device_tree` + `dev_node_find_*` | PSCI、GIC、`chosen/bootargs`、`cpu` |

先 raw 应急（还没有堆、不能建树），后建树供驱动 probe。两阶段**不要混用指针**：early 用的是物理 / 早期虚址上的 blob；平台阶段用的是 map 后的 `boot_dtb_header_base_addr` 与堆上的 `device_root`。

### 1.1 `build_device_tree` 在哪

实现写在 **`arch/aarch64/boot/start_arch.c`**，**不**在 `modules/dtb`。modules 提供 walk / 查找 / 属性类型；arch 负责「用 BSP allocator 从 FDT 递归构建 C 树」。旧名 `dev_tree_build` 不存在。
---

## 2. 目标与边界

**提供：** `fdt_check_header`；递归建树；按 name / `device_type` / `compatible` 查找；属性按类型表做 endian 安全读取。

**不做：** OF overlay、运行期改树、完整上游 libfdt、Linux `of_*` API。
**不做：** 规定 PCI 来源——aarch64 **无** `modules/pci` 消费路径；DT 里即便有 `pci-host-ecam-generic` 也**无人解析**。不要写「与 ECAM 并存须防冲突」这类并不存在的问题。

---

## 3. 分层与调用方

| 消费者 | 查找方式 | 关键串 |
|--------|----------|--------|
| early UART | raw compatible | `arm,pl011`（ARM PrimeCell PL011 UART）→ `reg`（假定两个 64-bit addr/size） |
| pmm | raw `device_type` | `memory` → `reg` |
| PSCI | 树 + compatible | `arm,psci`（list 常另含 `arm,psci-0.2`；查找为逐条全等） |
| GIC | 树 + compatible | **写死** `arm,cortex-a15-gic`（仅 `arm,gic-400` 的 blob **对不上**） |
| SMP | 树 + type | `device_type=cpu` + `reg` + `enable-method` |
| cmdline | 树 + name | `chosen` → `bootargs` |

典型形态：`dev_node_find_by_compatible(NULL, "…")`（`NULL` 表示从 `device_root` 起）→ `dev_node_find_property` → 读 `reg` / `method`。

---

## 4. 数据结构与不变量

### 4.1 FDT blob（硬件 / 规范）

`struct fdt_header`：magic、`totalsize`、`off_dt_struct` / `off_dt_strings`、version（规范现行多为 17）等。结构块标签：`FDT_BEGIN_NODE` / `END_NODE` / `PROP` / `NOP` / `END`。属性与多字节细胞大端——读 `len`/`reg` 须 `SWAP_ENDIANNESS_32`（或 property 层封装）。

#### 4.1.1 FDT 整体布局

FDT（Flattened Device Tree）是设备树的二进制序列化格式，整个 blob 在内存里按下列顺序排布，所有偏移字段均相对 blob 起始、大端：

```text
   ┌────────────────────────────┐  ← off = 0
   │  fdt_header  (40 字节固定)   │
   │   magic 0xd00dfeed           │
   │   totalsize / off_dt_struct  │
   │   off_dt_strings / off_mem_rsv│
   │   version / last_comp_version│
   │   boot_cpuid_phys / size_dt_  │
   │   size_dt_strings            │
   ├────────────────────────────┤  ← off_mem_rsvmap
   │  memory reservation block    │  {address(u64), size(u64)}[]
   │  以全 0 的 16 字节条目结尾     │
   ├────────────────────────────┤  ← off_dt_struct (4字节对齐)
   │  structure block             │  节点/属性序列化（见 4.1.3）
   ├────────────────────────────┤  ← off_dt_strings
   │  strings block                │  属性名以 \0 分隔去重存放
   └────────────────────────────┘  ← totalsize
```

magic `0xd00dfeed`（读作 "d00d feed" / "dude feed"）是 FDT 唯一标识，`fdt_check_header` 第一步就比它。`version` 字段表示 blob 版本，`last_comp_version` 是能兼容读的最低版本；本仓库只读不写，对版本 17（现行）的 blob 直接接受。

#### 4.1.2 memory reservation block

`off_mem_rsvmap` 指向一组 `{u64 address; u64 size}` 条目，描述「固件保留、OS 不可分配」的物理区（如 secure 世界内存、firmware runtime）。条目以全 0 的 16 字节结尾。本仓库**不**消费这一块——memmap 只看 structure block 里的 `device_type=memory` 节点；保留区若与 `memory` 节点重叠，理论上应优先 reserve，当前实现未做，属于已知简化（见 §10）。

#### 4.1.3 structure block 编码

structure block 是一串 32 位大端 token，token 取值：

| token | 值 | 含义 |
|-------|----|----|
| `FDT_BEGIN_NODE` | 1 | 节点开始，后跟 NUL 结尾节点名（按 4 字节对齐补 0） |
| `FDT_END_NODE` | 2 | 节点结束 |
| `FDT_PROP` | 3 | 属性：后跟 `len(u32)`、`nameoff(u32)`、`len` 字节数据（4 字节对齐） |
| `FDT_NOP` | 4 | 占位，跳过 |
| `FDT_END` | 9 | 整个 structure block 结束 |

`nameoff` 是属性名在 strings block 里的字节偏移；strings block 把所有属性名以 `\0` 分隔、去重存放，多个节点共用同一属性名时只存一份。属性值（prop values）按 DT binding 解释：常见形态有 `<u32>`（1 cell）、`<u64>`（2 cell）、`<u32 addr; u32 size>`（reg pair，cell 数由父节点 `#address-cells`/`#size-cells` 决定）、string、string list（`\0` 分隔）。注意 `#address-cells`/`#size-cells` **不沿树继承**——每个有子节点的节点都须显式声明；缺省时规范取 `#address-cells=2`、`#size-cells=1`（QEMU virt 常用 `2/2`，是平台约定而非规范缺省）。`device_type` 在规范中已 deprecated，仅 `cpu`/`memory` 节点为 IEEE 1275 兼容保留——本仓库 pmm/SMP 仍依赖它。标准节点除 `/cpus`、`/memory`、`/chosen` 外，还有 `/aliases`（别名表）、`/reserved-memory`（保留区，与 reservation block 互补）。

```text
   structure block 流（简化）：
   BEGIN_NODE "/"           ← 根节点
     PROP "compatible" = "arm,vexpress"
     BEGIN_NODE "cpus"
       PROP "#address-cells" = <1>
       PROP "#size-cells"    = <0>
       BEGIN_NODE "cpu@0"
         PROP "device_type"  = "cpu"
         PROP "reg"          = <0>
         PROP "enable-method"= "psci"
       END_NODE
     END_NODE
   END_NODE                 ← 根节点也必须 END_NODE
   END
```

#### 4.1.4 aarch64 启动时 DTB 指针约定

按 [arm64 booting](https://docs.kernel.org/arch/arm64/booting.html) 约定，bootloader 把内核镜像加载到主内存后，必须满足：

- **x0 寄存器**保存 DTB 的物理地址（64 位、8 字节对齐、位于低 128 MiB RAM 内）；
- DTB 不得被解压或内核镜像覆盖；
- 内核入口时 MMU 关闭、内核镜像地址范围须 clean 到 PoC（D-cache 可开可关但不得有 stale 项）、所有中断屏蔽。

本仓库的早期入口从 `x0` 取出该物理地址，写入 `setup_info`，后续 `boot_map` / `prepare_arch` 据此 map 与校验。QEMU `-machine virt` 默认遵守此约定。

arm64 booting：DTB 8 字节对齐、位于低 128 MiB RAM 内（协议本身**无** 2 MiB 大小限制）。本仓库 early 路径用 2 MiB block 映射（aarch64 L2 block on 4K granule = 2 MiB），故 DTB 实际应 &lt; 2 MiB 以一次 block 覆盖；细节见平台启动篇。

### 4.2 软件树

- `device_root`：`arch_start_platform` 之后的全局根。
- 每节点：`name`、属性链表、孩子 / 兄弟（`dev_tree_get_next` 深度优先遍历用）。
- `compatible`：NUL 分隔的 string list；匹配逻辑见下。
- 未识别名称的属性 → `PROPERTY_TYPE_OTHER`，数据仍保留。

#### 4.2.1 `device_node` 树结构

`build_device_tree` 把扁平的 structure block 递归展开成 C 树，节点之间用「孩子 / 兄弟」二叉链表表示（左孩子右兄弟法），便于深度优先遍历：

```text
   device_root ("/")
        │ child
        ▼
   ┌──────────┐ sibling ┌──────────┐ sibling ┌──────────┐
   │  cpus    │ ──────► │  gic     │ ──────► │ chosen   │
   └──────────┘         └──────────┘         └──────────┘
        │ child              │ child               (无 child)
        ▼                    ▼
   ┌──────────┐ sibling  ┌──────────┐ sibling
   │  cpu@0   │ ──────► │  cpu@1   │ ──────► …
   └──────────┘         └──────────┘
   (属性: device_type=cpu, reg, enable-method, …)
```

节点与属性对象本身分配在 BSP `kallocator` 上，但属性**名**与**值**的数据指针仍指向 map 后的 FDT blob——不复制。这意味着只要 `device_root` 在用，`boot_dtb_header_base_addr` 指向的 blob 必须保持 mapped；early raw blob 与平台阶段 blob 是两段不同映射，勿混指针域。

#### 4.2.2 phandle 引用

DT 允许节点用 `phandle`（property handle，属性句柄；或别名 `linux,phandle`）属性给自己分配一个 u32 句柄，其它节点再用 `clocks`、`interrupt-parent`、`gpios` 等属性以 `<u32 phandle>` 形式引用它，构成跨节点引用。例如 GIC 的 distributor/control 接口常由 `interrupt-parent = <&gic>` 风格引用 GIC 节点的 phandle。

本仓库**不**做通用 phandle 解析——`dev_node_find_by_compatible` 只按字符串匹配，不解析 `<>` 引用；GIC / PSCI 消费者各自直接按 compatible 找节点再读 `reg`，因此即便 DT 里写了 phandle 引用链，本仓库也不走那条路径。

### 4.3 compatible 匹配（逐条全等）

`_dev_node_find_by_compatible`：在 NUL 分隔的 compatible string list 上，对**每条**串做字符全等——仅当搜索串与当前条目同时走到 `\0` 才命中。因此查找 `arm,psci` **不会**命中单条的 `arm,psci-0.2`；常见 DT 会在 list 里同时列出 `arm,psci-0.2` 与 `arm,psci`，从而仍可命中。GIC 写死的串过长时，若 blob 只有 `arm,gic-400` 会对不上。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/dtb/dtb.c` + `fdt.h` / `libfdt.h` | 裁剪 fdt walk、`fdt_check_header`、`raw_get_prop_from_dtb` |
| `dev_tree.c` / `dev_tree.h` | 查找 API、`device_root` |
| `property.c` | 类型表、读属性 |
| `print_property.c` | 调试打印 |
| `arch/.../boot/boot_map.c` | MMU 前 raw 读 pl011 `reg` 并 map |
| `arch/.../boot/start_arch.c` | `map_dtb`、`build_device_tree`、cmdline、调 PSCI/GIC |
| `arch/.../mm/pmm.c` | raw `memory` 区 |

---

## 6. 流程

### 6.1 时间轴

```text
boot_map（MMU 前）:
  raw 找 arm,pl011 → 映 UART MMIO；拷贝/对齐 DTB 到高半可用窗
prepare_arch:
  map_dtb（2MiB 窗）+ fdt_check_header → 写 boot_dtb_header_base_addr
arch_init_pmm:
  raw 遍历 device_type=memory → buddy 可用区（见物理内存篇）
arch_start_platform:
  device_root = build_device_tree(kallocator, …)
  chosen/bootargs → cmdline_ptr
  psci_init(); gic.probe(); gic.init_distributor()
arch_start_smp（稍后）:
  遍历 device_type=cpu，读 enable-method / reg
```

### 6.2 Early raw：为何手写 `arm,pl011` 查找

此时没有虚存堆，不能 `build_device_tree`。`boot_get_uart_info` 在物理地址空间里用 `raw_get_prop_from_dtb` 按 compatible 查找，再按 **#address-cells/#size-cells = 2** 的常见 virt 布局拆 `reg`（两对 u32 → 64-bit 基址与长度）。找不到或长度 &gt; 2 MiB → `boot_Error()`。字符数组逐字节赋值，是为了避免早期依赖可写全局字符串的问题。

### 6.3 建树

`build_device_tree`：从 struct 偏移递归——节点名、属性（经 strings 块解析名）、子节点；节点与属性对象来自 **BSP `kallocator`**。失败路径依赖 allocator 行为；正常 virt 镜像应能构建出完整树。

### 6.4 与硬件中断 / 电源的接点

- **GIC：** probe 用写死 compatible；从节点 `reg` 取 GICD/GICC 基址（细节 GIC 篇）。DT 只有 `arm,gic-400` 时会找不到节点。
- **PSCI：** 从 DT 读 method（SMC/HVC）与功能 ID（PSCI 篇）。
- **UART：** 运行期 `uart_open` 用 early 映好的高半窗；不再二次查找 DT。

---

## 7. 公开 API

本篇涉及的接口分布在：FDT / 设备树软件面——裁剪 `fdt_*` / `raw_get_prop_from_dtb`（`dtb.h`）、`build_device_tree` + `device_root` + `dev_node_find_*` / `property_read_*`（`dev_tree.h`；建树实现在 `start_arch.c`）。以头文件注释为准（见上列头文件；已与 `.c` 核对）。

**本篇不涉及：** `map_dtb` / `prepare_arch` / `arch_start_platform` 编排 → `05`；early UART map → `05`/`34`；memmap `memory` 消费 → `06`；GIC / PSCI / SMP 对查找结果的使用 → `27`/`39`/`28`。

### 7.1 编排顺序（调用方须遵守）

| 阶段 | 工具 | 说明 |
|------|------|------|
| MMU-off / early | **`raw_get_prop_from_dtb`** | 无堆；物理/早期 VA 上的 blob |
| `prepare_arch` | **`fdt_check_header`** | 已 map 的 `boot_dtb_header_base_addr` |
| `arch_start_platform` | **`build_device_tree`** → `device_root` | 须已有 BSP `kallocator` |
| 驱动 probe | `dev_node_find_*` → `dev_node_find_property` → `property_read_*` | `node==NULL` → 从 `device_root` |

**勿混** early blob 指针与树节点指针。

### 7.2 Raw FDT（`dtb.h`）

```c
int fdt_check_header(const void *fdt);
struct fdt_property *raw_get_prop_from_dtb(void *fdt, int offset, …,
                                           const char *cmp_str,
                                           const char *cmp_type_str,
                                           u64 mode, void (*f)(…));
/* + fdt_next_node / first_subnode / property_offset / fdt_string … */
```

| 接口 | 说明 |
|------|------|
| `fdt_check_header` | magic / size；失败 → `prepare_arch` 返回错 → `cmain` panic。 |
| `raw_get_prop_from_dtb` | early UART（`arm,pl011`）与 pmm（`memory`）；SINGLE/MUL 模式。 |

### 7.3 建树与查找（`dev_tree.h`）

```c
struct device_node *build_device_tree(struct allocator *malloc,
                                      struct device_node *parent,
                                      void *fdt, int offset, int depth);
extern struct device_node *device_root;

struct device_node *dev_node_find_by_name / _by_type / _by_compatible(…);
struct property *dev_node_find_property(node, name, n);
error_t property_read_string / _u32 / _u64 / _u*_arr(…);
```

| 接口 | 说明 |
|------|------|
| `build_device_tree` | 实现在 **`start_arch.c`**；名字/属性数据仍指向 FDT blob。 |
| `dev_node_find_by_compatible` | 对 list 中每条 NUL 串**全等**匹配（非前缀）；`arm,psci` 命中靠 list 含该条目（常与 `arm,psci-0.2` 并存）。 |
| GIC 消费者 | 写死 `arm,cortex-a15-gic`（仅 `arm,gic-400` 的 blob 对不上）。 |

调试：`print_device_tree` / `parse_print_dtb`（非常用路径，约定从略）。

---

## 8. 多架构

仅 aarch64 主线消费。x86 用 ACPI / MADT（上一篇）。riscv 若将来接 FDT，可复用 `modules/dtb`，但当前无对等平台编排。

---

## 9. 测试

间接：QEMU `virt` 启动（UART / GIC / PSCI / SMP）。无单独 DTB 测试用例。

---

## 10. 限制与后续

- compatible 为 string list 上的**逐条全等**（非前缀）；过短搜索串不会“吃掉”长串。
- GIC compatible **写死** `arm,cortex-a15-gic`。
- PCI host 节点未接入。
- early raw 与 `device_root` 两阶段指针域不同，勿混。
- early UART 假定 `#address-cells/#size-cells` 布局；换板可能要改解析。
- 不消费 memory reservation block（见 §4.1.2）；保留区与 `memory` 节点重叠时未做优先 reserve。
- 不做通用 phandle 解析（见 §4.2.2）；跨节点引用链不走本仓库路径。

---

## 11. 变更记录

- 2026-10-05：修正 §4.1.4 两个 "2 MiB" 的混淆——arm64 booting 协议**无** "DTB < 2 MiB" 要求（协议只要求 DTB 位于低 128 MiB RAM 内、8 字节对齐）；"2 MiB" 实为 early map 的 block 粒度（aarch64 L2 block on 4K granule = 2 MiB），DTB 应 < 2 MiB 是该实现的一次覆盖约束，非协议上限。原 "通常 < 2 MiB" 改为 "位于低 128 MiB RAM 内"，并补两者关系说明。
- 2026-10-04：补硬件知识——§4.1 加 FDT 整体布局 ASCII 图（header/reservation/struct/strings 四块）、structure block token 表与节点序列化示例、memory reservation block 说明、aarch64 启动时 `x0` 传 DTB 指针约定；§4.2 加 `device_node` 树结构图（孩子/兄弟二叉链表）与 phandle 引用说明。结构无大改。语言润色——「假装」→「假设」、「长出 C 树」→「构建 C 树」、「扫」→「遍历」、「搜 compatible」→「按 compatible 查找」；§10 补 memory reservation block 与 phandle 两条限制交叉引用。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——`dtb.h`/`dev_tree.h` Doxygen；`build_device_tree` 声明进 `dev_tree.h`；划清 vs `05`/`27`/`39`。
- 2026-09-26：核对——changelog 误写「compatible 前缀」改为与正文/Doxygen 一致的**逐条全等**。
- 2026-09-25：语言轮——FDT 规范 / arm64 booting；raw vs 树因果；compatible **逐条全等**（非前缀）；GIC 写死串；明确无 ACPI、无 PCI 消费。
- 2026-08-29：整篇重做——`build_device_tree` 真位置；删 PCI 双源恐吓；消费者表。
- 2026-08-27：初稿。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：硬件事实核对（对照 Devicetree Spec v0.4 / arm64 booting）——修正 structure block 示例根节点缺 END_NODE；修正 arm64 booting「D-cache 关闭」为「镜像范围 clean 到 PoC」（与现行 kernel docs 一致）；补 #address-cells/#size-cells 不继承与缺省值（2/1）、device_type 已 deprecated、/aliases 与 /reserved-memory 标准节点。
- 2026-10-05：任务 1/3/5 精读——§9「测例」→「测试用例」；§1「救急/给驱动 probe」→「应急/供驱动 probe」；§2「假问题」→「并不存在的问题」；§4.1.1「dood feed」→「d00d feed / dude feed」（修正 magic 读法）；§4.1.2「属已知简化」→「属于已知简化」；§4.2「未知名属性」→「未识别名称的属性」；§4.2.2 phandle 补全称「property handle，属性句柄」、「interrupts-parent」→规范名「interrupt-parent」、「跨树边引用」→「跨节点引用」；§4.3「搜」→「查找」、「写死过长串时」→「写死的串过长时」；§6.2「的坑」→「的问题」；§6.3「建出完整树」→「构建出完整树」；§6.4「搜 DT」→「查找 DT」；任务 1 补首次出现的缩写全称——DTB、GIC、PSCI、ECAM（篇首）、PL011（§3）、phandle（§4.2.2）。
