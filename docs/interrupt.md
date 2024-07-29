# x86_64
x86_64下
首先看中断向量表
对于CPU的中断，他包括了32个CPU自己定义好的中断，然后剩下的中断则是用于外设的。但是也可以用int n触发
这部分可以看手册的第六章。

外设的中断源
我这边试图写四种情况下
## no APIC
这时候只能用8259A和8254（用于时钟），但是正常情况需要disable掉APIC

## APIC

## xAPIC

## x2APIC

## syscall and sysret
具体见column 3的5.8.8，和之前使用中断的方式不同，这个快速系统调用，需要额外考虑
这里只说64位模式。
首先是在gdt表中，需要额外考虑段的情况

IA32_STAR选择了具体的段，这里最高16位用于描述sysret的段，接下来16位用于描述syscall的段

IA32_LSTAR选择syscall的入口地址

堆栈的段是通过IA32_START加8（也就是gdt表中的下一个条目，所以至少在用户态和内核态需要增加gdt表的内容，至少包含用户的堆栈和内核的堆栈的段。）
使用IA32_FMASK来和当前的RFLAGS进行and运算，作为进入syscall的保留使用的位。（可以通过这种方式直接屏蔽中断位）

sysret的时候，则是使用高16位往后数两个的段作为sysret的段
rip则是从RCX复制到RIP（所以需要设置RCX寄存器，如果设错了，会有GP异常）
EFLAGS则是从R11复制回来的。

总之，在做x86的syscall这部分的时候，可以不考虑中断，因为这已经是另外的东西了。

# aarch64
arm下的中断，首先需要区分同步和异步
同步表示的是当前运行的指令报错了，而异步则是用于外设中断
异步还分为三类，包括IRQ和FIQ以及SError
总共有四种，SError本身是可以不处理的，因为我确实查不到更多的信息，Linux也没有处理，只是上报。

异常向量表中则只定义了16个entry（每个level），异常向量表由VBAR寄存器指定

分别是
current el with sp0
current el with spx
lower el with aarch64
lower el with aarch32

每个分别有同步异常，以及IRQ FIQ以及SError

同步异常
寄存器包括ESR，表示异常的原因
FAR错误虚拟地址
ELR寄存器则是中断返回地址。（也就是eret返回的地址）

异步的这些中断来自于中断寄存器GIC

GIC目前也有四个版本
v1-v4

对于aarch64下的syscall，会有个SVC（对于EL2有HVC，EL3有secure monitor call）
通过这种方式去搞syscall

SPSR寄存器
这里面有一堆寄存器的位需要处理
DAIF四个位表示允许屏蔽异常事件
D表示debug，A表示SError，I，IRQ，F，FIQ

SPSel，选择SP0或者SP_x


# riscv

还是同样的分为同步异常和异步异常

mcause和scause表示了异常的原因，最高位是interrupt字段，表示原因，如果是1则是异步异常，而为0则是同步异常
同步异常也是各种处理器的问题，得看exception code字段，而异步异常同样看EC字段，（不考虑M模式还是S模式的话）主要分为软件中断（比如ipi），时钟中断，外部中断三种。
（s模式的一些字段的修改会影响m模式的一些字段，只是包含在了m模式的字段当中）

mstatus中的MIE和SIE字段用于使能M模式和S模式下的中断
（和mie寄存器和sie寄存器不是一回事）

MPIE和SPIE则是用于临时保存之前的中断使能状态。
SPP和MPP是中断之前的特权模式

mtvec寄存器和stvec寄存器，在最后两位是mode，表示通过统一的入口函数还是通过中断向量表的方式

mie寄存器，对于异步异常的s和m模式下的软件中断，时钟中断，外部中断，分别进行使能，共6种
sie寄存器同理，共3种

mtval寄存器表明发生异常的虚拟地址

mip表示需要响应的外部中断的情况，总共也是6种

mideleg和medeleg（在m模式下委托中断给s模式，s态下用不到）

正常情况下的初始化流程
配置vec寄存器
配置plic等中断初始化外设中断
使能mie或sie寄存器
配置mstatus寄存器

上下文保存
x1-x31
sepc
sstatus
scause
ra
sp
等的值


## PLIC中断控制器
分为本地的中断和外部的中断，PLIC那一大堆寄存器，就是配置一堆中断号的寄存器的使能，优先级等等。


因此总体而言，对于中断处理的这部分代码，x86_64和aarch64架构，我都需要做
1，对于CPU定义好的中断的处理（这部分需要优先做）
2，控制好apic，gic等中断控制器的处理。包括这些抽象等。