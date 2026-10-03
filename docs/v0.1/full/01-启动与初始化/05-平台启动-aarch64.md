# 平台启动-aarch64

v0.1 · 2026-09-25

本篇覆盖：`arch/aarch64/boot/boot.S`、`arch/aarch64/boot/boot_map.c`、`arch/aarch64/boot/start_arch.c`、`arch/aarch64/boot/smp.c`（`cpu_on` 的架构侧；整机编排见 `06-SMP与同步/28-SMP启动与处理器拓扑.md`）、`arch/aarch64/psci/psci.c`、`include/arch/aarch64/boot/arch_setup.h`。链接脚本侧见 `00-总览/01-构建与链接.md`；`cmain` 何时调用下面的钩子见 `02-启动流程总览.md`。设备树解析见 `09-平台模块/36-DTB与设备树-aarch64.md`。GIC 运行期见 `05-陷阱与中断/27-平台中断-aarch64-GIC.md`。PSCI 见 `09-平台模块/39-PSCI与处理器电源-aarch64.md`。

本篇同样分三块：`boot.S` + `boot_map.c` 如何交到 `cmain`；全机一次的平台钩子；每个核的 `arch_start_core`。

---

## 1. 概述

aarch64 走的是 **Linux arm64 Image 引导约定**：固件 / QEMU 把镜像装到板级入口（QEMU virt 上常见是物理 `0x40080000`），入口时 **x0 指向 DTB**，x1–x3 保留（当前多为 0，但仍要存进 `setup_info`，以免将来协议启用）。`boot.S` 在 MMU 关闭时做完内核区间的 2 MiB 恒等映射、UART 的 4 KiB Device 页、以及 DTB 的物理拷贝，再打开 EL1 的 MMU，把返回地址加上高半偏移跳上链接 VA，最后 `bl cmain`。

设备树整树、`bootargs`、PSCI 方法表、GIC 分发器都不在汇编里完成——它们要等 `kallocator` 就绪，由 `arch_start_platform` 全机做一次。每个核自己的 GIC CPU interface、时间和 syscall 固定 trap，则在 `arch_start_core`。用 PSCI `cpu_on` 拉 AP 见 SMP 篇。

跟 x86「历史原因盯住约 1 MiB」不同，ARM 板级入口地址可以随平台变化；core 用链接脚本的 `kernel_start_offset` 对齐当前目标（QEMU virt 的 `0x40080000`），并用 Image 头里的 `text_offset` / magic 让装载器认这是一份合法 arm64 内核镜像。EL2/EL3 降到 EL1 的完整路径还没做完（evolution **E8**）；QEMU virt 上进来时往往已经在 EL1，所以日常路径几乎不踩降级代码。

---

## 2. 目标与边界

**本篇要讲清：** Image 头与入口寄存器约定；`drop_to_el1` / `init_mmu` / `boot_map_pg_table` 各自做了什么；`setup_info` 各字段的填写时机；`prepare_arch` 如何补上 DTB 的内核虚地址；平台钩子与每核钩子在 GIC 上的分界（分发器一次，CPU interface 每核）。

**本篇不展开：** GICv3 ITS、SMMU；`cpu_on` 遍历循环（SMP 篇）；DTB 属性解码细则（DTB 篇）。

失败路径：早期找不到 `"arm,pl011"`，或内核起止不在同一个 1 GiB 窗口，`boot_map_pg_table` 进 `boot_Error`（死循环）；`prepare_arch` 里 `fdt_check_header` 失败则 `cmain` panic；`arch_start_platform` 即使没有 `chosen`/`bootargs` 也返回成功。

---

## 3. 分层与调用方

`boot.S` 加 `boot_map.c` 结束于 `cmain`。`start_arch.c` 的四个钩子由 `cmain` / `start_secondary_cpu` 按与 x86 相同的名字调用。GIC **分发器**是全机一份，所以只在 `arch_start_platform`；**CPU interface** 跟着核走，所以在 `arch_start_core`。若把 CPU interface 塞进平台钩子，AP 上来时自己的接口还没初始化，中断路径会缺半边。

链接方若换板级加载地址，需要同时改链接脚本的 `kernel_start_offset` 与 Image 头约定；`cmain` 编排本身不变。
---

## 4. 数据结构与不变量

### 4.1 Image 头与 `struct setup_info`

Image 头在 `.boot` 最前面（`include/arch/aarch64/boot/arch_setup.h` 的 `struct boot_header` 与汇编一致）：两条跳到 `bsp_entry` 的指令、`text_offset = 0x80000`、magic `0x644d5241`（`"ARM\x64"` 小端）。装载器认这个头，入口落在镜像开头。

