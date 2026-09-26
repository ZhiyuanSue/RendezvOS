# v0.1 全量文档索引

对应 core v0.1 冻结代码。生成规则见 `core/docs/文档生成操作计划.md`。

下面列出 **44 篇正文**（不含本 README）。每篇标题前有两个方框，从左到右：

- 第一个 `[x]`：初稿已生成（撰写者 / AI 完成并自查后勾选）
- 第二个 `[x]`：Maintainer 审定通过

两个都是 `[x]`，该篇才算完成；仅第一个 `[x]` 表示待审定。每篇的「源码」是该篇负责讲清楚的文件。

文件名在 **整个 full 目录内统一按阅读顺序编号（`00`–`43`）**，分区目录只做归类；因此引用里看到 `04-平台启动-x86_64.md` 即全局第 5 篇，不会与其他分区的「第 4 篇」撞号。

示例：`### [x] [ ] 01-启动与初始化/04-平台启动-x86_64.md` — 已有初稿，等你审定。

---

## 目录树

```text
v0.1/full/
├── README.md                          ← 本文件
├── 00-总览/
│   ├── 00-架构与源码布局.md
│   └── 01-构建与链接.md
├── 01-启动与初始化/
│   ├── 02-启动流程总览.md
│   ├── 03-模块初始化与内核入口.md
│   ├── 04-平台启动-x86_64.md
│   └── 05-平台启动-aarch64.md
├── 02-内存管理/
│   ├── 06-物理内存与Buddy分配器.md
│   ├── 07-虚拟地址空间与页表.md
│   ├── 08-Radix树与用户映射.md
│   ├── 09-kmalloc与内核堆.md
│   ├── 10-page_slice稀疏页索引.md
│   ├── 11-TLB与缓存一致性.md
│   └── 12-ASID与地址空间标识.md
├── 03-任务与调度/
│   ├── 13-线程与Task_Manager.md
│   ├── 14-线程创建与ELF加载.md
│   ├── 15-VSpace所有权与调度切换.md
│   ├── 16-CPU亲和性-创建时绑核.md
│   └── 17-EBR与线程资源回收.md
├── 04-IPC/
│   ├── 18-Port与消息模型.md
│   ├── 19-阻塞与非阻塞收发.md
│   ├── 20-kmsg与TLV序列化.md
│   ├── 21-Port钩子与准入门.md
│   └── 22-无锁队列与EBR设计.md
├── 05-陷阱与中断/
│   ├── 23-Trap抽象与分类.md
│   ├── 24-系统调用入口.md
│   ├── 25-IRQ向量分配与处理.md
│   ├── 26-平台中断-x86_64-APIC与PIC.md
│   └── 27-平台中断-aarch64-GIC.md
├── 06-SMP与同步/
│   ├── 28-SMP启动与处理器拓扑.md
│   ├── 29-per-CPU数据与访问约定.md
│   ├── 30-软IPI机制.md
│   ├── 31-锁与内存屏障.md
│   └── 32-TLB_shootdown与跨核一致性.md
├── 07-时间与定时器/
│   └── 33-时间与定时器子系统.md
├── 08-日志与控制台/
│   └── 34-日志与UART控制台.md
├── 09-平台模块/
│   ├── 35-ACPI与MADT-x86_64.md
│   ├── 36-DTB与设备树-aarch64.md
│   ├── 37-ELF加载辅助模块.md
│   ├── 38-PCI枚举与配置空间.md
│   └── 39-PSCI与处理器电源-aarch64.md
├── 10-基础设施/
│   ├── 40-错误码与panic.md
│   ├── 41-名称索引注册表.md
│   └── 42-公共基础库与数据结构.md
└── 11-测试/
    └── 43-内核测试框架与测例索引.md
```

共 11 分区、44 篇正文。

---

## 当前状态（2026-09-19）

- **初稿 / 深读重做**：44 篇第一格均为 `[x]`；逐篇深读重做已完成。
- **Maintainer 审定**：看各篇标题前第二格。已通过：`00-总览/00-架构与源码布局.md`、`00-总览/01-构建与链接.md`。
- **compat**：规划见 `v0.1/compat/README.md`；正文待 full 全部第二格通过后再写。
- **evolution**：远期项见 `v0.1/evolution/TODO.md`；`design/`、`archive/` 正文未建。

