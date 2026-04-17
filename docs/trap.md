# Trap中断处理文档

本文档记录硬件的trap/interrupt处理行为，以及core/中的架构无关抽象层设计。

---

## 硬件行为

### x86_64

本文档主要记录Intel手册中的硬件行为。

#### 栈帧结构

- 栈帧大小固定为8字节（64位模式）
- 使用RBP保存栈帧基址
- 栈切换会自动保存SS:RSP

#### syscall/sysret机制

64位下syscall指令：
- 保存下一条指令到RCX
- 从IA32_LSTAR加载到RIP
- 保存RFLAGS低32位到R11
- 目标CS必须是ring 0
- sysret返回时CS必须是ring 3

#### 中断和异常分类

**故障（Fault）**：可纠正，返回地址指向故障指令
- 例如：Page Fault（#PF）

**陷阱（Trap）**：执行后立即报告，返回地址指向下一条指令
- 例如：溢出异常（OF）

**中止（Abort）**：严重错误，不允许重启
- 例如：Machine Check（#MC）

#### Page Fault Error Code

```
bit 0 (P): 0=页不存在, 1=保护违反
bit 1 (W/R): 0=读, 1=写
bit 2 (U/S): 0=内核态, 1=用户态
bit 3 (RSVD): 保留位违反
bit 4 (I/D): 0=数据访问, 1=指令获取
bit 5 (PK): 保护密钥违反
bit 6 (SS): 影子栈访问
bit 7 (HV): hypervisor MMIO
```

#### IST（中断栈表）

IDT中有IST字段（3bit），提供7个独立栈：
- IST=0：使用传统的栈切换机制
- IST=1-7：使用IST指定的独立栈
- IST指定的栈必须是per-CPU的

---

### aarch64

本文档主要记录ARM手册中的硬件行为。

#### 异常入口（Exception Entry）

发生异常时：
- CPSR保存到SPSR_ELx
- 返回地址保存到ELR_ELx
- DAIF设为1（禁止嵌套中断）
- ESR_ELx记录异常原因
- 自动选择SP_ELx
- 跳转到异常向量表
- 对于abort/data abort，FAR_ELx保存故障地址

#### 返回地址

- 异步异常：返回下一条指令
- 同步异常：返回触发异常的指令
- System call：返回svc的下一条指令

#### ESR寄存器格式

```
bit [24:0] ISS: Instruction Specific Syndrome
bit 25 IL: 指令位宽（64/32位）
bit [31:26] EC: Exception Class（异常原因）
```

EC值定义：
- 0x20: Instruction abort from lower EL
- 0x24: Data abort from lower EL
- 0x0e: Illegal execution state
- 0x18: SVC (system call)

---

## 架构无关抽象层设计

### 设计目标

在core/中提供架构无关的trap处理抽象，使得：
1. 上层代码可以用相同的接口处理不同架构的异常
2. 不需要为每个架构编写ifdef分支
3. 类型安全，易于维护

### 三层文件组织

```
include/rendezvos/
  trap_common.h         # 通用定义（所有架构共享）
  trap.h                # 通用接口声明

include/arch/x86_64/trap/
  trap_def.h            # x86特定宏定义（汇编可用）
  trap.h                # x86特定结构体、函数声明

arch/x86_64/trap/
  trap.c                # x86特定实现

include/arch/aarch64/trap/
  trap_def.h            # ARM特定宏定义（汇编可用）
  trap.h                # ARM特定结构体、函数声明

arch/aarch64/trap/
  trap.c                # ARM特定实现
```

### 核心数据结构

#### TRAP_COMMON宏

定义在`trap_common.h`中，所有架构的trap_info结构体必须包含：

```c
#define TRAP_COMMON \
    struct trap_frame *tf;     /* 指向原始trap_frame（不重复存储寄存器） */ \
    u8 trap_class;              /* 异常分类（见enum trap_class） */ \
    u8 is_user:1;               /* 来自用户态 */ \
    u8 is_fatal:1;              /* 致命错误 */ \
    u8 reserved1:6;             /* 保留 */ \
    vaddr fault_addr;           /* 故障地址（用于PF） */ \
    u8 is_write:1;              /* 写访问 */ \
    u8 is_execute:1;            /* 执行访问 */ \
    u8 is_present:1;            /* 页存在性 */ \
    u8 reserved2:5;             /* 保留 */ \
    u32 error_code;             /* 原始error code */ \
    u64 arch_flags;             /* 架构特定标志 */ \
    u64 reserved[4];            /* 保留扩展 */
```

**设计要点**：
- 使用位域（u8:1）节省空间
- `tf`指针访问原始寄存器，避免重复存储ESR/FAR/error_code等
- 预留reserved字段保证对齐和扩展

#### 异常分类（enum trap_class）

定义在`trap_common.h`中：