`setup_info` 在 `.boot.data`，字段按偏移。前缀是本架构专用字段；尾部 `ap_boot_stack_ptr` / `cpu_id` 是跨 ISA **通用字段**（语义与谁写谁读见 `02-启动流程总览.md` §4.1.1，此处只给本 ISA 偏移与本侧填写路径）。

| 偏移 | 字段 | 谁写 | 谁读 / 用途 |
|------|------|------|-------------|
| `0x0` | `dtb_ptr` | `bsp_entry`（入口 x0，物理）；`boot_map_pg_table` 拷贝后可能改写 | `prepare_arch` / `arch_init_pmm` 等读 DTB |
| `0x8`–`0x18` | `res_x1`…`res_x3` | `bsp_entry`（入口 x1–x3，现多为 0） | 协议保留，先存下 |
| `0x20` | `map_end_virt_addr` | `boot_map_pg_table`；`prepare_arch` 的 `map_dtb` 再推进 | 早期映射高水位；PMM / 后续映射从这往后长 |
| `0x28` | `boot_uart_base_addr` | `boot_map_pg_table` | 早期 PL011 高半基址 |
| `0x30` | `boot_dtb_header_base_addr` | `prepare_arch`（`map_dtb`） | DTB 头的内核 VA；`arch_start_platform` 建设备树用 |
| `0x38` | `ap_boot_stack_ptr` | `arch_start_smp`（`cpu_on` 前） | 通用字段，见总览 §4.1.1 |
| `0x40` | `cpu_id` | `ap_entry`：把 PSCI `cpu_on` 传入的入口 x0 写入（C 侧不直接写本字段） | 通用字段，见总览 §4.1.1 |

注意：汇编在 MMU 关闭时用 `adr` 得到的是**物理**地址写表、写 `setup_info`；开 MMU 并跳上高半之后，同一符号的 `adr` 才是虚地址。`boot_dtb_header_base_addr` 进 `cmain` 时仍为 0，必须等 `prepare_arch` 补上。

### 4.2 链接、加载与早期物理布局

`script/link/aarch64_linker.ld`：

- `kernel_virt_offset = 0xffff800000000000`（与 x86 相同的高半窗口）
- `kernel_start_offset = 0x40080000`（对齐 QEMU virt 常见加载点）
- `ENTRY(_start)`，VMA 从二者之和起排

页表页在 `.boot.page`（L0–L3 各一页，启动前全零）；boot 栈在 `.boot_stack`，大小 `0x10000`。镜像占用的物理区间就是加载点起、到 `_end` 对应的物理地址；早期 DTB 还会再被拷到「内核末尾 + UART 窗口之后、按 2 MiB 对齐」的物理位置，避免和镜像、UART 映射打架。

### 4.3 早期页表（四层、4 KiB granule）

`boot_map_pg_table`（`boot_map.c`）全程用**物理地址**写表，因为此时 MMU 还关着：

1. 要求内核起止落在同一个 1 GiB（`>> 30` 相同），否则一张 L1 项盖不住，直接 `boot_Error`。
2. **L0 → L1 → L2**：对内核起始页建 table 描述符；L2 上对 `[kernel_start, kernel_end)` 按 2 MiB 写 **block** 描述符（不置 table 位，属性 Normal + AF），做恒等映射。
3. 在内核末尾再挂一张 **L3**（经 L2 的 table 项），按 4 KiB 映射 `"arm,pl011"` 的 `reg`，属性为 **Device**。UART 基址与长度来自 DTB 的 raw 属性遍历（兼容串手写在栈上的 `"arm,pl011"`，因为此时还不能依赖已映射的只读数据段约定）。找不到 PL011，或长度超过一个 middle page，进 `boot_Error`。
4. 再写一条高半 L0 项，指向同一张 L1，使 `KERNEL_PHY_TO_VIRT` 后的地址也能走同一套下层表。
5. 把 DTB 从原来的 `dtb_ptr` 起拷 **2 MiB** 到「UART 窗口按 2 MiB 对齐之后」的物理位置（按源/目的先后选择正向或反向字节拷，避免重叠破坏），并改写 `dtb_ptr`。此时还没有 DTB 的**内核虚地址**——那是 `prepare_arch` 的事。

`map_end_virt_addr` / `boot_uart_base_addr` 在这一步被写成高半虚地址，供后面的 C 代码接着往上长映射。

