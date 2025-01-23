# 8254时钟
这是在使用8259A的情况下，使用8254的时钟。
连接到IRQ0

关于结构
8位控制寄存器
16位初值寄存器CR
16位减一寄存器CE
16位输出锁存寄存器OL

设定初值，复制到减一寄存器
然后OL其实可以读出来，不过好像没啥用

对于计数器，首先需要弄明白的是他所工作的几种模式
方式0：计数结束发送一次中断
方式1：单稳脉冲（同样单次但是并不是设定之后立马开始，而是触发门控信号才开始（不懂））
方式2：循环方式的频率发生器，这应该是我们想要的当成时钟中断的方式
方式3：方波发生器
方式4：软件触发选通信号
方式5：硬件触发选通信号

在正常PC中，他的端口号为40H-42H为timer0-2，然后43H为控制字寄存器。
反正对于每个计数器，都是先写控制字，然后往每个8位的端口中写入低8位，再写入高8位
整体而言不算复杂

顺带一提，一个魔法（我确实没查到更详细的说明），关于61H端口
在我查到的对APIC时钟的校正程序中，存在一个对61H端口的读写，我查到说法是，其bit0-1用于开关8253/4
这个当然，他的操作是and了0xfd，也就是忽略1位，而保留0位原值，同时or了1，也就是置位0位。
我的理解是，这跟他采用的APIC的模式为方式1有关，他不能去更改他的gate否则会导致重新计数。
并且，他选用了42端口（也就是计时器2），也跟8255的方式有关系。


# 时钟选型
在x86下，除了上面的8254时钟之外（那太古老了，还有APIC，HPET，TSC等时钟）
在处理具体的时钟的时候，还需要考虑一个选型问题，
同时，他们应当分为两种，一个是我不停的读取值就完事了，另一种，会涉及到时钟中断等问题（那么就需要中断的中断号和对应的处理代码）
https://zhuanlan.zhihu.com/p/678583925
这个文章讲了时钟选型的问题
最简单的，Local APIC是必须有的，因为per cpu的时钟中断是必须的
同时Local APIC时钟是需要校准的，但我也看到有的说法是，他有个固定的时钟频率（姑且不采信）

TSC时钟，他只需要rdtsc指令就行，有很多优势。
（好在目前我的qemu里面并不支持这一点）

# APIC时钟及其校准
https://zhuanlan.zhihu.com/p/678583968

# 时间系统
除了上面的东西，我获得了固定10ms发一次中断的APIC中断，还需要一个完整的时间系统的支持。（但是我目前没啥概念，只能先去了解一下了）

# aarch64的时钟以及时钟中断
arm的时钟其实有板级支持。
也就是不同的平台不一样
但是通用的称为generic timer
这部分在Arm平台的手册中就有，我看的是Armv8.6，在D11，以及I2
总共也没有多少页

整体的架构上

存在一个system level的时钟，以及每个PE都有的timer
每个PE都有的timer包括如下几个
 - An EL1 physical timer.
 - A Non-secure EL2 physical timer.
 - An EL3 physical timer.
 - An EL1 virtual timer.
 - A Non-secure EL2 virtual timer.
 - A Secure EL2 virtual timer.
 - A Secure EL2 physical timer.

## CNTFRQ_EL0
虽然是个EL0的
但是在以下几种情况下可以读取
Secure and Non-secure EL2.
Secure and Non-secure EL1.
When CNTKCTL_EL1.EL0PCTEN is set to 1, Secure and Non-secure EL0.
这里又涉及到了CNTKCTL_EL1（Counter-timer Kernel Control register）
对于这个的写入，只有在最高等级的EL才可以写入（实现了的最高等级），所以应该是不能写的

## physical counter
 - CNTPCT_EL0, Counter-timer Physical Count register
这个寄存器用于存放当前的physical的计数
整个64位都是

 - CNTPOFF_EL2, Counter-timer Physical Offset register
这个寄存器主要用于给physical count加上一个offset，处理虚拟化的情况

下面给了个例子，大概是说，如果读取CNTPCTSS_EL0（这是一个用于替代读取CNTPCT_EL0的寄存器）
那么，就无需ISB
反之，需要在 MRS Xi, CNTPCT_EL0这样的指令之前，加上DSB和ISB
否则，只需要DSB即可

## virtual counter
同样的CNTPOFF_EL2，设置了offset
 - CNTVCT_EL0寄存器，存放了虚拟计数值
对于同步相关，和物理计数器值相似

## Event
我确实没怎么看懂啥是event stream

CNTKCTL_EL1.{EVNTEN, EVNTDIR, EVNTI, EVNTIS}这几位用于设置一个由virtual counter的中断事件

CNTHCTL_EL2.{EVNTEN, EVNTDIR, EVNTI, EVNTIS}则用于设置由于物理计数器产生的中断事件

## TimerValue 和 CompareValue 的对比
 - CompareValue
Is based around a 64-bit CompareValue that provides a 64-bit unsigned upcounter.
 - TimerValue
Provides an alternative view of the CompareValue, called the TimerValue, that appears to operate as a 32-bit downcounter.（也就是设置一个倒计时，递减到0就触发中断）

相应的寄存器，我觉得表D11-1和D11-2已经很清楚了

分CV，TV，控制，以及EL1-3，安全和非安全，phy和virt，就衍生出来很多个寄存器了

TV的操作手法就是计算出来下一次需要何时中断，那么设置好相应的值，是增加的
而CV，则是，隐含的倒计时，不过需要注意这是一个signed的值，可能溢出

CTL，控制寄存器总共就三个bit，enable mask和status

## 关于I2的内容
看上去I2相关的内容，是关于EL2或者EL3中，如何设置好generic timer的部分，在EL1的OS按道理不应该考虑这一点。

## 设置中断号
这就需要去GIC中申请中断了。