审定前自查（撰写侧已做一轮，Maintainer 可复验）：

- 无 `old/` 引用；无绑定 `linux_layer/` 等具体上层路径。
- core 测例命令为 `cd core && make ARCH=<isa> config && make all && make run`（非 `make build`）。
- 各篇 §10「限制与后续」中的远期工作应在 `evolution/TODO.md` 有对应条目或 design 占位。

---

## 00-总览

### [x] [x] 00-总览/00-架构与源码布局.md

职责：core 树分区、`include/` vs `kernel/` vs `arch/` vs `modules/` 关系；公开 API 边界；与 compat 的分层

源码：`include/rendezvos/`（目录级） · `include/common/` · `include/modules/modules.h` · `kernel/system/main.c` · `include/rendezvos/task/initcall.h`

说明：各子系统细节不在此展开，仅指向本目录树下对应篇章

---

### [x] [x] 00-总览/01-构建与链接.md

职责：`make config` / **`make all`** / `make run` 流程；standalone 与链接方 `make -C core all` + `EXTRA_OBJECTS` 两种目标；`configure.py` 与 `config_*.json`；`gen_makefile.py`；链接脚本段布局（含 `.init.call.*`）；`modules.h` 生成；与启动/initcall 的构建侧关系

源码：`Makefile` · `Makefile.env`（生成） · `script/config/configure.py` · `script/config/config_*.json` · `script/make/gen_makefile.py` · `script/make/build.mk` · `script/make/qemu.mk` · `script/link/*_linker.ld`

说明：链接方树顶 Makefile 不在 core 树内；core 只文档化 `EXTRA_OBJECTS`/`OVERRIDE_CFLAGS` 等挂接点。运行时 initcall 见 `01-启动与初始化/03-模块初始化与内核入口.md`

---

## 01-启动与初始化

### [x] [ ] 01-启动与初始化/02-启动流程总览.md

职责：BSP 上 `cmain` 的调用顺序；以及链接→加载→早期页表→进入 `cmain` 的跨架构地图（§4.4）。某一 ISA 的表项见平台篇

源码：`kernel/system/main.c` · `arch/x86_64/boot/boot.S` · `arch/aarch64/boot/boot.S` · `arch/x86_64/boot/start_arch.c` · `arch/aarch64/boot/start_arch.c` · `include/arch/x86_64/boot/arch_setup.h` · `include/arch/aarch64/boot/arch_setup.h` · `include/arch/x86_64/boot/multiboot.h` · `include/arch/x86_64/boot/multiboot2.h`

---

### [x] [ ] 01-启动与初始化/03-模块初始化与内核入口.md

职责：`DEFINE_INIT` / `do_init_call`；兼容层在启动顺序上如何挂进来；注册与真正被调度运行的差别。boot/idle 的切换见线程篇

源码：`include/rendezvos/task/initcall.h` · `kernel/task/thread_boot.c` · `kernel/task/task_manager.c` · `kernel/system/main.c` · `modules/helloworld/helloworld.c` · `include/modules/helloworld/helloworld.h`

---

### [x] [ ] 01-启动与初始化/04-平台启动-x86_64.md

职责：x86 `boot.S` 到 `cmain`；只做一次的 `prepare_arch` / `arch_cpu_info` / `arch_start_platform`；每核 `arch_start_core`。AP 唤醒见 SMP 篇

源码：`arch/x86_64/boot/boot.S` · `arch/x86_64/boot/gdt.c` · `arch/x86_64/boot/start_arch.c` · `arch/x86_64/boot/smp.c` · `arch/x86_64/acpi/acpi.c` · `arch/x86_64/acpi/madt.c` · `include/arch/x86_64/desc.h` · `include/arch/x86_64/boot/arch_setup.h`

---

### [x] [ ] 01-启动与初始化/05-平台启动-aarch64.md

职责：aarch64 `boot.S` 到 `cmain`；只做一次的 `prepare_arch` / `arch_cpu_info` / `arch_start_platform`；每核 `arch_start_core`。AP 的 `cpu_on` 见 SMP 篇

源码：`arch/aarch64/boot/boot.S` · `arch/aarch64/boot/boot_map.c` · `arch/aarch64/boot/start_arch.c` · `arch/aarch64/boot/smp.c` · `arch/aarch64/psci/psci.c` · `arch/aarch64/psci/psci_call.S` · `include/arch/aarch64/psci/psci.h` · `include/arch/aarch64/psci/psci_error.h`