### 4.4 打开 MMU：`init_mmu`

`init_mmu` 在汇编里完成，对应 AArch64 开 MMU 的最小集合：

- 从 `ID_AA64MMFR0_EL1` 取 PARange，填入 `TCR_EL1.IPS`；若大于 40 位则**压到 40 位**（实现的有意上限）。
- **TTBR0_EL1 与 TTBR1_EL1 都指向同一张 `L0_table`**：低半（TTBR0）走恒等，高半（TTBR1）走链接 VA；与 x86「PML4 两项进同一张 L1」是同一思路。
- TCR：TTBR0 / TTBR1 两侧半区均为 inner shareable、写回分配、硬件更新 AF/DB（`HA`/`HD`）、4 KiB granule；若硬件宣称支持 16-bit ASID，则置 `TCR_EL1.AS`。
- `SCTLR_EL1` 置 `M`（MMU）、`C`（数据 cache）、`I`（指令 cache），`isb` 后把返回地址和 `lr` 都加上 `kernel_virt_offset`，`br` 到高半标签再 `ret`。此后 PC 落在链接 VA 上。

开 MMU 前后要注意屏障与 TLB；实现里在写 TTBR/TCR/SCTLR 处用了 `isb`。MAIR 由先前的 `mair_init` 填好，页表描述符里的 AttrIndx 才有意义。

### 4.5 异常级与栈

`drop_to_el1`：读 `CurrentEL`。已在 EL1 则直接返回。EL3 / EL2 分支目前是空壳（注释里留了 SPSR/ELR/`eret` 的位置），EL3 甚至会落到 EL2 路径——这就是 **E8** 未完成的部分。QEMU virt 上常见进来就是 EL1，所以日常能跑。

`sel_stack` 置 `SPSel` 选用 `SP_ELx`（当前 EL 自己的栈指针）。`set_stack` 把 SP 设为 `boot_stack + 0x10000`；开 MMU 前这是物理地址，开 MMU 并跳高半后再调一次时，`adr` 已是虚地址。手册侧含义见 §4.6。

### 4.6 与官方文档的对照（启动必知）

下面只摘与本实现直接相关的条款。ARM 侧以 *Arm Architecture Reference Manual for A-profile architecture*（常称 **ARM ARM**）为准；引导寄存器约定跟 Linux 的 arm64 booting 文档（本内核有意兼容该装载约定，但并非 Linux 内核）。

**Linux arm64 引导协议（装载约定）**