```c
enum trap_class {
    TRAP_CLASS_PAGE_FAULT,        /* 内存访问故障：页不存在或保护违反 */
    TRAP_CLASS_ILLEGAL_INSTR,     /* 非法或未定义指令 */
    TRAP_CLASS_BREAKPOINT,        /* 断点（调试器） */
    TRAP_CLASS_ALIGNMENT,         /* 未对齐访问（某些架构可模拟） */
    TRAP_CLASS_DIVIDE_ERROR,      /* 除零或整数溢出 */
    TRAP_CLASS_OVERFLOW,          /* 算术溢出 */
    TRAP_CLASS_FP_FAULT,          /* 浮点异常 */
    TRAP_CLASS_GP_FAULT,          /* 通用保护错误（特权或访问违规） */
    TRAP_CLASS_STACK_FAULT,       /* 栈损坏或限制违规 */
    TRAP_CLASS_MACHINE_CHECK,     /* 硬件错误或损坏 */
    TRAP_CLASS_SYSCALL,           /* 系统调用入口 */
    TRAP_CLASS_IRQ,               /* 外部设备中断 */
    TRAP_CLASS_DEBUG,             /* 调试异常（单步执行、断点） */
    TRAP_CLASS_DOUBLE_FAULT,      /* 双重故障（关键错误，表示handler有bug） */
    TRAP_CLASS_SEGMENT_FAULT,     /* 段相关故障（x86: 无效TSS、段不存在） */
    TRAP_CLASS_SECURITY,          /* 安全异常（x86 #SE, ARM MTE fault） */
    TRAP_CLASS_VIRTUALIZATION,    /* 虚拟化异常（x86 #VE, EPT违规） */
    TRAP_CLASS_ASYNC_ABORT,       /* 异步中止（ARM SError, 外部中止） */
    TRAP_CLASS_UNKNOWN,           /* 未知异常 */
};
```

**设计要点**：
- 按异常的**语义性质**分类，而不是硬件编号
- 跨架构统一：x86的#PF、aarch64的data abort都是`TRAP_CLASS_PAGE_FAULT`
- 注释完全架构无关，不出现"(x86)"、"ARM"等字样

#### 架构特定结构体

**x86_64**（定义在`arch/x86_64/trap/trap.h`）：

```c
struct x86_64_trap_info {
    TRAP_COMMON
    
    u64 cr2;  /* Page fault地址（来自CR2寄存器） */
    
    /* PF error code详细解析 */
    struct {
        u8 page_present:1;
        u8 write_access:1;
        u8 user_mode:1;
        u8 reserved_bit:1;
        u8 instruction_fetch:1;
        u8 protection_key:1;
        u8 shadow_stack:1;
        u8 hv_mmio:1;
        u32 reserved_bits:24;
    } pf_ec;
};
```

**aarch64**（定义在`arch/aarch64/trap/trap.h`）：

```c
struct aarch64_trap_info {
    TRAP_COMMON
    
    /* ESR寄存器解析 */
    struct {
        u32 ec:6;      /* Exception Class */
        u32 iss:25;    /* Instruction Specific Syndrome [24:0] */
        u8 dfsc:6;      /* Data Fault Status Code */
        u8 ifsc:6;      /* Instruction Fault Status Code */
        u8 wnR:1;       /* Write not Read */
        u8 isv:1;       /* Instruction Syndrome Valid */
    } esr_fields;
};
```

**设计要点**：
- `TRAP_COMMON`必须在开头
- 架构特定字段在后面
- 不重复trap_frame中的字段（ESR、FAR、error_code等）

### 核心接口

#### 注册固定异常处理函数

```c
void register_fixed_trap(enum trap_class trap_class,
                        fixed_trap_handler_t handler,
                        u64 irq_attr);
```

**功能**：
- 按异常类型（trap_class）注册，而不是架构特定的trap ID
- 架构层自动映射到具体的trap ID(s)
- 例如：`TRAP_CLASS_PAGE_FAULT`在x86映射到#PF（14），在aarch64映射到EC 0x20和0x24

**反向注册策略**：
- 同一个trap_class可能对应多个trap_id
- 例如：`TRAP_CLASS_PAGE_FAULT`在aarch64映射到EC 0x20, 0x21, 0x24, 0x25
- `register_fixed_trap()`会为所有相关的trap_id注册wrapper
- 每个trap_id只能有一个handler，后注册的会覆盖先注册的

**优先级和互斥**（详见trap_common.h）：
- ❌ **不要混用**：`register_fixed_trap()`和`register_irq_handler()`注册同一个trap_id
- ✅ **推荐做法**：使用`register_fixed_trap()`进行架构无关处理
- ✅ **特殊情况**：仅在需要架构特定处理时使用`register_irq_handler()`

