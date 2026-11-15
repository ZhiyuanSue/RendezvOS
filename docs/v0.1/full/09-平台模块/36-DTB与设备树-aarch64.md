# DTB 与设备树（aarch64）

v0.1 · 2026-09-27

本篇覆盖：`modules/dtb/*`（裁剪 libfdt、`dev_tree`、`property`、`print_property`、`of`）、以及 **`build_device_tree` 的真实落点** `arch/aarch64/boot/start_arch.c`；early raw 消费点在 `arch/aarch64/boot/boot_map.c` 与 aarch64 `pmm`，树路径消费点还在 `smp.c`、`psci.c`、`gic.c`、`time/generic_time.c`。

Early map / DTB（Device Tree Blob，设备树二进制块）拷贝时序见平台启动篇（`05`）；memmap 的 `memory`+`reg` 见物理内存篇（`06`）；GIC（Generic Interrupt Controller，通用中断控制器）/ PSCI（Power State Coordination Interface，电源状态协调接口）/ UART / SMP 消费者见对应专篇。本篇**不**假设 aarch64 有 PCI ECAM（Enhanced Configuration Access Mechanism，增强型配置访问机制）扫描器。

**规范对照：** Devicetree Specification（FDT 二进制：magic `0xd00dfeed`、struct/strings、大端属性）；[arm64 booting](https://docs.kernel.org/arch/arm64/booting.html)（DTB 对齐与传递约定）。节点 binding（`arm,pl011`、`arm,psci*`、GIC、`cpu`）以 DT 源与本仓库查找字符串为准。

---

## 1. 概述

aarch64 **不用** ACPI 枚举板级资源；**Flattened Device Tree（FDT）** 是板级硬件描述的权威来源。固件 / QEMU 按 arm64 booting 约定把 DTB 物理地址放进 `x0` 寄存器（详见 §4.1.4），内核入口将其存入 `setup_info`，再分两层消费：

| 层 | 何时 | 工具 | 用途 |
|----|------|------|------|
| **MMU-off / early raw FDT** | `boot_map`、early pmm | `raw_get_prop_from_dtb` / `fdt_*` | PL011 `reg`、`device_type=memory` 物理区 |
| **`device_node` 树** | `arch_start_platform`（已有 `kallocator`） | `build_device_tree` + `dev_node_find_*` / `of_find_matching_node` | PSCI、GIC、`chosen/bootargs`、`cpu`、arch_timer 中断号 |

先 raw 应急（还没有堆、不能建树），后建树供驱动 probe。两阶段**不要混用指针**：early 用的是物理 / 早期虚址上的 blob；平台阶段用的是 map 后的 `boot_dtb_header_base_addr` 与堆上的 `device_root`。

### 1.1 `build_device_tree` 在哪

实现写在 **`arch/aarch64/boot/start_arch.c`**，**不**在 `modules/dtb`。modules 提供 walk / 查找 / 属性类型；arch 负责「用 BSP allocator 从 FDT 递归构建 C 树」。旧名 `dev_tree_build` 不存在。

---

## 2. 目标与边界

**提供：** `fdt_check_header`；递归建树；按 name / `device_type` / `compatible` 查找（另有 `of_find_matching_node` 表驱动匹配）；`property_read_{string,u8,u16,u32,u64}` 及数组版按固定宽度读取并逐细胞换端。注意两点易混：**`property_read_*` 完全不查类型表**——它们只是把 `data` 按声明宽度解析、每细胞 `SWAP_ENDIANNESS_*`；类型表（`property_types[]`）只服务于建树时的属性过滤与调试打印两条路径（见 §6.3）。

**不做：** OF overlay、运行期改树、完整上游 libfdt、Linux `of_*` API（`of.c` 只有一个 `of_find_matching_node`，与 Linux 同名 API 语义无关）。
**不做：** 规定 PCI 来源——aarch64 **无** `modules/pci` 消费路径；DT 里即便有 `pci-host-ecam-generic` 也**无人解析**。不要写「与 ECAM 并存须防冲突」这类并不存在的问题。

---

## 3. 分层与调用方

| 消费者 | 查找方式 | 关键串与读法 |
|--------|----------|--------------|
| early UART | raw compatible | `arm,pl011`（ARM PrimeCell PL011 UART），读其 `reg`（按 2+2 cell 拆两对 u32 拼 64-bit）；raw 路径只比对属性值的**第一条**串（见 §4.3） |
| pmm | raw `device_type` | `device_type=memory` 节点的 `reg`；MUL 模式，每条 `reg` 回调 `get_mem_prop_and_insert_region` 插入 `m_regions` |
| PSCI | 树 + compatible | `arm,psci`（list 常另含 `arm,psci-0.2`；查找为逐条全等）；`method` 用 `strcmp` 全等定 SMC/HVC，再按 `migrate` / `cpu_on` / `cpu_off` / `cpu_suspend` 属性名决定装哪些函数指针——缺功能 ID 属性只打印并提前 `return`，`enable` 此前已置 true（记档） |
| GIC | 树 + compatible | `gic_probe` 自带 `gic_dt_drivers[]` 表，**逐项调 `dev_node_find_by_compatible`，未用 `of_find_matching_node`**：`arm,cortex-a15/a7/a9-gic`、`arm,gic-400`、`arm,arm11mp-gic`；`arm,gic-v3` 匹配但 v0.1 的 bind 失败（见 `27`） |
| arch_timer | 树 + `of_find_matching_node` | `arm,armv8-timer`；读 `interrupts` 属性第 **4** 个 tuple（非安全物理中断 CNTP，偏移 3×3 cell），要求 type=PPI；BSP 解析一次缓存进 `timer_irq_num`，解析失败或非 BSP 无缓存 → `kernel_panic` |
| SMP | 树 + type | `device_type=cpu` + `reg` + `enable-method`（`strcmp_s(…, "psci", 5)`，只接受 psci，否则打印跳过该 CPU） |
| cmdline | 树 + name | `dev_node_find_by_name` 找 `chosen` 后**手写遍历属性链表**找 `bootargs`（未走 `dev_node_find_property`） |

典型形态：`dev_node_find_by_compatible(NULL, "…")`（`NULL` 表示从 `device_root` 起），再 `dev_node_find_property(node, name, n)`，然后读 `reg` / `method`。`n` 是内部 `strcmp_s` 的比对上限（最多比 `n` 个字符、遇 `str1` 的 `\0` 提前停）：调用点惯例传 `strlen+1`（含终止符，等价全等），如 `"method"` 配 7、`"cpu_on"` 配 7；`generic_time.c` 传裸 `strlen` 也可——两侧同长度相等时恰好都截在同一位置。

---

## 4. 数据结构与不变量

### 4.1 FDT blob（硬件 / 规范）

`fdt.h`：

```c
struct fdt_header {
        u32 magic; /* FDT_MAGIC = 0xd00dfeed */
        u32 totalsize;
        u32 off_dt_struct;
        u32 off_dt_strings;
        u32 off_mem_rsvmap;
        u32 version; /* 规范现行多为 17 */
        u32 last_comp_version;
        u32 boot_cpuid_phys;
        u32 size_dt_strings;
        u32 size_dt_struct;
};
```

结构块标签：`FDT_BEGIN_NODE` / `END_NODE` / `PROP` / `NOP` / `END`。属性与多字节细胞大端——读 `len`/`reg` 须 `SWAP_ENDIANNESS_32`（或 property 层封装）。

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

magic `0xd00dfeed`（读作 "d00d feed" / "dude feed"）是 FDT 唯一标识，`fdt_check_header` 第一步就比它。`version` 字段表示 blob 版本，`last_comp_version` 是能兼容读的最低版本。本仓库只读不写，接受版本区间 `0x02`–`0x11`（`FDT_FIRST_SUPPORTED_VERSION` / `FDT_LAST_SUPPORTED_VERSION`，即 v2–v17；QEMU 生成的多为现行 v17）；header 自身长度按版本在 `FDT_V1/V2/V3/V16/V17_SIZE` 间选取。`fdt_check_header` 的完整顺序：magic → `version` 落在支持区间 → `version >= last_comp_version` → `totalsize ∈ [header 长度, INT_MAX]` → reservation block 不越界 → （v≥17 时）structure block 不越界 → strings block 不越界。`dtb.c` 头部注释注明代码移植自 u-boot（2024-05）；`fdt_next_tag` 挂 `optimize("no-jump-tables")`，规避早期无跳表重定位环境。

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

协议本身**无** DTB 大小上限——见上，只约束对齐与「位于低 128 MiB RAM 内」。本仓库 early 路径用 2 MiB block 映射（aarch64 L2 block on 4K granule = 2 MiB），故 DTB 实际应 < 2 MiB 才能被一个 block 整个覆盖；细节见平台启动篇（`05`）。

### 4.2 软件树：为什么要再解析成 C 对象

§4.1 是固件传来的扁平二进制。bootloader 传递用扁平格式合适；内核里若反复按名查找、深度优先走树，则解析成「节点 + 属性链表」更方便。`build_device_tree` 之后，全局根是 `device_root`。

```c
struct device_node {
        struct tree_node dev_node;
        char *name;
        struct property *property;
};
extern struct device_node *device_root;

struct property {
        char *name;
        void *data;
        int len;
        struct property *next;
} __attribute__((packed));
```

| 字段 | 为什么要有 |
|------|-----------|
| `dev_node` | 孩子/兄弟二叉表示，供深度优先遍历（`dev_tree_get_next`）。 |
| `name` | 节点名（如 `cpu@0`），`find_by_name` 用。 |
| `property` | 该节点属性单链表头。 |
| `property.name` / `data` / `len` | 属性名、值指针、字节长度。**名和值的数据常常仍指向 map 后的 FDT blob，不复制**——blob 映射必须活着。 |
| `property.next` | 同一节点上的下一条属性。 |

`compatible` 是 NUL 分隔的字符串列表，匹配逻辑见 §4.3。注意 `struct property` **没有类型字段**——属性名到类型的映射（`get_property_type`）只在建树过滤与调试打印时按需查询；未识别名称的属性照常保留（回退值 `PROPERTY_TYPE_OTHER` 非零，过得了 `if (p_type)`），只是打印时按空值解码（见 §6.3）。

#### 4.2.1 `device_node` 树结构

`build_device_tree` 把扁平的 structure block 递归展开成 C 树，节点之间用「孩子 / 兄弟」二叉链表表示（左孩子右兄弟法），便于深度优先遍历：

```text
  device_root ("/")
        │ child
        ▼
      cpus
        │
        ├─ sibling → gic
        │              │ child
        │              ▼
        │            （gic 子节点…）
        │
        ├─ sibling → chosen（无 child）
        │
        └─ child
             ▼
          cpu@0
             │
             ├─ sibling → cpu@1 → …
             │
             └─ 属性: device_type=cpu, reg, enable-method, …
```

节点与属性对象本身分配在 BSP `kallocator` 上，但属性**名**与**值**的数据指针仍指向 map 后的 FDT blob——不复制。这意味着只要 `device_root` 在用，`boot_dtb_header_base_addr` 指向的 blob 必须保持 mapped；early raw blob 与平台阶段 blob 是两段不同映射，勿混指针域。

#### 4.2.2 phandle 引用

DT 允许节点用 `phandle`（property handle，属性句柄；或别名 `linux,phandle`）属性给自己分配一个 u32 句柄，其它节点再用 `clocks`、`interrupt-parent`、`gpios` 等属性以 `<u32 phandle>` 形式引用它，构成跨节点引用。例如 GIC 的 distributor/control 接口常由 `interrupt-parent = <&gic>` 风格引用 GIC 节点的 phandle。

本仓库**不**做通用 phandle 解析——`dev_node_find_by_compatible` 只按字符串匹配，不解析 `<>` 引用；GIC / PSCI 消费者各自直接按 compatible 找节点再读 `reg`，因此即便 DT 里写了 phandle 引用链，本仓库也不走那条路径。

### 4.3 compatible 匹配：raw 与树两条路径语义不同

**树路径**（`_dev_node_find_by_compatible`）：在 NUL 分隔的 compatible string list 上，对**每条**串做字符全等——仅当搜索串与当前条目同时走到 `\0` 才命中。因此查找 `arm,psci` **不会**命中单条的 `arm,psci-0.2`；常见 DT 会在 list 里同时列出 `arm,psci-0.2` 与 `arm,psci`，从而仍可命中。GIC 由 `gic_probe` 依次尝试多条 compatible（含 `arm,gic-400`），见 `27` §6.3。

**raw 路径**（`raw_get_prop_from_dtb`）：对属性值整体做 `strcmp_s(data, cmp_str, strlen(cmp_str))`——只与列表的**第一条**串全等，第一条不匹配就放弃，**不会**像树路径那样走到 `\0` 继续扫第二条。也就是说：`arm,pl011` 必须恰好是 compatible 列表的第一项，early UART 才能找到；pmm 的 `device_type` 值通常是单串，不受影响。QEMU virt 的 DT 恰好满足「第一项即规范名」，换厂商 DT 时值得先检查列表次序（限制记入 §10）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `modules/dtb/dtb.c` + `fdt.h` / `libfdt.h` | 裁剪 fdt walk、`fdt_check_header`、`raw_get_prop_from_dtb`（自 u-boot 移植） |
| `dev_tree.c` / `dev_tree.h` | 查找 API、`device_root` |
| `of.c` / `of.h` | `of_find_matching_node` + `struct of_device_id`：对匹配表逐项调 `dev_node_find_by_compatible`，返回节点并可带出命中的表项 |
| `property.c` | 类型表（建树过滤 / 打印用）、`property_read_*`（不查表） |
| `print_property.c` | 调试打印；`parse_print_dtb` 直接走 raw blob，注释自述「仅作解析示例」 |
| `arch/.../boot/boot_map.c` | MMU 前 raw 读 pl011 `reg` 并映射；把 DTB 整体拷贝到 2 MiB 对齐窗 |
| `arch/.../boot/start_arch.c` | `map_dtb`、`build_device_tree`、cmdline、调 PSCI/GIC/SPI 注册 |
| `arch/.../boot/smp.c` | 树：`device_type=cpu` + `reg` + `enable-method` |
| `arch/.../mm/pmm.c` | raw `memory` 区（MUL 模式 + 回调插 `m_regions`） |
| `arch/.../time/generic_time.c` | 树 + `of_find_matching_node`：arch_timer 的 `interrupts` PPI |

---

## 6. 流程

### 6.1 时间轴

```text
boot_map（MMU 前，全程物理地址）:
  boot_get_uart_info: raw 找 arm,pl011 的 reg → 按页映射 UART MMIO
  把 DTB 从 x0 原址整体拷贝到 ROUND_UP(kernel 尾 + UART, 2MiB) 处
  （拷贝量固定 2MiB；源/目的有重叠，按地址大小选前向/后向逐字节搬移）
  更新 setup_info->dtb_ptr 为拷贝目的址
prepare_arch:
  map_dtb: paddr ROUND_DOWN(dtb_ptr,2MiB) → 一条 L2 block（GLOBAL|HUGE|READ|VALID）
  boot_dtb_header_base_addr = dtb_ptr + blob 内偏移
  fdt_check_header；失败 → 返回错，cmain 处 kernel_panic
arch_init_pmm（见物理内存篇）:
  raw MUL 遍历 device_type=memory 的 reg → 插入 m_regions；再打印 reservation block
arch_start_platform:
  device_root = build_device_tree(per_cpu(kallocator,BSP_ID), NULL, dtb, 0, 0)
  找 chosen + 手写属性链表遍历取 bootargs → cmdline_ptr（直指 blob，不复制）
  psci_init()
  gic_probe()（失败 → 直接返回错误）
  gic.init_distributor()
  return irq_space_gic_spi_register(gic.nr_spi())
arch_start_smp（稍后）:
  dev_node_find_by_type 沿 dev_tree_get_next 链式遍历 device_type=cpu
  读 reg / enable-method（非 psci 打印跳过）；psci_func.cpu_on 拉起 AP
```

### 6.2 Early raw：为何手写 `arm,pl011` 查找

此时没有虚存堆，不能 `build_device_tree`。`boot_get_uart_info` 在物理地址空间里用 `raw_get_prop_from_dtb` 按 compatible 查找（比较类型串取自 `property_types[PROPERTY_TYPE_COMPATIBLE]`），再按 **#address-cells/#size-cells = 2** 的常见 virt 布局拆 `reg`：每 16 字节一对 addr/size，各拆 4 个换端 u32 拼成 64-bit 基址与长度——`reg` 含多对时**最后一对覆盖前面的**。找不到则 `boot_Error()`。字符数组逐字节赋值 `"arm,pl011"`，是为了避免早期依赖可写全局字符串的问题。

有一处顺序值得**记档**：代码里 `if (!uart_prop_ptr || *uart_len > MIDDLE_PAGE_SIZE) boot_Error();` 发生在**解析 `reg` 之前**，此时 `*uart_len` 还是调用方传入的初值 0，长度上限检查实际恒过；解析出的 `uart_len` 反而没有再被校验。当前 virt DT 的 PL011 只有一对 reg 且远小于 2 MiB，掩盖了这一点（见 §10）。

### 6.3 建树

`build_device_tree`：从 struct 偏移递归——节点名、属性（经 strings 块解析名）、子节点；节点与属性对象来自 **BSP `kallocator`**。保留哪个属性由 `get_property_type(name)` 查类型表后按 `if (p_type)` 决定：表的回退值是 `PROPERTY_TYPE_OTHER`（非零），所以实际**只有名字为空的属性被丢弃**，未识别的名字一律以 OTHER 保留（见 §4.2）。一处**记档**：节点分配 `m_alloc` 的返回值未做判空，堆耗尽时直接 NULL 解引用；建树只在 BSP 启动早期跑一次，BSP 堆几乎不可能枯竭，属理论缺陷。正常 virt 镜像可构建出完整树。

### 6.4 与硬件中断 / 电源的接点

- **GIC：** `gic_probe` 按表匹配多 compatible；从节点 `reg` 取 GICD/GICC 基址（细节见 `27`）。
- **PSCI：** 从 DT 读 method（SMC/HVC）与功能 ID（PSCI 篇）。
- **UART：** 运行期 `uart_open` 用 early 映好的高半窗；不再二次查找 DT。

---

## 7. 公开 API

本篇涉及的接口分布在：FDT / 设备树软件面——裁剪 `fdt_*` / `raw_get_prop_from_dtb`（`dtb.h`）、`build_device_tree` + `device_root` + `dev_node_find_*` / `property_read_*`（`dev_tree.h`；建树实现在 `start_arch.c`）。以头文件注释为准（见上列头文件；已与 `.c` 核对）。

**本篇不涉及：** `map_dtb` / `prepare_arch` / `arch_start_platform` 编排（见 `05`）；early UART map（见 `05`/`34`）；memmap `memory` 消费（见 `06`）；GIC / PSCI / SMP 对查找结果的使用（见 `27`/`39`/`28`）。

### 7.1 编排顺序（调用方须遵守）

| 阶段 | 工具 | 说明 |
|------|------|------|
| MMU-off / early | **`raw_get_prop_from_dtb`** | 无堆；物理/早期 VA 上的 blob |
| `prepare_arch` | **`fdt_check_header`** | 已 map 的 `boot_dtb_header_base_addr` |
| `arch_start_platform` | **`build_device_tree`** 得到 `device_root` | 须已有 BSP `kallocator` |
| 驱动 probe | `dev_node_find_*`，再 `dev_node_find_property`，然后 `property_read_*` | `node==NULL` 时从 `device_root` 起 |

**勿混** early blob 指针与树节点指针。

### 7.2 Raw FDT（`dtb.h`）

```c
int fdt_check_header(const void *fdt);
struct fdt_property *raw_get_prop_from_dtb(
        void *fdt, int offset, struct property_type *property_types_addr,
        const char *cmp_str, const char *cmp_type_str, u64 raw_get_mode,
        void (*f)(struct fdt_property *fdt_prop));
#define DTB_RAW_GET_PROP_MODE_SINGLE 1 /* 找到一条即返回 */
#define DTB_RAW_GET_PROP_MODE_MUL    0 /* 走到结尾，逐条回调 f */
/* + fdt_next_node / first_subnode / property_offset / fdt_string … */
```

| 接口 | 说明 |
|------|------|
| `fdt_check_header` | 完整校验顺序见 §4.1.1；失败则 `prepare_arch` 返回错，`cmain` 随之 panic。 |
| `raw_get_prop_from_dtb` | early UART（`arm,pl011`）与 pmm（`memory`）两个消费者。`offset` 传 0 从根节点起；`property_types_addr` 传 `property_types[]` 表；`cmp_type_str` 决定比对哪个属性名（UART 传 `"compatible"` 的类型项，pmm 传 `"device_type"` 的类型项），`cmp_str` 是目标串。查找逻辑是「按标识属性定位节点、取它的 `reg`」：当前节点的属性里找 `name==cmp_type_str` 且值==`cmp_str` 的项，命中后在该节点内按 `property_types[PROPERTY_TYPE_REG]` 的串找 `reg` 返回（**返回的永远是 `reg`**）；当前节点没有则在子节点递归。SINGLE 模式命中即返回 `reg` 指针；MUL 模式对每条 `reg` 调 `f`、最终返回 NULL。注意模式常量**反向**：SINGLE=1、MUL=0，内部按 `mode & SINGLE` 判停。 |

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

表驱动匹配另有 `of.h`（`modules/dtb/of.c` 全部家当就这一个函数）：

```c
struct of_device_id {
        const char *compatible;
        const void *data;   /* 驱动私有数据，可 NULL；表以全 0 哨兵结尾 */
};
struct device_node *
of_find_matching_node(struct device_node *from,
                      const struct of_device_id *matches,
                      const struct of_device_id **match_out);
```

它按 `matches` 表**逐项**调 `dev_node_find_by_compatible`，先命中的表项优先；`match_out` 可为 NULL，非空时回指命中的表项。当前唯一消费者是 `time/generic_time.c`（表 `arm,armv8-timer`）；GIC 自带 `gic_dt_drivers[]` 表并自己调 `dev_node_find_by_compatible`，**不走**这个 API。

| 接口 | 说明 |
|------|------|
| `build_device_tree` | 实现在 **`start_arch.c`**；名字/属性数据仍指向 FDT blob。 |
| `dev_node_find_by_compatible` | 对 list 中每条 NUL 串**全等**匹配（非前缀）；`arm,psci` 命中靠 list 含该条目（常与 `arm,psci-0.2` 并存）。 |
| GIC 消费者 | `gic_probe` 多 compatible（见 `27` §6.3）；不再写死单一串。 |

调试：`print_device_tree` / `parse_print_dtb`（非常用路径，约定从略）。

---

## 8. 多架构

仅 aarch64 主线消费。x86 用 ACPI / MADT 枚举板级资源，见 ACPI 篇（`35`）。riscv 若将来接 FDT，可复用 `modules/dtb`，但当前无对等平台编排。

---

## 9. 测试

`modules/test` 里没有 DTB / FDT 测例，覆盖全靠 QEMU `virt` 启动链上能经眼的串口输出：early 找不到 PL011 → `boot_Error()`；`fdt_check_header` 失败打 `check fdt header fail`、`cmain` 随之 panic；`arch_start_platform` 打 `cmdline:%s`（或 `no input cmdline`）；`psci_init` 打 `[ PSCI ] use smc call` / `use hvc call`，缺节点或缺 `method` 打 `cannot find psci node in dtb` 等；SMP 侧非 psci 的 `enable-method` 打 `[ SMP ] we only support psci smp setup method now`；arch_timer 解析失败直接 `kernel_panic`。树建得对不对、查找结果对不对，最终落在 SMP 拉起与 GIC / timer 初始化能否跑通上。失败路径基本碰不到：`fdt_check_header` 的六步校验（版本区间、`totalsize`、三个块越界）都得自造 blob 才触发。`print_device_tree` / `parse_print_dtb` 在 `start_arch.c` 里是注释掉的示例调用，不算覆盖。

---

## 10. 限制与后续

- compatible 匹配两条路径语义不同：**树路径**对 string list **逐条全等**（非前缀），过短搜索串不会「吃掉」长串；**raw 路径**只与列表**第一条**串全等（§4.3），`arm,pl011` 必须排第一。
- GIC：由 `gic_probe` 表匹配（v2 多串；v3 匹配后失败）；细节见 `27`。
- PCI host 节点未接入。
- early raw 与 `device_root` 两阶段指针域不同，勿混。
- early UART 假定 `#address-cells/#size-cells` 布局；换板可能要改解析。
- 不消费 memory reservation block（见 §4.1.2）；保留区与 `memory` 节点重叠时未做优先 reserve。
- 不做通用 phandle 解析（见 §4.2.2）；跨节点引用链不走本仓库路径。
- early UART 的 `reg` 长度上限检查发生在解析**之前**、用的是调用方初值 0，实际恒过；解析出的 `uart_len` 不再校验（§6.2，记档）。
- early UART 的 `reg` 若含多对 addr/size，**最后一对覆盖前面的**（§6.2）。
- boot 阶段 DTB 拷贝固定 `MIDDLE_PAGE_SIZE`（2 MiB）字节，不参照 `totalsize`：超限 blob 会被截短、不足则连带复制 blob 外的字节；`fdt_check_header` 要到 `prepare_arch` 才对拷贝后的副本执行（§6.1，记档）。
- `build_device_tree` 的节点内存分配不判空，OOM 时 NULL 解引用（§6.3，记档）。
- `psci_init` 缺功能 ID 属性只提前 `return`，`enable` 已置 true——上层无法据 `enable` 区分「全功能」与「半装」（§3，记档）。
- `gic_probe` 表匹配第一个 compatible 命中即定绑；bind 失败直接清零返回错误，**不会**继续试表中后续条目。
- arch_timer 解析 `interrupts` 失败、或非 BSP 核无缓存中断号，直接 `kernel_panic`（§3）。

---

## 11. 变更记录
- 2026-11-15：全文可读性通读（篇章架构 / 篇章引用 / 词句）。篇章引用三处：篇首全文件名 `05-平台启动-aarch64.md` 缩为「平台启动篇（`05`）」、物理内存篇补号（`06`）；§8「（上一篇）」改「见 ACPI 篇（`35`）」，与 `35` 篇回指「DTB 篇（`36`）」对仗；§4.1.4 尾「平台启动篇」补号。格式三处：§1.1 段末 `---` 前缺空行（setext 陷阱，会把上段渲染成二级标题）；§4.1.1 / §4.2.1 两个四级标题前缺空行；§4.1.4 的 `&lt;` HTML 实体还原为 `<`。词句两处：§4.1.4 末段开头「arm64 booting：DTB 8 字节对齐…」电报体且与上方 bullet 重复，改写为以「协议本身无上限」领起；§10「不会“吃掉”长串」的弯引号改用全篇统一的「」。§9 按实况扩写：`modules/test` 确无 DTB 测例（原句属实但过简），补齐 QEMU virt 启动实际可经眼的打印行——`check fdt header fail`、`cmdline:%s` / `no input cmdline`、`[ PSCI ] use smc call`、`[ SMP ] we only support psci smp setup method now`、arch_timer 失败 `kernel_panic`（均已对 `start_arch.c` / `psci.c` / `smp.c` / `generic_time.c` 核对）。另核 SMP 侧 `strcmp_s(method_str, "psci", 5)`、`reg` 配 4 / `enable-method` 配 14（`strlen+1`）、cmdline 手写遍历不走 `dev_node_find_property`、`psci_func.enable = true` 先于功能 ID 检查（`psci.c` L77），均与文一致。余沿 11-14 精读，未变。

- 2026-11-14：全文对照代码复核——§3 补齐 arch_timer/SMP/cmdline 消费者与读法，注明 `dev_node_find_property` 的 `n` 为 `strcmp_s` 比对上限（惯例 `strlen+1`）；§4.1.1 补 `fdt_check_header` 完整校验顺序、版本区间 v2–v17、u-boot 移植来源与 `no-jump-tables` 注记；§4.2 澄清 `property_read_*` 不查类型表、`struct property` 无类型字段、未识别属性按 OTHER 保留；§4.3 区分 raw（只比 compatible 第一条串）与树（逐条全等）两种匹配语义；§6.1 重写启动/平台时序（DTB 固定 2 MiB 拷贝与重叠方向、手写 bootargs 遍历、SPI 注册收尾）；§6.2 记档 `uart_len` 上限检查先于解析恒过、`reg` 多对取最后一对；§6.3 记档建树节点分配不判空；§7.2 给全 `raw_get_prop_from_dtb` 签名（定位标识节点、返回的永远是 `reg`，SINGLE=1/MUL=0 反向）；§7.3 补 `of_find_matching_node`/`struct of_device_id` 与消费者实况；§10 归并 8 条限制/记档；变更记录排序修正（三条 2026-10-05 条目原误排在初稿之后）。
- 2026-11-01：GIC 消费者改为 `gic_probe` 多 compatible（不再写死 `arm,cortex-a15-gic`）；交叉引用 `27`。
- 2026-10-05：修正 §4.1.4 两个 "2 MiB" 的混淆——arm64 booting 协议**无** "DTB < 2 MiB" 要求（协议只要求 DTB 位于低 128 MiB RAM 内、8 字节对齐）；"2 MiB" 实为 early map 的 block 粒度（aarch64 L2 block on 4K granule = 2 MiB），DTB 应 < 2 MiB 是该实现的一次覆盖约束，非协议上限。原 "通常 < 2 MiB" 改为 "位于低 128 MiB RAM 内"，并补两者关系说明。
- 2026-10-05：最终词句顺畅。
- 2026-10-05：硬件事实核对（对照 Devicetree Spec v0.4 / arm64 booting）——修正 structure block 示例根节点缺 END_NODE；修正 arm64 booting「D-cache 关闭」为「镜像范围 clean 到 PoC」（与现行 kernel docs 一致）；补 #address-cells/#size-cells 不继承与缺省值（2/1）、device_type 已 deprecated、/aliases 与 /reserved-memory 标准节点。
- 2026-10-05：任务 1/3/5 精读——§9「测例」→「测试用例」；§1「救急/给驱动 probe」→「应急/供驱动 probe」；§2「假问题」→「并不存在的问题」；§4.1.1「dood feed」→「d00d feed / dude feed」（修正 magic 读法）；§4.1.2「属已知简化」→「属于已知简化」；§4.2「未知名属性」→「未识别名称的属性」；§4.2.2 phandle 补全称「property handle，属性句柄」、「interrupts-parent」→规范名「interrupt-parent」、「跨树边引用」→「跨节点引用」；§4.3「搜」→「查找」、「写死过长串时」→「写死的串过长时」；§6.2「的坑」→「的问题」；§6.3「建出完整树」→「构建出完整树」；§6.4「搜 DT」→「查找 DT」；任务 1 补首次出现的缩写全称——DTB、GIC、PSCI、ECAM（篇首）、PL011（§3）、phandle（§4.2.2）。
- 2026-10-04：补硬件知识——§4.1 加 FDT 整体布局 ASCII 图（header/reservation/struct/strings 四块）、structure block token 表与节点序列化示例、memory reservation block 说明、aarch64 启动时 `x0` 传 DTB 指针约定；§4.2 加 `device_node` 树结构图（孩子/兄弟二叉链表）与 phandle 引用说明。结构无大改。语言润色——「假装」→「假设」、「长出 C 树」→「构建 C 树」、「扫」→「遍历」、「搜 compatible」→「按 compatible 查找」；§10 补 memory reservation block 与 phandle 两条限制交叉引用。
- 2026-09-27：语言润色——「真源 = … Doxygen」改为「以头文件注释为准」；one-shot→单次、bring-up→拉起、非热路径契约→非常用路径，约定从略；符号与技术事实未改。
- 2026-09-26：§7 全文审阅——`dtb.h`/`dev_tree.h` Doxygen；`build_device_tree` 声明进 `dev_tree.h`；划清 vs `05`/`27`/`39`。
- 2026-09-26：核对——changelog 误写「compatible 前缀」改为与正文/Doxygen 一致的**逐条全等**。
- 2026-09-25：语言轮——FDT 规范 / arm64 booting；raw vs 树因果；compatible **逐条全等**（非前缀）；GIC 写死串；明确无 ACPI、无 PCI 消费。
- 2026-08-29：整篇重做——`build_device_tree` 真位置；删 PCI 双源恐吓；消费者表。
- 2026-08-27：初稿。