---

## 02-内存管理

### [x] [ ] 02-内存管理/06-物理内存与Buddy分配器.md

职责：zone、`phy_mm_init`、buddy 算法、reclaim hook、多 zone 骨架

源码：`kernel/mm/pmm.c` · `kernel/mm/buddy_pmm.c` · `include/rendezvos/mm/pmm.h` · `include/rendezvos/mm/buddy_pmm.h` · `arch/x86_64/mm/pmm.c` · `arch/aarch64/mm/pmm.c` · `include/arch/x86_64/mm/pmm.h` · `include/arch/aarch64/mm/pmm.h` · `include/arch/riscv64/mm/pmm.h` · `include/arch/loongarch/mm/pmm.h`

---

### [x] [ ] 02-内存管理/07-虚拟地址空间与页表.md

职责：`VSpace`、`Map_Handler`、`map()`、自映射、kernel/user 映射策略

源码：`kernel/mm/vmm.c` · `kernel/mm/map_handler.c` · `kernel/mm/map_util_page.S` · `include/rendezvos/mm/vmm.h` · `include/rendezvos/mm/map_handler.h` · `arch/x86_64/mm/vmm.c` · `arch/aarch64/mm/vmm.c` · `include/arch/x86_64/mm/vmm.h` · `include/arch/aarch64/mm/vmm.h` · `include/arch/x86_64/mm/page_table_def.h` · `include/arch/aarch64/mm/page_table_def.h` · `include/arch/x86_64/mm/mm.h` · `include/arch/aarch64/mm/mm.h`

---

### [x] [ ] 02-内存管理/08-Radix树与用户映射.md

职责：radix 真源、`mm_user_utils_*`、锁层级 L0/L2、`register_vspace`、page fault 协作

源码：`kernel/mm/vmm_radix_tree.c` · `kernel/mm/mm_user_utils.c` · `include/rendezvos/mm/vmm_radix_tree.h` · `include/rendezvos/mm/mm_user_utils.h`

说明：`register_vspace` 使用的 name 索引见 `10-基础设施/41-名称索引注册表.md`，本篇不展开

---

### [x] [ ] 02-内存管理/09-kmalloc与内核堆.md

职责：per-CPU `kallocator`、slab/页路径、`root_vspace` 关系

源码：`kernel/mm/kmalloc.c` · `kernel/mm/string.c` · `include/rendezvos/mm/kmalloc.h` · `include/rendezvos/mm/allocator.h`

---

### [x] [ ] 02-内存管理/10-page_slice稀疏页索引.md

职责：pgoff→kva、与 VSpace 边界、copy 辅助

源码：`kernel/mm/page_slice.c` · `kernel/mm/page_slice_copy.c` · `include/rendezvos/mm/page_slice.h` · `include/rendezvos/mm/page_slice_copy.h`

---

### [x] [ ] 02-内存管理/11-TLB与缓存一致性.md

职责：`tlb_cpu_mask`、unmap/shootdown、arch flush 钩子

源码：`include/rendezvos/mm/tlb_cpu_mask.h` · `include/arch/x86_64/sync/tlb.h` · `include/arch/aarch64/sync/tlb.h` · `include/arch/x86_64/sync/cache.h` · `include/arch/aarch64/sync/cache.h` · `arch/x86_64/mm/arch_smp_tlb_flush.c`

---

### [x] [ ] 02-内存管理/12-ASID与地址空间标识.md

职责：ASID 分配/回收、与 schedule 切换关系

源码：`kernel/mm/asid.c` · `include/rendezvos/mm/asid.h` · `include/arch/x86_64/mm/asid.h` · `include/arch/aarch64/mm/asid.h`

---

## 03-任务与调度

### [x] [ ] 03-任务与调度/13-线程与Task_Manager.md

职责：`Thread_Base`、`Task_Manager`、状态机、`schedule`、run queue

源码：`kernel/task/thread.c` · `kernel/task/task_manager.c` · `include/rendezvos/task/thread.h` · `include/rendezvos/task/id.h` · `kernel/task/id.c`

---

### [x] [ ] 03-任务与调度/14-线程创建与ELF加载.md