**使用示例**：
```c
register_fixed_trap(TRAP_CLASS_PAGE_FAULT, my_pf_handler, IRQ_NEED_EOI);
register_fixed_trap(TRAP_CLASS_ILLEGAL_INSTR, my_ill_handler, 0);
```

#### 填充trap信息

```c
// x86_64
void arch_populate_trap_info(struct trap_frame *tf, 
                             struct x86_64_trap_info *info);

// aarch64
void arch_populate_trap_info(struct trap_frame *tf,
                             struct aarch64_trap_info *info);
```

**功能**：
- 解析trap_info和error_code/ESR
- 填充TRAP_COMMON字段和架构特定字段
- 设置`info->tf`指向原始trap_frame

### 上层使用方式

#### 简单的架构无关handler

```c
static void my_page_fault_handler(struct trap_frame *tf)
{
    struct x86_64_trap_info info;  // 或 struct aarch64_trap_info
    arch_populate_trap_info(tf, &info);
    
    /* 访问通用字段（架构无关） */
    printf("Page fault at %p, %s, user=%d\n",
           (void*)info.fault_addr,
           info.is_write ? "write" : "read",
           info.is_user);
    
    /* 处理COW、demand paging等 */
    handle_page_fault(&info);
}

void init_traps(void)
{
    register_fixed_trap(TRAP_CLASS_PAGE_FAULT, my_page_fault_handler, IRQ_NEED_EOI);
}
```

#### 访问原始寄存器（通过tf指针）

```c
static void my_handler(struct trap_frame *tf)
{
#if defined(_X86_64_)
    struct x86_64_trap_info info;
    arch_populate_trap_info(tf, &info);
    
    /* 通过tf指针访问原始寄存器 */
    u64 raw_error_code = info.tf->error_code;
    u64 rip = info.tf->rip;
    
#elif defined(_AARCH64_)
    struct aarch64_trap_info info;
    arch_populate_trap_info(tf, &info);
    
    u64 raw_esr = info.tf->ESR;
    u64 far = info.tf->FAR;
#endif
}
```

#### 访问架构特定字段

```c
#if defined(_X86_64_)
    if (info.pf_ec.instruction_fetch) {
        /* 处理指令获取缺页 */
    }
    if (info.pf_ec.protection_key) {
        /* 处理保护密钥违反 */
    }

#elif defined(_AARCH64_)
    if (info.esr_fields.dfsc == 0x11) {
        /* Translation fault */
    }
#endif
```

### 设计优势

1. **架构无关**：上层代码用trap_class而不是硬件trap ID
2. **类型安全**：使用enum、位域、结构体，不是magic number
3. **不重复存储**：通过tf指针访问原始寄存器
4. **可扩展**：添加新架构只需实现两个函数
5. **清晰分离**：通用字段（TRAP_COMMON）vs架构特定字段

### 循环依赖解决方案

通过`trap_common.h`解决：
- `trap_common.h`：前向声明`struct trap_frame;`，定义TRAP_COMMON宏、enum trap_class
- `arch/*/trap.h`：包含`trap_common.h`，定义完整的`struct trap_frame`和`struct xxx_trap_info`
- `rendezvous/trap.h`：包含`arch/*/trap.h`，声明通用接口

**依赖关系**：
```
rendezvous/trap.h
  → include arch/x86_64/trap/trap.h
      → include rendezvous/trap_common.h  ✅ 无循环

arch/x86_64/trap/trap.h
  → include rendezvous/trap_common.h    ✅ 无循环
```

---

## 使用指南

### 对于core/开发者

**实现新的架构支持**（如riscv64）：
1. 创建`include/arch/riscv64/trap/trap_def.h`：定义宏常量
2. 创建`include/arch/riscv64/trap/trap.h`：
   - 包含`trap_common.h`
   - 定义`struct trap_frame`（寄存器）
   - 定义`struct riscv64_trap_info { TRAP_COMMON ... }`
3. 实现`arch/riscv64/trap/trap.c`：
   - 实现`arch_populate_trap_info()`
   - 实现`register_fixed_trap()`（映射trap_class到硬件trap ID）

### 对于上层开发者（linux_layer/）

**推荐的编写方式**：
1. 优先使用`trap_class`而不是架构特定trap ID
2. 优先访问TRAP_COMMON字段，实现架构无关代码
3. 只在必要时使用`#ifdef`访问架构特定字段
4. 通过`info.tf`访问原始寄存器，不要假设字段重复存储

**错误示例**：
```c
// ❌ 架构特定代码
#ifdef _X86_64_
register_irq_handler(14, handler, 0);
#elif defined(_AARCH64_)
register_irq_handler(0x24, handler, 0);
register_irq_handler(0x20, handler, 0);
#endif
```

**正确示例**：
```c
// ✅ 架构无关代码
register_fixed_trap(TRAP_CLASS_PAGE_FAULT, handler, IRQ_NEED_EOI);
```

