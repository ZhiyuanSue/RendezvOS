# 平台启动-aarch64

v0.1 · 2026-10-03

本篇对应的源码是：`arch/aarch64/boot/boot.S`、`boot_map.c`、`start_arch.c`、`boot/smp.c`（只含按设备树调用 `cpu_on` 的架构侧），以及 `arch/aarch64/psci/psci.c`、`include/arch/aarch64/boot/arch_setup.h`。

下文把最先起来的核叫 **BSP**，其余核叫 **AP**。整机何时调用 `prepare_arch` 等四个架构函数，见启动总览；链接脚本见构建篇。设备树整棵树怎么解析，见 DTB 篇。运行期中断见 GIC 篇。谁调用 `cpu_on`、第三个参数传什么，见 SMP 篇和 PSCI 篇——本篇只解释入口寄存器和页表由 BSP 先建成什么样。

建议的读法：若对「异常级、TTBR0/TTBR1、设备树、PSCI」不熟，先看 §4.6，再回头看布局和流程；§6 按时间往下走，不必在流程里回头翻手册。

本篇要讲清三件事：汇编和早期映射怎样把机器交到 `cmain`；全机只做一次的平台初始化；每个核都要跑的 `arch_start_core`。

---

## 1. 概述

固件或 QEMU 按照 **Linux arm64 Image 约定** 把内核镜像放到板级入口（QEMU virt 上常见是物理 `0x40080000`），并在 **MMU 关闭** 时跳进镜像第一条指令。入口时 **x0 指向设备树（DTB）的物理地址**，x1–x3 协议保留（当前多为 0，但仍写入 `setup_info`，以免以后协议启用）。链接脚本却把内核符号挂在高半虚拟地址 `0xffff800040080000` 上，所以汇编必须先让「低物理地址」和「高半虚地址」看见同一份镜像，再打开 EL1 的 MMU，把返回地址加上高半偏移，落到链接地址，`bl cmain`。

跟 x86 盯住大约 1 MiB 不同，ARM 的加载点可以随板子变。本仓库用链接脚本的 `kernel_start_offset` 对齐当前目标（virt 上就是 `0x40080000`），并用 Image 头里的 `text_offset` 和 magic，让装载器认出这是一份合法的 arm64 内核镜像。

设备树整树、命令行、PSCI 方法表、GIC 分发器故意不塞进这段汇编：它们要么要在 MMU 打开之后才能用高半地址安心访问，要么需要内核堆。`prepare_arch` 只给拷好的 DTB 补一张高半大页并校验头；`phy_mm_init` 再按设备树里的内存去建可用区；有了堆之后 `arch_start_platform` 全机做一次设备树、PSCI 和 GIC 分发器；每个核自己的 GIC CPU 接口、时间和系统调用入口放在 `arch_start_core`。

从 EL2/EL3 降到 EL1 的完整路径还没做完（evolution **E8**）。QEMU virt 进来时往往已经在 EL1，日常几乎不踩那段空壳。