职责：`create_thread` / `copy_thread` / `gen_thread_from_func` / `gen_thread_from_elf` / `run_elf_program` / user stack

源码：`kernel/task/thread_loader.c` · `include/rendezvos/task/thread_loader.h` · `modules/elf/elf.c` · `modules/elf/elf_print.c` · `include/modules/elf/elf.h` · `include/modules/elf/elf_common.h` · `include/modules/elf/elf_32.h` · `include/modules/elf/elf_64.h` · `include/modules/elf/elf_print.h` · `arch/x86_64/task/arch_thread.c` · `arch/aarch64/task/arch_thread.c` · `arch/x86_64/task/arch_switch.S` · `arch/aarch64/task/arch_switch.S` · `arch/x86_64/task/arch_run_thread.S` · `arch/aarch64/task/arch_run_thread.S` · `arch/x86_64/task/arch_user_switch.S` · `arch/aarch64/task/arch_user_switch.S` · `include/arch/x86_64/thread_arch.h` · `include/arch/aarch64/thread_arch.h`

---

### [x] [ ] 03-任务与调度/15-VSpace所有权与调度切换.md

职责：`thread->vs` refcount、schedule 时 HW AS 切换、teardown、boot 线程例外

源码：`kernel/task/thread.c`（schedule/teardown 相关） · `kernel/task/task_manager.c` · `include/rendezvos/task/thread.h` · `include/rendezvos/mm/vmm.h`

---

### [x] [ ] 03-任务与调度/16-CPU亲和性-创建时绑核.md

职责：`cpu_id_is_online` · `task_manager_for_cpu` · `thread_owner_cpu` · `add_thread_to_cpu`；**无**运行期迁移

源码：`kernel/task/thread.c`（affinity  helpers） · `include/rendezvos/task/thread.h` · `modules/test/thread_affinity_test.c` · `modules/test/smp_test.c` · `include/modules/test/test.h`

---

### [x] [ ] 03-任务与调度/17-EBR与线程资源回收.md

职责：EBR 在 IPC/线程路径的用法；`delete_thread` 与资源释放顺序

源码：`kernel/task/ebr.c` · `include/rendezvos/task/ebr.h` · `kernel/task/thread.c`（`thread_set_name_with_copy`、release）

---

## 04-IPC

### [x] [ ] 04-IPC/18-Port与消息模型.md

职责：两层模型（port rendezvous + per-thread 队列）；`Message_Port_t`；全局 port 表

源码：`kernel/ipc/port.c` · `kernel/ipc/message.c` · `include/rendezvos/ipc/port.h` · `include/rendezvos/ipc/message.h` · `kernel/registry/name_index.c`

---

### [x] [ ] 04-IPC/19-阻塞与非阻塞收发.md

职责：`send_msg` / `recv_msg` / try 变体；push vs pull；返回码

源码：`kernel/ipc/ipc.c` · `include/rendezvos/ipc/ipc.h`

---

### [x] [ ] 04-IPC/20-kmsg与TLV序列化.md

职责：`kmsg_t`、TLV、`ipc_serial`、system async outbound

源码：`kernel/ipc/kmsg.c` · `kernel/ipc/ipc_serial.c` · `include/rendezvos/ipc/kmsg.h` · `include/rendezvos/ipc/kmsg_system.h` · `include/rendezvos/ipc/ipc_serial.h`

---

### [x] [ ] 04-IPC/21-Port钩子与准入门.md

职责：`port_append_hooks_t`、`ops_allow`、`port_ops_begin`、FAM append

源码：`kernel/ipc/port.c` · `include/rendezvos/ipc/port.h` · `modules/test/single_port_test.c`

---

### [x] [ ] 04-IPC/22-无锁队列与EBR设计.md

职责：MS queue、tagged ptr、ABA、与 IPC 的结合；**设计**为主

源码：`include/common/dsa/ms_queue.h` · `include/common/taggedptr.h` · `kernel/task/ebr.c` · `kernel/ipc/ipc.c` · `kernel/ipc/message.c`

---

## 05-陷阱与中断

### [x] [ ] 05-陷阱与中断/23-Trap抽象与分类.md

职责：`trap_class`、`register_fixed_trap`、`trap_frame`、与 personality 弱符号边界