---

## 历史设计问题

### 中断号设计问题

最初只实现了x86_64，有固定的IDT表。但aarch64的设计不同：
- 中断向量表只有16个表项
- 同步异常需要根据EC（Exception Class）分发
- 需要区分同特权级vs跨特权级的异常

### 解决方案

**不改变原有设计**，仍然映射到数组：

```c
DEFINE_PER_CPU(struct irq, irq_vector[NR_IRQ]);
```

**对于aarch64**：
- 前64项：用于不同的EC值（0x00-0x3F）
- 后面项（偏移64）：用于IRQ

**对于riscv64**：采用类似设计

**trap_info复用**：
- 64位trap_info中保留若干位用于表示中断号
- 其余位数足够用于存储其他信息

---

## 关键设计决策

### aarch64系统调用的双EC映射（0x15和0x18）

ARM定义了两个SVC相关的EC值：
- **EC 0x15**: SVC from lower EL（用户态→内核态的系统调用）
- **EC 0x18**: SVC in AArch64（同特权级的SVC指令）

**为什么两个都映射到TRAP_CLASS_SYSCALL？**

1. **主要路径**：EC 0x15是正常的系统调用路径（EL0执行SVC进入EL1）
2. **防御性编程**：EC 0x18用于捕获异常情况
   - 内核态错误地执行SVC指令（内核bug）
   - 攻击者通过内核漏洞执行SVC
   - 虚拟化或特殊场景下的同特权级SVC

**实际使用**：
- 绝大多数系统调用使用EC 0x15
- EC 0x18作为安全网，可以记录日志或触发panic
- 两者都映射到同一个handler简化了系统调用的统一处理

**参考**：Linux内核也同时处理这两个EC值（arch/arm64/kernel/traps.c）

### TRAP_ARCH_USED的语义

x86_64的`TRAP_ARCH_USED`定义：
```c
enum TRAP_NUM {
    // ...
    TRAP_SE,          /* 30: Security Exception */
    TRAP_31 = 31,     /* Reserved */
    TRAP_ARCH_USED,   /* = 32: 上界 */
};
```

**TRAP_ARCH_USED的含义**：
- ✅ **是**：异常向量范围的上界（0-31，共32个）
- ✅ **是**：数组大小定义：`x86_trap_class_map[TRAP_ARCH_USED]`
- ❌ **不是**：实际使用的异常向量数量
- ❌ **不是**：用于逻辑判断的计数器

**为什么是32而不是21？**
- Intel保留向量0-31用于架构定义的异常
- 向量21-29目前保留，但可能在未来使用
- 向量30（#SE Security Exception）已经存在
- `TRAP_ARCH_USED = 32`确保数组可以容纳所有可能的向量

**扩展性**：
- 如果Intel将来定义向量29、30等，只需在enum中添加
- `TRAP_ARCH_USED`会自动扩展
- 不需要手动调整数组大小

### Handler注册的优先级和互斥

Core/提供两层handler注册：

**1. Fixed trap handlers（架构无关）**：
```c
register_fixed_trap(TRAP_CLASS_PAGE_FAULT, handler, attr);
```
- 按trap_class注册，不是trap_id
- 一个trap_class可能对应多个trap_id
- 内部调用`register_irq_handler()`为每个相关trap_id注册wrapper

**2. Direct IRQ handlers（架构特定）**：
```c
register_irq_handler(14, handler, attr);  // x86 #PF
```
- 按trap_id注册
- 架构特定，绕过trap_class抽象
- **会覆盖**同一trap_id的fixed handler

**互斥规则**：
- ❌ **不要混用**：对同一trap_id同时使用fixed和direct注册
- ✅ **推荐**：优先使用`register_fixed_trap()`
- ✅ **例外**：仅在架构特定处理时使用`register_irq_handler()`

**如果需要两种handler**：
- 方案1：先注册direct，再注册fixed（fixed覆盖）
- 方案2：在handler中链式调用（direct调用fixed，或反之）
- 方案3：根据条件动态选择注册方式

---

## 总结

硬件层提供架构特定的trap机制（IDT、EC、ESR等），抽象层提供：
1. 统一的异常分类（trap_class）
2. 统一的信息结构（TRAP_COMMON）
3. 统一的注册接口（register_fixed_trap）

上层代码通过抽象层处理异常，不需要关心底层架构差异。

**设计保证**：
- ✅ 所有映射的EC值都经过ARM手册验证
- ✅ 所有x86向量都符合Intel SDM定义
- ✅ DFSC/IFSC与EC清晰分离，不混淆语义
- ✅ 编译期检查确保trap_class不会溢出u8
- ✅ Handler注册优先级明确，避免意外覆盖
- ✅ ARM双EC映射有完整的文档说明