装载约定对齐 [Linux arm64 booting](https://docs.kernel.org/arch/arm64/booting.html)；本仓库并不是 Linux 内核，只是入口寄存器和镜像头跟那份文档一致。

---

## 2. 目标与边界

要讲清的是：Image 头和入口寄存器约定；`drop_to_el1` / `boot_map_pg_table` / `init_mmu` 各自负责哪一段；`setup_info` 各字段谁写谁读；BSP 上的 `prepare_arch` / `arch_cpu_info` / `arch_start_platform`，以及每核的 `arch_start_core`，在 GIC 上怎样分工（分发器全机一次，CPU 接口每核一次）。

不在这里展开 GICv3 ITS、SMMU、`cpu_on` 怎么遍历多颗核，以及设备树属性怎么逐项解码。

---

## 3. 分层与调用方

`boot.S` 加 `boot_map.c` 只对装载器负责，成功就进 `cmain`，没有别的 C 调用方。进了 C 之后，`start_arch.c` 里那四个与 x86_64 同名的函数由 `cmain`（AP 上则是 `start_secondary_cpu`）按固定顺序调用，这样总览那一层不必按 ISA 分叉。

GIC 分成全机一份的**分发器**和跟着核走的 **CPU 接口**。分发器只在 `arch_start_platform` 里初始化；CPU 接口放在 `arch_start_core`。

以后若换板级加载地址，改的是链接脚本的 `kernel_start_offset` 和 Image 头约定，不是 `cmain` 的编排。

---

## 4. 数据结构与不变量

硬件上的名词（异常级、两根 TTBR、设备树、PSCI、`svc`）集中在 **§4.6**。下面 4.1–4.5 讲本实现怎么摆数据和怎么走步骤；读到不明的缩写可以先翻 4.6（当然，如果有条件，强烈建议读一遍 ARM A-profile 手册，并长期备在手边）。

### 4.1 Image 头与 `struct setup_info`（aarch64）

镜像最前面是 Linux 约定的 **Image 头**，定义在 `include/arch/aarch64/boot/arch_setup.h` 的 `struct boot_header`，和 `boot.S` 里 `.boot` 段开头必须一致：两条跳到 `bsp_entry` 的指令、`text_offset = 0x80000`、magic `0x644d5241`（小端 `"ARM\x64"`）。装载器认这个头，入口落在镜像开头。

`setup_info` 是汇编留给后面整条 C 启动链的一块状态，同样在这个头文件里定义，汇编 `.boot.data` 有同名符号，C 头和汇编的字段顺序、大小必须对得上。前半是 aarch64 专用字段；末尾 `ap_boot_stack_ptr` 和 `cpu_id` 与别的 ISA 对齐，语义见启动总览 §4.1.1，这里只给本架构的偏移。

| 偏移 | 字段 | 谁写 | 谁读 / 用途 |
|------|------|------|-------------|
| `0x0` | `dtb_ptr` | `bsp_entry`（入口 x0，物理）；`boot_map_pg_table` 拷贝后改写为新物理位置 | `prepare_arch` / `arch_init_pmm` 等读 DTB |
| `0x8`–`0x18` | `res_x1`…`res_x3` | `bsp_entry`（入口 x1–x3，现多为 0） | 协议保留，先存下 |
| `0x20` | `map_end_virt_addr` | `boot_map_pg_table`；`prepare_arch` 的 `map_dtb` 再推进 | 早期映射末尾；后面物理/虚拟内存从这往后长 |
| `0x28` | `boot_uart_base_addr` | `boot_map_pg_table` | 早期 PL011 的高半基址 |
| `0x30` | `boot_dtb_header_base_addr` | `prepare_arch`（`map_dtb`） | DTB 头的内核虚地址；`arch_start_platform` 建设备树用 |
| `0x38` | `ap_boot_stack_ptr` | `arch_start_smp`（`cpu_on` 前） | 见总览 §4.1.1 |
| `0x40` | `cpu_id` | `ap_entry`：把 PSCI `cpu_on` 传入的入口 x0 写入（C 侧不直接写本字段） | 见总览 §4.1.1 |

汇编在 MMU 关闭时用 `adr` 拿到的是**物理**地址，用来写页表、写 `setup_info`；打开 MMU 并跳上高半之后，同一符号的 `adr` 才是虚地址。`boot_dtb_header_base_addr` 进 `cmain` 时仍是 0，必须等 `prepare_arch` 补上——早期只把 DTB **拷到**内核后面一块物理位置，还没有给它高半映射。

### 4.2 链接、加载与早期物理布局

链接脚本（`script/link/aarch64_linker.ld`）约定：高半窗口 `kernel_virt_offset = 0xffff800000000000`（与 x86_64 同一扇窗），加载偏移 `kernel_start_offset = 0x40080000`，所以符号从虚地址 `0xffff800040080000` 往上排。于是早期映射可以对齐成一句：

**高半虚地址 = 物理地址 + `kernel_virt_offset`**。加载点就是 `0xffff800040080000` 对着 `0x40080000`。板级入口可以变，但链接脚本的 `kernel_start_offset` 必须和装载器实际放下的物理基址一致。

页表页在 `.boot.page`（L0–L3 各一页，启动前全零）；boot 栈在 `.boot_stack`，大小 `0x10000`。镜像物理区间是加载点到 `_end` 对应的物理地址。`boot_map_pg_table` 还会在内核按 2 MiB 对齐的上界之后，挂 UART 的 4 KiB Device 页，并把 DTB 拷到再往后一张 2 MiB 对齐的物理位置——固件原来的 DTB 在 x0，不一定贴着内核。

```text
物理地址（QEMU virt 常见；DRAM 从 0x40000000 起，本内核不写死）

  0x40000000     DRAM 起点（示意）
  0x40080000     ← 镜像加载点 = kernel_start_offset
                 |  Image 头、.text、.init、.rodata
                 |  .data：setup_info、其余 data
                 |  .boot.page、.boot_stack
                 |  .bss / .percpu
                 |_end
                 ROUND_UP(_end, 2 MiB) = kernel_end_page
                 |  L3 窗口：虚地址从 kernel_end_page 起，物理指向 PL011 MMIO
                 |           （设备地址，不在内核镜像里；virt 上常见 0x9000000）
                 ROUND_UP(kernel_end_page + uart_len, 2 MiB)
                 └─ 拷过来的 DTB（最多 2 MiB）  ← 改写 setup_info.dtb_ptr
                    （此时还没有 DTB 的高半虚地址，prepare_arch 再映一张大页）

早期虚拟窗口（同一套 L1 以下；怎么挂见 §4.3 / §4.4）

  低址恒等  VA  0x40080000           ──┐
                                      ├── 同一 PA 0x40080000 起的镜像
  高半链接  VA  0xffff800040080000   ──┘
  UART      VA  KERNEL_PHY_TO_VIRT(kernel_end_page)  → Device 物理地址
```

### 4.3 早期页表（四级，4 KiB granule）

`boot_map_pg_table`（`boot_map.c`）全程用**物理地址**写表，因为此时 MMU 还关着。为什么高半要再写一条 L0、TTBR0/TTBR1 怎样分工，§4.6 有一段专讲；这里只写这张表填了什么。

先要求内核起止落在同一个 1 GiB（地址右移 30 位相同）。否则一张 L1 项盖不住，直接 `boot_Error`。然后对内核起始页建 L0 → L1 → L2 的 table 描述符；L2 上对 `[kernel_start, kernel_end)` 按 2 MiB 写 **block**（不再指向更下一层，属性 Normal，并置 Access Flag），这就是恒等映射。

内核末尾再经 L2 的一条 table 项挂一张 **L3**，按 4 KiB 映射 `"arm,pl011"` 的 `reg`，属性为 **Device**。UART 基址和长度来自 DTB 的原始属性遍历：兼容串手写在栈上（`"arm,pl011"`），因为此时还不能依赖已经映射好的只读数据段约定。找不到 PL011，或长度超过一个 middle page，进 `boot_Error`。

再写一条高半 L0 项，指向同一张 L1，使加上 `KERNEL_PHY_TO_VIRT` 之后的地址也能走同一套下层表。低址窗口和高半窗口因此看见同一份物理内核。

最后把 DTB 从原来的 `dtb_ptr` 起拷 **2 MiB** 到「UART 窗口按 2 MiB 对齐之后」的物理位置（按源和目的的先后选正向或反向按字节拷，避免重叠破坏），并改写 `dtb_ptr`。此时还没有 DTB 的**内核虚地址**——那是 `prepare_arch` 的事。`map_end_virt_addr` 和 `boot_uart_base_addr` 在这一步被写成高半虚地址，供后面的 C 接着往上长映射。

### 4.4 打开 MMU：`init_mmu`

真正让 MMU 走起来的顺序在汇编里：先前 `mair_init` 填好 **MAIR**（页表里的 AttrIndx 才有意义），再写 **TCR / TTBR0 / TTBR1**，最后置 **SCTLR_EL1.M**，`isb` 之后把返回地址和 `lr` 都加上 `kernel_virt_offset`，`br` 到高半再 `ret`。此后程序计数器落在链接虚地址上。这些寄存器各自管什么见 §4.6。

**TTBR0_EL1 与 TTBR1_EL1 都指向同一张 `L0_table`**：低半（TTBR0）走恒等映射（虚地址等于物理地址），高半（TTBR1）走链接虚地址；与 x86「页表第 0 项和第 256 项进同一张下一层」是同一思路。`T0SZ` / `T1SZ` 都写成 `0x10`（64−16=48 位虚地址），对得上从 `0xffff8000…` 开始的那一段高半虚地址。

从 `ID_AA64MMFR0_EL1` 取 PARange 填 `TCR_EL1.IPS`；若大于 40 位则**压到 40 位**（实现的有意上限）。TTBR0 侧 4 KiB granule 的位域复位值已是 0，汇编不再或进去；TTBR1 侧要显式置 `TG1=4KB`。

同一段 TCR / SCTLR 里还会多开几位。它们不是「打开 MMU」的最低要求，但后面马上要用，与其再改一次，不如现在一起打开：

| 多开的位 | 若不开 | 本内核为什么现在就开 |
|----------|--------|----------------------|
| `SH0` / `SH1` Inner Shareable | 页表走表在多核之间不一定按可共享缓存一致 | 单核也能开；SMP 时必须是这一档 |
| `ORGN*` / `IRGN*` 写回可分配 | 每次页表 walk 都打到内存 | 走表本身走 cacheable |
| `TCR_EL1.HA` | 第一次访问可能要软件处理 Access Flag 故障 | 早期描述符已经写了 AF；硬件自己置更省事 |
| `TCR_EL1.HD` | dirty 要软件更新 | 和 HA 配套，少一次写故障走软件 |
| `TCR_EL1.AS` | 只支持 8-bit ASID | 硬件宣称 16-bit 才置；后面 ASID 分配器要用更宽的标识（见 ASID 篇） |
| `SCTLR_EL1.C` | 数据仍非 cacheable | 只开 M、不开 C，等于翻译亮着但数据不过 cache |
| `SCTLR_EL1.I` | 指令不过 I-cache | 跳上高半之后取指才走指令 cache |

写 TTBR/TCR/SCTLR 处用了 `isb`，让后续取指看到新翻译。

### 4.5 异常级、栈与 `BSP_ID`

`drop_to_el1` 读 `CurrentEL`。已经在 EL1 就直接返回。EL2 会落到还没填 `SPSR`/`ELR` 的空壳，然后 `eret`——这就是 **E8** 未完成的部分。EL3 的 TODO 是空的，比较下来当前又不是 EL2，于是跳到「当作 EL1」直接返回，等于没降级。QEMU virt 上常见进来就是 EL1，所以日常能跑。各档异常级是什么见 §4.6。

`sel_stack` 置 `SPSel`，选用当前异常级自己的栈指针，避免早期误用还没准备好的 EL0 栈。`set_stack` 把 SP 设为 `boot_stack + 0x10000`：开 MMU 前这是物理地址，开 MMU 并跳高半后再调一次时，`adr` 已是虚地址。

C 代码起来之后，要给这颗启动核一个软件编号，并启用它的 per-CPU 区（用 `TPIDR_EL1` 指向那一块，见 §4.6）。`arch_cpu_info` **把 `BSP_ID` 固定写成 0**，并不把 `MPIDR_EL1` 的亲和性当槽号；`cmain` 先做这一步，再 `arch_enable_percpu(BSP_ID)`。以后若 DTB 里 BSP 的 `reg` 不是 0，这个约定就要一起改，见启动总览 §10。

### 4.6 启动用到的硬件基础

下面按「读本篇会碰到的顺序」讲，不必一次记完。GIC 寄存器怎么配、设备树属性怎么解码、PSCI 功能号怎么调，分别在 GIC 篇、DTB 篇、PSCI 篇。手册以 ARM A-profile 为准。

#### 异常级：内核在 EL1

同一颗 CPU 有四档**异常级**：**EL0** 是以后给非内核代码用的最低档，**EL1** 是本内核常态，**EL2** 是 hypervisor，**EL3** 是安全监控。从更高档降下来要填目标档的 `SPSR` / `ELR` 再 `ERET`；本实现的 `drop_to_el1` 在 EL2/EL3 上还是空壳，见 §4.5。`CurrentEL` 能读出当前在哪一档。

#### 两套栈指针

每个异常级可以选：用 **EL0 那根栈**（`SP_EL0`），还是用 **当前档自己的栈**（`SP_ELx`），由 `SPSel` 决定。boot 选后者。`set_stack` 只是把选中的那根 SP 指到 boot 栈顶。

#### 系统寄存器和 `isb`

ARM 把模式、页表、cache 配在一堆名为 `*_EL1` 的**系统寄存器**里（`mrs` / `msr`），角色接近 x86 的 MSR 加控制寄存器。写完之后通常要一条 **`isb`**（指令同步屏障），后面的取指才按新配置走。本篇开 MMU、记下 `cpu_number` 后面都能看到它。

#### MMU、TTBR、TCR、SCTLR、MAIR

分页关闭时，CPU 看见的就是物理地址；打开之后先走虚地址再翻译。EL1 用两根页表根：**TTBR0_EL1** 管低半虚地址，**TTBR1_EL1** 管高半，分界由 **TCR_EL1** 的 `T0SZ`/`T1SZ` 等决定。本实现两根都指向同一张 L0，再靠 L0 两项分别挂恒等和高半。`T0SZ`/`T1SZ` 写成 `0x10` 表示 48 位虚地址，对得上 `0xffff8000…`。**SCTLR_EL1.M** 才是「MMU 开」。常同时开 **C**（数据 cache）和 **I**（指令 cache）。**MAIR_EL1** 是一张内存属性表，页表项里的 AttrIndx 查这张表：内核用 Normal（可 cache），UART 用 Device（外设，不能当普通内存 cache）。

页表粒度本实现是 **4 KiB granule**、四级。L0/L1/L2 可以是「下一张表」或「一块大页（block）」；到 L3 才是 4 KiB page。内核镜像用 2 MiB block；PL011 用 Device 的 4 KiB page。`ID_AA64MMFR0_EL1` 能读出硬件支持的物理地址宽度等，TCR 的 IPS 不能超过它；本实现还把 IPS **压到不超过 40 位**。TCR 里其它多开的位见 §4.4。

#### 物理 / 虚拟，以及 `adr` 的坑

**高半虚地址 = 物理地址 + `kernel_virt_offset`**。装载点随板子变，链接脚本必须和固件放下的物理基址一致，见 §4.2。汇编 `adr` 取的是「当前 PC 能看见的地址」：MMU 关着时是物理，跳上高半之后是虚地址。所以设栈、写 `setup_info` 往往要做两次，或开 MMU 前用物理、之后再改。

#### 设备树（DTB）

固件用一块扁平**设备树**（FDT/DTB）描述内存、UART、CPU、中断控制器等。入口 **x0 = DTB 物理地址**（Linux arm64 约定；x1–x3 保留）。本篇早期只做三件事：找到 PL011、把 DTB 拷到内核后面一块 2 MiB 对齐的物理位置、再在 `prepare_arch` 给它一张可 cache 的高半大页。整棵树的解析要等堆起来，见 DTB 篇。协议还要求 DTB 8 字节对齐、不超过 2 MiB。

#### GIC：一份分发器，每核一个 CPU 接口

通用中断控制器分成：全机一份 **Distributor**（路由、使能源），以及每个核自己的 **CPU interface**（本核收哪几个、应答/EOI）。所以 `arch_start_platform` 只初始化分发器；`arch_start_core` 再初始化本核接口。细节见 GIC 篇。

#### PSCI：用固件接口叫醒 AP

ARM 二次核一般不走 x86 那种「往低 1 MiB 发 SIPI」。常见是固件提供的 **PSCI**（电源和 CPU 控制，经 `smc`/`hvc`）。`cpu_on` 指定目标核和入口地址。Linux 约定 AP 入口 x0–x3 为 0；本内核自己的路径把传入的 x0 当作软件 `cpu_id` 存进 `setup_info`，不要和纯 Linux AP 入口混读。细节见 PSCI / SMP 篇。

#### `MPIDR` 不是软件 cpu_id

`MPIDR_EL1` 是硬件亲和性（哪颗核、是否多线程）。`arch_cpu_info` **只记下** MT/U 两位，软件 **`BSP_ID` 固定为 0**，并不把 affinity 当 percpu 下标。这是软件约定。

#### 本核数据：`TPIDR_EL1`

aarch64 没有 GS。本仓库把 EL1 的 **`TPIDR_EL1`** 当成 per-CPU 区基址（`arch_enable_percpu` 写入）。访问约定见 per-CPU 篇。

#### `svc`：异常进核

低特权用 **`svc` 进 EL1**，CPU 从 **VBAR** 指向的表取处理入口，再进本仓库的 trap 分发。和 x86 `syscall` 直跳一条 MSR 里的地址不同。本篇 `arch_start_core` 只登记「系统调用」那条固定 trap；入口怎么保存、恢复 IRQ 状态，见系统调用入口篇。

#### 浮点 / SIMD

默认 EL1 去碰浮点/SIMD 会同步异常。`enable_fp` 改 `CPACR_EL1`，允许访问，后面 C 代码才能用。

#### Image 引导约定

以上是 CPU 自己的异常级和地址翻译。镜像怎么被放进内存、入口寄存器里是什么，走的是 [Booting AArch64 Linux](https://docs.kernel.org/arch/arm64/booting.html) 那套约定：主 CPU 跳进镜像第一条指令时 MMU 关着；x0 是 DTB 物理地址；头里的 magic 和 `text_offset` 让装载器认出这是 arm64 Image。

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `boot.S` | Image 头、`drop_to_el1`、`init_mmu`、清 BSS、BSP/AP 入口、`setup_info` |
| `boot_map.c` | MMU 关闭时的四级表、UART Device 映射、DTB 物理拷贝 |
| `start_arch.c` | 四个与 ISA 同名的 C 钩子 |
| `boot/smp.c` | 遍历 DTB `cpu` 节点并 `cpu_on`；编排见 SMP 篇 |
| `psci.c` | `psci_init` 填的方法表；细节见 PSCI 篇 |

---

## 6. 流程

下面按时间走：装载器先看到 Image 头，再进入 BSP 汇编；AP 只在这里点到 `ap_entry` 为止。进 C 之后是全机一次的架构函数，最后每核补自己的运行环境。总览里夹在中间的物理/虚拟内存初始化，本篇不重复。

### 6.1 Image 头：为什么长成这样

镜像一开头就是两条跳到 `bsp_entry` 的指令，后面跟着 `text_offset`、占位的 image size/flags，以及 magic。装载器按这份头认出 arm64 Image，并从第一条指令进入。真正干活的是 `bsp_entry`，头本身不执行逻辑。

### 6.2 `bsp_entry`：从 MMU 关闭走到 `cmain`

进来时 MMU 关着。x0 是 DTB 物理指针，x1–x3 是保留值，马上写入 `setup_info` 开头。然后 `drop_to_el1`（多数 QEMU 路径直接返回），选栈并按物理地址设栈，填 MAIR，再调 `boot_map_pg_table` 建早期表（§4.3）。

表填好之后按 §4.4 打开 MMU 并跳上高半。接着 `enable_fp`，让后面的 C 去碰浮点不会立刻同步异常。栈开在 boot 段里，所以再从 `_bss_start` 清到 `_bss_end`，并把栈改成高半的 boot 栈顶。最后把 `setup_info` 的地址放进调用约定的第一个参数，进入 `cmain`。如果 `cmain` 居然返回，就进 `boot_Error` 死循环。

### 6.3 AP 入口（本篇只到 `ap_entry`）

`ap_entry` 不是上面那条链：它把固件传入的 x0 存进 `setup_info.cpu_id`，自己降 EL、开 MMU（**不再**跑 `boot_map_pg_table`，页表已由 BSP 建好），栈先用 `ap_boot_stack_ptr` 减去高半偏移得到物理地址，开 MMU 后再改成虚地址，然后进入 `start_secondary_cpu`。谁写入栈顶和软件编号、谁调 PSCI，见 SMP 篇。

### 6.4 进 C 之后、全机只做一次的架构函数

`cmain` 会按总览里的顺序调用三个与 BSP/全机相关的架构函数，中间还夹着物理内存和虚拟内存初始化。这里只说这三个自己干什么。

`prepare_arch` 此时 MMU 已开，可以用高半地址访问早期表。内部的 `map_dtb` 把虚地址取 `map_end_virt_addr` 按 2 MiB **向上**对齐，物理地址取当前 `dtb_ptr` 按 2 MiB **向下**对齐，在已有的 `L2_table` 上写一条 2 MiB huge 映射，令 `boot_dtb_header_base_addr = dtb_ptr + (vaddr - paddr)`，再把 `map_end` 往后推一个 2 MiB。然后对这个虚地址做 `fdt_check_header`。它不遍历整棵树，也不读 `bootargs`——那些要等堆起来之后，由 `arch_start_platform` 去做。

`arch_cpu_info` 不用它的参数。它读 `MPIDR_EL1`，只把多线程（MT）和是否单核（U）记进全局 `cpu_info`，再把 `BSP_ID` 固定写成 0。不把硬件亲和性当软件编号。

`arch_start_platform` 必须在虚拟内存和堆起来之后，并用 BSP 的堆。它从已映射的 DTB 头递归建成全局 `device_root`；在名为 `chosen` 的节点里找 `bootargs` 赋给 `cmdline_ptr`（没有节点或没有属性只打印，**不失败**）；再 `psci_init` 填 PSCI 调用方法；最后 `gic.probe()` 加 `gic.init_distributor()`，只初始化分发器。CPU 接口不在这里。当前实现总是返回成功。AP 不会进这个函数。

### 6.5 每个核：`arch_start_core(cpu_id)`

BSP 走到这里时由 `cmain` 调用，AP 则由 `start_secondary_cpu` 调用。汇编已经让这颗核跑在 EL1、MMU 开着并能执行 C；本函数补的是**这一核自己还缺的运行环境**。下面用到的 GIC 接口、`svc`、`TPIDR_EL1` 等，§4.6 有说明。这里仍然不创建线程或 port。

先记下本核编号并 `isb`。然后装本核中断入口，并初始化**本核**的 GIC CPU 接口（分发器已在平台钩子里做过）。软 IPI 和时钟也在这里登记，细节见中断篇、SMP 篇和时间篇。

再挂上系统调用那条固定 trap。低特权（EL0）的 `svc` 会进内核；助手按陷入时保存的状态决定要不要继承「当时是否允许 IRQ」，再调用可移植的 `syscall()`，返回前恢复。同特权级自己 `svc` 不改内核的 IRQ 策略。和 x86「进 syscall 时关掉可屏蔽中断」是同一类意图，机制见系统调用入口篇。

浮点已在进 `cmain` 之前由 `enable_fp` 打开，本函数不再重复。本函数没有失败返回路径。

---

## 7. 公开 API

本篇对外的 C 接口就是 `arch_setup.h` 里那四个与 ISA 同名的函数（实现都在 `start_arch.c`）。`boot.S` / `boot_map.c` 的入口只有装载器和跳板会跳进去，没有另做公开头。`arch_start_smp` 和 PSCI `cpu_on` 归 SMP / PSCI 篇，设备树查找和 `build_device_tree` 归 DTB 篇。

### 7.1 编排顺序（与源码一致）

| 调用方 | 和本篇有关的顺序 |
|--------|------------------|
| BSP `cmain` | … → **`prepare_arch`** → `phy_mm_init` → **`arch_cpu_info`** → `arch_enable_percpu(BSP_ID)` → `virt_mm_init` → **`arch_start_platform`** → **`arch_start_core(BSP_ID)`** → … |
| AP `start_secondary_cpu` | … → **`arch_start_core(cpu_id)`** → …（不再走 `prepare_arch` / `arch_cpu_info` / `arch_start_platform`） |

GIC：分发器只在 `arch_start_platform`；CPU 接口在每个核的 `arch_start_core`。

### 7.2 Image 头与 `setup_info`（布局见 §4.1）

字段表见 §4.1。与 x86 不同：没有 Multiboot magic/info 访问宏，入口约定是 Image + DTB（x0）。字段须与 `boot.S` 存取一致。

### 7.3 四个架构钩子（aarch64 侧语义）

```c
error_t prepare_arch(struct setup_info *arch_setup_info);
error_t arch_cpu_info(struct setup_info *arch_setup_info);
error_t arch_start_platform(struct setup_info *arch_setup_info);
error_t arch_start_core(cpu_id_t cpu_id);
```

| 接口 | 说明 |
|------|------|
| `prepare_arch` | 给已拷贝的 DTB 一张 2 MiB 高半映射，填 `boot_dtb_header_base_addr` 并推进 `map_end`；再校验 FDT 头。不建 `device_root`、不读 `bootargs`。成功返回 0；头校验失败返回 `-E_RENDEZVOS`，`cmain` 会 panic。 |
| `arch_cpu_info` | 参数不用。填 `cpu_info` 的 MT/U，`BSP_ID = 0`（固定）。总是成功。 |
| `arch_start_platform` | 全机一次，须在虚拟内存之后。建设备树，可选命令行，PSCI，GIC 分发器。缺 cmdline 不失败。当前实现总是返回成功。AP 不得调用。 |
| `arch_start_core` | 每核一次，补中断、本核 GIC 接口和系统调用入口，顺序见 §6.5。总是返回 0。不建线程或 port。 |

### 7.4 本篇有源码、但不算本篇公开 API 的符号

| 符号 | 归属 |
|------|------|
| `bsp_entry` / `ap_entry` / `drop_to_el1` / `init_mmu` | 装载器或 PSCI 跳入，没有 C 声明头 |
| `boot_map_pg_table` 等 | 仅汇编早期路径调用 |
| `build_device_tree` / `device_root` | DTB 篇（实现落在本篇 `start_arch.c`） |
| `arch_start_smp` | SMP 篇 |
| `psci_init` | PSCI 篇 |

---

## 8. 多架构

这些是 aarch64 自己的启动细节。和 x86_64 相比，差别主要在引导协议（Image 加 DTB，而不是 Multiboot 加 BIOS 区里的 RSDP）、`BSP_ID` 写死 0 而不是来自 APIC ID、以及 AP 走 PSCI `cpu_on` 而不是低地址 16 位跳板。相同的是交给 `cmain` 的四个函数名字和参数。riscv64 仅占位，不在本篇。

---

## 9. 测试

在 `core/` 下：

```bash
make ARCH=aarch64 config && make run
```

没有单独覆盖 `boot.S` 的测试用例。

---

## 10. 限制与后续

目前 Image 头对齐 Linux arm64 约定，其它协议见 evolution。`drop_to_el1` 在 EL2/EL3 未完成（**E8**）。早期 UART 只认 `"arm,pl011"`。内核镜像必须落在同一个 1 GiB 窗口内。TCR IPS 有意限制到不超过 40 位物理地址宽度。`arch_start_platform` 不因缺少 `bootargs` 失败。

---

## 11. 变更记录

- 2026-10-03：§4.6 各段补回小标题，方便按主题跳读。
- 2026-10-03：按 x86 平台篇同一套读法改写——先标明 §4.6 再读布局/流程；概述与 §6 改成连贯段落；§4.5 写清 EL3 实际落回「当作 EL1 返回」、EL2 空壳 `eret`。
- 2026-10-03：§4.6 改为启动用到的硬件基础（异常级/栈/系统寄存器/MMU/DTB/GIC/PSCI/`svc`），流程回指这里。
- 2026-10-03：§4.4 分开「开 MMU 最小集合」与 TCR/SCTLR 多开的位（SH/cache/HA/HD/AS/C/I）。
- 2026-10-03：§4.2 补物理/虚地址布局图（加载点、UART L3 窗口、DTB 拷贝）。
- 2026-10-03：§7.2 去掉与 §4.1 重复的字段表；§7.1 对齐现行 `arch_cpu_info` → `arch_enable_percpu`。
- 2026-10-03：§4.1 `setup_info` 表补「谁读」、`boot_map` 改写 `dtb_ptr`、`cpu_id` 经 PSCI context；通用字段语义链总览 §4.1.1。
- 2026-09-27：中文表述润色（母语习惯）。
- 2026-09-26：§7 全文审阅：`arch_setup.h` 补注释；写清钩子编排、GIC 分界与返回约定；去掉 `.c` 里过时/与 x86 串台的旧 brief。
- 2026-09-25：增补 §4.6（ARM ARM 翻译/EL/SPSel，以及 Linux arm64 booting 装载约定引用）。
- 2026-09-25：按操作计划补全链接→加载→早期布局→页表→开 MMU 链条；写清 Image/DTB 动机、EL 降级现状与 GIC 分界；流程节加详。
- 2026-09-20：三块分开（汇编 / 平台一次 / 每核），放回 full 十一节。
- 2026-08-29：曾按源码重做，并补过早期映射。