源码：`kernel/trap/trap.c` · `include/rendezvos/trap/trap.h` · `include/rendezvos/trap/trap_common.h` · `include/rendezvos/trap.h` · `arch/x86_64/trap/trap_vec.S` · `arch/aarch64/trap/trap_vec.S`

---

### [x] [ ] 05-陷阱与中断/24-系统调用入口.md

职责：弱符号 `syscall`、`arch_syscall_*`、从 syscall 返回用户

源码：`kernel/system/syscall.c` · `arch/x86_64/trap/trap.c` · `arch/aarch64/trap/trap.c` · `arch/x86_64/trap/kernel_entry.S` · `arch/aarch64/trap/kernel_entry.S` · `arch/x86_64/trap/tss.c` · `include/arch/x86_64/trap/tss.h` · `include/arch/x86_64/trap/trap.h` · `include/arch/aarch64/trap/trap.h` · `include/arch/x86_64/trap/trap_def.h` · `include/arch/aarch64/trap/trap_def.h`

---

### [x] [ ] 05-陷阱与中断/25-IRQ向量分配与处理.md

职责：`irq_vector_alloc`、`register_irq_handler`、`IRQ_NEED_EOI`、per-CPU `irq_vector[]`

源码：`kernel/trap/trap.c` · `include/rendezvos/trap/trap.h` · `kernel/time/time.c`

---

### [x] [ ] 05-陷阱与中断/26-平台中断-x86_64-APIC与PIC.md

职责：8259、Local APIC/x2APIC、PIT/HPET、timer IPI、**IOAPIC 空壳现状**

源码：`arch/x86_64/PIC/PIC.c` · `arch/x86_64/PIC/LocalAPIC.c` · `arch/x86_64/PIC/IRQ.c` · `arch/x86_64/PIC/PIT.c` · `arch/x86_64/time/time.c` · `arch/x86_64/time/rtc.c` · `include/arch/x86_64/PIC/*.h` · `include/arch/x86_64/time.h` · `include/arch/x86_64/io.h` · `include/arch/x86_64/io_port.h` · `include/arch/x86_64/msr.h`

---

### [x] [ ] 05-陷阱与中断/27-平台中断-aarch64-GIC.md

职责：GICv2 驱动、SPI/SGI、EOI、`gicd_v2_set_affinity`（arch 内部）

源码：`arch/aarch64/gic/gic_v2.c` · `include/arch/aarch64/gic/gic_v2.h` · `include/arch/aarch64/gic/gic_v3.h` · `arch/aarch64/time/generic_time.c` · `include/arch/aarch64/time.h`

---

## 06-SMP与同步

### [x] [ ] 06-SMP与同步/28-SMP启动与处理器拓扑.md

职责：`smp_start`、NR_CPU、BSP/AP、拓扑头文件现状

源码：`kernel/smp/smp.c` · `include/rendezvos/smp/smp.h` · `include/rendezvos/cpu_topology.h` · `include/rendezvos/smp/cpu_id.h` · `include/arch/x86_64/smp.h` · `include/arch/aarch64/smp.h` · `include/arch/x86_64/arch_bitmap.h` · `include/arch/aarch64/arch_bitmap.h`

---

### [x] [ ] 06-SMP与同步/29-per-CPU数据与访问约定.md

职责：`percpu()`、`core_tm`、`current_vspace`、MCS `me` 必须本 CPU

源码：`kernel/smp/percpu.c` · `include/rendezvos/smp/percpu.h` · `arch/x86_64/percpu.c` · `arch/aarch64/percpu.c`

---

### [x] [ ] 06-SMP与同步/30-软IPI机制.md

职责：`smp_ipi_register/send/init`、arch 门铃

源码：`kernel/smp/ipi.c` · `include/rendezvos/smp/ipi.h` · `arch/x86_64/smp/arch_smp_ipi.c` · `arch/aarch64/smp/arch_smp_ipi.c`

---

### [x] [ ] 06-SMP与同步/31-锁与内存屏障.md

职责：spinlock、CAS lock、barrier、与 IPC 的关系

源码：`include/rendezvos/sync/spin_lock.h` · `include/rendezvos/sync/cas_lock.h` · `include/rendezvos/sync/barrier.h` · `include/common/atomic.h` · `include/common/spin.h` · `include/arch/x86_64/sync/barrier.h` · `include/arch/aarch64/sync/barrier.h` · `include/arch/x86_64/sync/sync.h` · `arch/x86_64/atomic.c` · `arch/aarch64/atomic.c`

