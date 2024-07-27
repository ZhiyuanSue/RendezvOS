# x86_64
x86_64下
首先看中断向量表
对于CPU的中断，他包括了32个CPU自己定义好的中断，然后剩下的中断则是用于外设的

外设的中断源
我这边试图写四种情况下
## no APIC
这时候只能用8259A和8254（用于时钟），但是正常情况需要disable掉APIC

## APIC

## xAPIC

## x2APIC

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

因此总体而言，对于中断处理的这部分代码，x86_64和aarch64架构，我都需要做
1，对于CPU定义好的中断的处理（这部分需要优先做）
2，控制好apic，gic等中断控制器的处理。包括这些抽象等。