官方说明见：[Booting AArch64 Linux](https://docs.kernel.org/arch/arm64/booting.html)。与本实现对齐的要点：

- 主 CPU 跳进镜像**第一条指令**时：MMU off；**x0 = DTB 物理地址**；**x1–x3 = 0**（保留给将来）。本实现把四者都存进 `setup_info`，即使后三者当前为 0。
- DTB 须 8 字节对齐，且不超过 2 MiB；文档还要求它不要落在「必须以特殊属性映射的 2 MiB 区」里——本实现在早期把 DTB **拷到**内核末尾 + UART 窗口之后的 2 MiB 对齐物理位置，再在 `prepare_arch` 用一张 Normal 的 2 MiB 大页映射，就是为了满足「可 cacheable 映射、尺寸上限」这类约定。
- Image 头 magic `0x644d5241`、`text_offset` 等字段属于同一套 Image 格式约定；链接脚本的 `kernel_start_offset` 须与装载器实际放置的物理基址一致（QEMU virt 上常见 `0x40080000`）。

**异常级与栈（ARM ARM，Exception levels / SPSel）**

- AArch64 有 EL0…EL3。内核常态跑在 **EL1**；EL2 是 hypervisor，EL3 是 secure monitor。从更高 EL 降下来必须配置目标 EL 的 `SPSR_ELx` / `ELR_ELx` 再 `ERET`。本实现的 `drop_to_el1` 在 EL2/EL3 上尚未按手册填完这些寄存器（**E8**）。
- `CurrentEL` 的编码可区分当前异常级。`SPSel.SP`：0 表示使用 `SP_EL0`，1 表示使用当前 EL 的 `SP_ELx`。boot 置 1，避免早期还没准备好 EL0 栈时误用 `SP_EL0`。

**翻译体制与开 MMU（ARM ARM，VMSAv8-64）**

- EL1&0 翻译体制下，**TTBR0_EL1** 覆盖低半 VA，**TTBR1_EL1** 覆盖高半 VA；分界由 `TCR_EL1` 的 `T0SZ`/`T1SZ` 等决定。本实现把 **TTBR0_EL1 与 TTBR1_EL1** 都指向同一张 L0，再靠 L0 项分别挂低址恒等与高半窗口——与手册「两个 TTBR、两套根」的模型一致，只是软件选择让两棵树共享下层。
- 开 MMU 前通常要求：已写好 **MAIR_EL1**（内存属性）、**TCR_EL1**（粒度、可共享性、IPS、是否 16-bit ASID 等）、TTBR，再置 **SCTLR_EL1.M**；同时常开 `C`/`I` 以启用数据和指令 cache。写系统寄存器后需要合适的上下文同步（本实现用 `isb`）。
- 4 KiB granule 下四级描述符：L0/L1/L2 可以是 **table** 或 **block**；到 L3 才是 **page**。UART 用 Device 属性的 page、内核用 Normal 的 2 MiB block，对应的是手册里 AttrIndx → MAIR 的那条链，而不是「随便写个 P 位」。
- `ID_AA64MMFR0_EL1.PARange` 告诉软件硬件支持的物理地址宽度上限；`TCR_EL1.IPS` 必须设成不超过该能力。本实现额外把 IPS 压到不超过 40 位。
- 置了 `TCR_EL1.HA`（硬件更新 Access Flag）时，页表项需要按手册准备好 AF 等相关位；本早期映射在 Normal/Device 描述符里置了 AF 相关标志，与之对应。

**CPU 标识（ARM ARM，MPIDR_EL1）**

- `MPIDR_EL1` 提供 affinity 与 MT/U 等拓扑提示。本实现的 `arch_cpu_info` **只记录** MT/U，软件 `BSP_ID` 仍固定为 0，并不把 affinity 直接当 `cpu_id`——这是软件约定，不是手册强制。

**次级核（协议侧，细节见 SMP / PSCI 篇）**

- Linux arm64 booting 文档要求 DTB 为每个 cpu 节点提供 `enable-method`；常见路径是 PSCI。次级核入口时 x0–x3 在 Linux 约定里为 0；本实现的 `ap_entry` 则把传入的 x0 存进 `cpu_id`（由本内核的 `cpu_on` 路径约定），与「纯 Linux AP 入口寄存器」不完全相同，读代码时不要混为一谈。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `boot.S` | Image 头、`drop_to_el1`、`init_mmu`、BSS、BSP/AP 入口、`setup_info` |
| `boot_map.c` | MMU 关闭时的四级表、UART Device 映射、DTB 物理拷贝 |
| `start_arch.c` | `prepare_arch`、`arch_cpu_info`、`arch_start_platform`、`arch_start_core` |
| `boot/smp.c` | 遍历 DTB `cpu` 节点并 `cpu_on`；见 SMP 篇 |
| `psci.c` | `psci_init` 填的方法表；细节见 PSCI 篇 |

---

## 6. 流程

### 6.1 `bsp_entry` 到 `cmain`

1. **保存入口寄存器**：`adr setup_info`，把 x0–x3 存进去（x0 = DTB 物理指针）。
2. **`drop_to_el1`**：见 §4.5；多数 QEMU 路径此处直接返回。
3. **选栈并设栈**（物理地址）、`mair_init`、`prepare_page_table` → 调 `boot_map_pg_table`（§4.3）。
4. **`init_mmu`**：写 TCR/TTBR/SCTLR，跳上高半（§4.4）。
5. **`enable_fp`**：`CPACR_EL1` 打开 FP/SIMD 访问，避免后面 C 代码碰浮点即同步异常。
6. **清 BSS**：这里的比较是正确的，会从 `_bss_start` 清到 `_bss_end`（与 x86 那段「比较条件导致循环不跑」不同）。再设一次栈。
7. `x0 = setup_info`，`bl cmain`。若返回则进 `boot_Error` 死循环。

### 6.2 AP 入口（与 BSP 分叉）

`ap_entry` 不是上面那条链：把固件传入的 x0 存进 `setup_info.cpu_id`（偏移 0x40），自己降 EL、开 MMU（**不再**跑 `boot_map_pg_table`，页表已由 BSP 建好），栈先用 `ap_boot_stack_ptr` 减去高半偏移得到物理地址，开 MMU 后再改成虚地址，然后 `bl start_secondary_cpu`。谁写 `ap_boot_stack_ptr`、谁调 PSCI，见 SMP 篇。

### 6.3 `prepare_arch`：给 DTB 一张高半大页

此时 MMU 已开，可以用高半地址访问早期表。`map_dtb`：

- 虚地址取 `map_end_virt_addr` 按 2 MiB **向上**对齐；
- 物理地址取当前 `dtb_ptr` 按 2 MiB **向下**对齐；
- 在已有的 `L2_table` 上写一条 2 MiB huge 映射；
- `boot_dtb_header_base_addr = dtb_ptr + (vaddr - paddr)`，`map_end` 再往后推一个 2 MiB。

然后对这个虚地址做 `fdt_check_header`。这里**不**遍历整棵树，也**不**读 `bootargs`——那些要等 `arch_start_platform` 里用分配器建 `device_root`。

### 6.4 `arch_cpu_info` 与 `BSP_ID`

参数不用。读 `MPIDR_EL1`，只记录 MT（多线程）与 U（单核）两位到 `cpu_info`。**`BSP_ID` 固定写 0**，不用 affinity 当软件 id。后面 SMP 若约定「BSP 在 DTB 里的 `reg` 也是 0」，依赖的就是这个约定；和 x86「APIC ID 可能非 0」形成对照。

### 6.5 `arch_start_platform`（全机一次）

使用 `per_cpu(kallocator, BSP_ID)`：

1. 从已映射的 DTB 头递归 `build_device_tree`，得到全局 `device_root`。
2. 在名为 `chosen` 的节点里找 `bootargs`，赋给 `cmdline_ptr`；没有节点或没有属性只打印，**不失败**。
3. `psci_init()` 填 PSCI 调用方法。
4. `gic.probe()` + `gic.init_distributor()`——只初始化分发器。

CPU interface **不在这里**。函数返回成功；AP 不会再调用。

### 6.6 每个核：`arch_start_core(cpu_id)`

`cpu_number = cpu_id` 后 `isb`，然后：`init_interrupt` → `gic.init_cpu_interface` → `smp_ipi_init` → `rendezvos_time_init` → `register_fixed_trap(TRAP_CLASS_SYSCALL, …)`。

syscall helper 的细节：来自用户态（EL0 SVC）时，按陷入时的 SPSR 把用户的 DAIF.I 继承进当前 DAIF，调用可移植的 `syscall()`，返回前恢复；同 EL 的 SVC 不改内核的 DAIF 策略。这与 x86 在 syscall 窗口对 IF 的处理意图对齐，机制细节见系统调用入口篇。

本函数没有失败返回路径。线程和 port 不在这里建。

---

## 7. 公开 API

本篇拥有的 C 接口在 `include/arch/aarch64/boot/arch_setup.h`（实现 `arch/aarch64/boot/start_arch.c`）。说明与该头注释一致，并已与实现核对。`boot.S` / `boot_map.c` 仅由固件或汇编调用，无单独为文档新建的公开头（入口与早期映射约定见 §6.1–§6.2）。`arch_start_smp` / PSCI `cpu_on` 见 SMP / PSCI 篇；设备树查找 API 与 `build_device_tree` 细节见 `36-DTB与设备树-aarch64.md` §7。

### 7.1 编排顺序（`cmain` / AP，与源码一致）

| 调用方 | 本篇相关顺序 |
|--------|----------------|
| BSP `cmain` | … → **`prepare_arch`** → `phy_mm_init` → `arch_enable_percpu` → **`arch_cpu_info`** → `virt_mm_init` → **`arch_start_platform`** → **`arch_start_core(BSP_ID)`** → … |
| AP `start_secondary_cpu` | … → **`arch_start_core(cpu_id)`** → …（**不**再 `prepare_arch` / `arch_cpu_info` / `arch_start_platform`） |

GIC：**分发器**只在 `arch_start_platform`；**CPU interface** 在每个核的 `arch_start_core`。

### 7.2 `setup_info` 与 Image 头

```c
struct boot_header { /* Linux arm64 Image；见 boot.S */ … };

struct setup_info {
        u64 dtb_ptr;                   /* 0x0  物理；boot_map 可能改写 */
        u64 res_x1, res_x2, res_x3;    /* 0x8–0x18 固件 x1–x3 */
        u64 map_end_virt_addr;         /* 0x20 早期映射高水位 */
        u64 boot_uart_base_addr;       /* 0x28 PL011 */
        u64 boot_dtb_header_base_addr; /* 0x30 prepare_arch 填写的内核 VA */
        vaddr ap_boot_stack_ptr;       /* 0x38 通用，见总览 §4.1.1 */
        cpu_id_t cpu_id;               /* 0x40 通用，见总览 §4.1.1 */
};
```

字段偏移须与 `boot.S` 存取一致。与 x86 不同：无 Multiboot magic/info，入口约定是 Image + DTB（x0）。

### 7.3 可移植钩子（aarch64 实现语义）

```c
error_t prepare_arch(struct setup_info *arch_setup_info);
error_t arch_cpu_info(struct setup_info *arch_setup_info);
error_t arch_start_platform(struct setup_info *arch_setup_info);
error_t arch_start_core(cpu_id_t cpu_id);
```

| 接口 | 说明 |
|------|------|
| `prepare_arch` | 内部 `map_dtb`：按 `map_end_virt_addr` 向上 / `dtb_ptr` 向下对齐，写一条 2 MiB huge 映射，填 `boot_dtb_header_base_addr` 并推进 `map_end`；再 `fdt_check_header`。**不**建 `device_root`、**不**读 `bootargs`。成功 `0`；头校验失败 → `-E_RENDEZVOS`（`cmain` panic）。 |
| `arch_cpu_info` | 参数未用。读 `MPIDR_EL1` 填 `cpu_info` 的 MT/U；**`BSP_ID = 0`（固定）**。恒 `REND_SUCCESS`。 |
| `arch_start_platform` | **全机一次**，须在 `virt_mm_init` 之后。`build_device_tree` → 可选 `chosen`/`bootargs` → `psci_init` → `gic.probe` + `gic.init_distributor`。缺 cmdline **不**失败。当前实现恒返回 `REND_SUCCESS`。AP 不得调用。 |
| `arch_start_core` | **每核一次**。体内顺序：`cpu_number` → `isb` → `init_interrupt` → `gic.init_cpu_interface` → `smp_ipi_init` → `rendezvos_time_init` → `init_syscall`（`register_fixed_trap(TRAP_CLASS_SYSCALL, …)`）。恒返回 `0`。不建线程/port。 |

### 7.4 本篇源码但不列入本 §7 的符号

| 符号 | 归属 |
|------|------|
| `bsp_entry` / `ap_entry` / `drop_to_el1` / `init_mmu`（`boot.S`） | 固件 / PSCI 跳入；无 C 声明头 |
| `boot_map_pg_table` 等（`boot_map.c`） | 仅汇编早期路径调用 |
| `build_device_tree` / `device_root` 查找 | DTB 篇 §7（实现落在本篇 `start_arch.c`） |
| `arch_start_smp`（`smp.h`） | SMP 篇 §7 |
| `psci_init` | PSCI 篇 §7 |

---

## 8. 多架构

仅 aarch64。与 x86 对照：引导是 Image + DTB（x0），没有低 1 MiB 的 16 位跳板；`BSP_ID` 固定 0；平台一次初始化是设备树、PSCI 与 GIC 分发器，不是 ACPI + PCI。交给 `cmain` 的钩子名字相同。riscv64 仅占位，不在本篇。

---

## 9. 测试

在 `core/` 目录内：

```bash
make ARCH=aarch64 config && make all && make run
```

无单独的 `boot.S` 单测。本篇按源码整理，本轮未单独复测。

---

## 10. 限制与后续

- `drop_to_el1` 在 EL2/EL3 未完成（evolution **E8**）。
- 早期 UART 只认兼容串 `"arm,pl011"`；其它控制台需后续扩展。
- 内核镜像必须落在同一个 1 GiB 窗口内，否则早期 L1 一张表盖不住。
- `arch_start_platform` 不因缺少 `bootargs` 失败。
- TCR IPS 有意限制到不超过 40 位物理地址宽度。

---

## 11. 变更记录

- 2026-10-03：§4.1 `setup_info` 表补「谁读」、`boot_map` 改写 `dtb_ptr`、`cpu_id` 经 PSCI context；通用字段语义链总览 §4.1.1。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：§7 全文审阅：`arch_setup.h` 补注释；写清钩子编排、GIC 分界与返回约定；去掉 `.c` 里过时/与 x86 串台的旧 brief。
- 2026-09-25：增补 §4.6（ARM ARM 翻译/EL/SPSel，以及 Linux arm64 booting 装载约定引用）。
- 2026-09-25：按操作计划补全链接→加载→早期布局→页表→开 MMU 链条；写清 Image/DTB 动机、EL 降级现状与 GIC 分界；流程节加详。
- 2026-09-20：三块分开（汇编 / 平台一次 / 每核），放回 full 十一节。
- 2026-08-29：曾按源码重做，并补过早期映射。