---

### [x] [ ] 06-SMP与同步/32-TLB_shootdown与跨核一致性.md

职责：跨核 TLB flush 路径、与 MM 的协作

源码：`arch/x86_64/mm/arch_smp_tlb_flush.c` · `include/arch/x86_64/sync/tlb.h` · `include/arch/aarch64/sync/tlb.h` · `kernel/smp/ipi.c`

---

## 07-时间与定时器

### [x] [ ] 07-时间与定时器/33-时间与定时器子系统.md

职责：`time.h` 抽象、per-CPU 事件树、arch timer 后端

源码：`kernel/time/time.c` · `include/rendezvos/time.h` · `arch/x86_64/time/time.c` · `arch/x86_64/time/rtc.c` · `arch/x86_64/PIC/PIT.c` · `arch/aarch64/time/generic_time.c` · `include/arch/x86_64/time.h` · `include/arch/aarch64/time.h`

---

## 08-日志与控制台

### [x] [ ] 08-日志与控制台/34-日志与UART控制台.md

职责：`pr_*` → UART、`uart_getc` 轮询、VGA console（x86）、v0.1 冻结现状

源码：`modules/log/log.c` · `include/modules/log/log.h` · `modules/driver/uart/uart.c` · `modules/driver/uart/uart_16550A.c` · `modules/driver/uart/uart_pl011.c` · `include/modules/driver/uart/uart.h` · `include/modules/driver/uart/uart_16550A.h` · `include/modules/driver/uart/uart_pl011.h` · `modules/driver/x86_char_console/char_console.c` · `include/modules/driver/x86_char_console/char_console.h` · `include/modules/driver/driver.h`

---

## 09-平台模块

### [x] [ ] 09-平台模块/35-ACPI与MADT-x86_64.md

职责：RSDP/MADT 遍历、CPU 枚举

源码：`modules/acpi/acpi.c` · `modules/acpi/acpi_madt.c` · `include/modules/acpi/acpi.h` · `include/modules/acpi/acpi_madt.h` · `include/modules/acpi/acpi_table.h` · `include/modules/acpi/acpi_defs.h` · `include/modules/acpi/acpi_fadt.h` · `arch/x86_64/acpi/acpi.c` · `arch/x86_64/acpi/madt.c`

---

### [x] [ ] 09-平台模块/36-DTB与设备树-aarch64.md

职责：FDT 两阶段（raw / device_node）、查找 API；明确无 ACPI、无 aarch64 PCI 消费

源码：`modules/dtb/dtb.c` · `modules/dtb/dev_tree.c` · `modules/dtb/property.c` · `modules/dtb/print_property.c` · `include/modules/dtb/dtb.h` · `include/modules/dtb/dev_tree.h` · `include/modules/dtb/property.h` · `include/modules/dtb/fdt.h` · `include/modules/dtb/libfdt.h` · `include/modules/dtb/print_property.h` · `arch/aarch64/boot/start_arch.c` · `arch/aarch64/boot/boot_map.c`

---

### [x] [ ] 09-平台模块/37-ELF加载辅助模块.md

职责：与 §03 loader 的边界：纯 ELF 解析/打印

源码：`modules/elf/elf.c` · `modules/elf/elf_print.c` · `include/modules/elf/*.h`

---

### [x] [ ] 09-平台模块/38-PCI枚举与配置空间.md

职责：PCI 扫描、config 访问、设备树

源码：`modules/pci/pci_ops.c` · `modules/pci/pci_dev_tree.c` · `include/modules/pci/pci.h` · `include/modules/pci/pci_ops.h` · `include/modules/pci/pci_dev_tree.h`

---

### [x] [ ] 09-平台模块/39-PSCI与处理器电源-aarch64.md

职责：PSCI 调用约定、secondary boot 与 idle

源码：`arch/aarch64/psci/psci.c` · `arch/aarch64/psci/psci_call.S` · `include/arch/aarch64/psci/psci.h` · `include/arch/aarch64/psci/psci_error.h` · `include/arch/aarch64/power_ctrl.h` · `include/arch/x86_64/power_ctrl.h` · `kernel/system/powerd.c` · `include/rendezvos/system/powerd.h`

---

## 10-基础设施

### [x] [ ] 10-基础设施/40-错误码与panic.md

职责：`error_t`、panic、powerd

源码：`include/rendezvos/error.h` · `kernel/system/panic.c` · `include/rendezvos/system/panic.h` · `kernel/system/powerd.c` · `include/rendezvos/system/powerd.h` · `include/rendezvos/common.h` · `include/rendezvos/limits.h`

---

### [x] [ ] 10-基础设施/41-名称索引注册表.md

职责：全局 name → object 索引（port、vspace 等）

源码：`kernel/registry/name_index.c` · `include/rendezvos/registry/name_index.h`

---

### [x] [ ] 10-基础设施/42-公共基础库与数据结构.md

职责：`common/` 类型、refcount、rb_tree、list、bitmap、endian

源码：`include/common/types.h` · `include/common/refcount.h` · `include/common/string.h` · `include/common/dsa/rb_tree.h` · `include/common/dsa/list.h` · `include/common/dsa/bitmap.h` · `include/common/dsa/tree.h` · `include/common/endianness.h` · `include/common/bit.h` · `include/common/align.h` · `include/common/limits.h` · `include/common/stddef.h` · `include/common/stdbool.h` · `include/common/stdarg.h` · `include/common/rand.h` · `include/common/mm.h` · `include/common/assemble.h` · `include/arch/x86_64/sys_ctrl.h` · `include/arch/x86_64/sys_ctrl_def.h` · `include/arch/aarch64/sys_ctrl.h` · `include/arch/aarch64/sys_ctrl_def.h` · `include/arch/x86_64/cpuinfo.h` · `include/arch/aarch64/cpuinfo.h` · `include/arch/x86_64/arch_common.h` · `include/arch/aarch64/arch_common.h` · `include/arch/riscv64/arch_common.h` · `include/arch/loongarch/arch_common.h` · `arch/riscv64/kernel/sbi.c` · `arch/riscv64/kernel/sbi.h`

说明：riscv64 / loongarch 仅为占位；`sbi` 无完整 bring-up，正文注明即可

---

## 11-测试

### [x] [ ] 11-测试/43-内核测试框架与测例索引.md

职责：`BSP_test`/`AP_test`、`single_test`/`smp_test`、`check_result` 语义

源码：`modules/test/test.c` · `modules/test/single_test.c` · `modules/test/smp_test.c` · `include/modules/test/test.h` · `modules/test/single_*.c` · `modules/test/smp_*.c` · `modules/test/thread_affinity_test.c`

---

## 查漏

implementation 文件应各归一篇「源码」列表，头文件随对应 `.c` 计入同一篇，不重复展开。

- `script/`（config、make、link）→ `00-总览/01-构建与链接.md`
- `kernel/`（约 28 个文件）→ 02–07、10 各篇
- `modules/` 除 test（约 20）→ 01、03、08、09
- `modules/test/`（约 20）→ 11
- `arch/x86_64/`（约 25）→ 01、03、05–07
- `arch/aarch64/`（约 18）→ 01、03、05–07、09
- `arch/riscv64/` 仅 `sbi.c` / `sbi.h` → `10-基础设施/42-公共基础库与数据结构.md`（占位）

早期独立中间层 `nexus` 已移除；现行契约见 `08-Radix树与用户映射.md`（`vmm_radix_tree` / `mm_user_utils`）。`tcb.h` 等若仍见历史名，以当前 `thread.h` 等为准，不单独开文。

---

## 建议审定顺序

初稿已全部完成。Maintainer 可按下列顺序审定（第二格 `[x]`）：

1. `00-总览/00-架构与源码布局.md`、`00-总览/01-构建与链接.md`（索引与交叉引用）
2. `01-启动与初始化/` → `02-内存管理/` → `03-任务与调度/`
3. `04-IPC/` → `05-陷阱与中断/` → `06-SMP与同步/`
4. `07-时间与定时器/` → `08-日志与控制台/` → `09-平台模块/`
5. `10-基础设施/` → `11-测试/`

单篇审定通过：勾该篇标题前**第二个** `[x]`。全部 `[x] [x]` 后再启动 compat 正文与 evolution `design/` 撰写。