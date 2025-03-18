# 关于smp启动
首先在我增加这个md的commit中，我修改了smp的值为4，并且修改了aarch64的dts为对应的多核cpu的版本

## x86的acpi
x86的smp的启动称为MP启动
通过 BIPI和FIPI消息发送
分为BSP和AP两种类型的处理器
BSP在IA32_APIC_BASE MSR寄存器的bit8里面表明
首先需要发送INIT的IPI
等一会之后10ms
然后发送SIPI
然后继续等待200us
然后再发送一次SIPI

但是需要注意以下几点
1/SIPI本身存在对多个核心全都通知的机制的，需要权衡是否可以这么做
如果一口气全都分配的话，是很难按照需求分配堆栈的。所以实际上推荐一个一个启动。
2/需要注意每个核心的堆栈，他们必须不尽相同
3/SIPI本身传递的启动向量的位置，必须是在1M以下的，但是，rendezvos本身的代码都在1M以上的位置
而且启动的时候是实模式，不太可能直接跳转到1M以上的位置，所以在rendezvos中需要进行一次内存拷贝，将AP的启动代码拷贝到具体位置（我这里设置了0x1000这个位置）并不超过一页。

对于数据的传递，在x86中我需要传递两个参数，stack top和cpuid
我选择在setup info那里增加两个字段
并且使用逐个启动的方式
而bsp则等待在那里，直到所有的ap都启动。

## aarch64的pcsi
在aarch64的启动中，有spin-table和pcsi两种
但是很显然，我的qemu里面dts打印出来发现他支持pcsi
参考文档（官方）
https://developer.arm.com/documentation/den0022/fb/?lang=en

### psci
psci其实是一个接口，这个接口主要是通过smc或者hvc等方式向更上一层的系统调用EL2或者EL3的函数去处理
提供的接口包括
• Core idle management.（但是按照他的说法，并不管理动态频率，那是ACPI等需要处理的）
• Dynamic addition of cores to and removal of cores from the system, often referred to as hotplug.（热插拔和多核启动不是一回事）
• Secondary core boot.
• Moving trusted OS context from one core to another.
• System shutdown and reset.

#### 关于虚拟化
他这里提到了两种类型的虚拟化
type1和type2

因而这里的电源管理OSPF也分为物理的和虚拟的
对于虚拟OSPF的PSCI的调用，那么会被截获（但是总之，我们目前不涉及到PSCI的虚拟化部分）

#### idle 管理
arm可能处于以下几种状态
• Run
• StandBy
    通过WFI或者WFE指令进入这一状态
• Retention
    看上去和standby没啥差别，但是，对于外部调试来说，无法访问调试寄存器了。
    本文档都用standby，因为对于os来说其实基本没啥差别
• Powerdown
    对于上下文已经存在了损坏，所以psci提供了一个重启之后执行的地址等等一系列管理的方法（我个人认为这是在描述系统已经起来的时候，然后把它放到powerdown，类似于睡眠模式）
逐级向下更深（这里还给出了深的定义，其实没啥好说的）

#### power 状态系统拓扑
设计了一个功率域的概念
大概来说，具有三个层级，system的，cluster的，core的
从上到下形成一棵树的样子
显而易见的是，子节点的功率状态（上面四个之一）必须比父节点的更深
比如说子节点在retention，那么父节点不能是powerdown
如果要关机，那么必须把其他节点都变成powerdown，然后最后一个核心把自己和整个系统关机

所以他支持了两种模式
platform-coordinated mode
OS-initiated mode

##### platform-coordinated mode
默认的，1.0之前只有这个（个人认为只需要考虑这个即可）

##### OS-initiated mode
不管他，OS视图和实现视图会有一些差别

#### CPU热插拔和多核启动
这里提出了热插拔和功耗管理中的powerdown的差别
1) When a core is hot unplugged, the supervisory software stops all use of that core in interrupt and thread processing. The calling supervisory software regards the core as no longer available.
2) The supervisory software must issue an explicit command to bring a core back online, that is, to hotplug a core. The appropriate supervisory software only starts using that core in thread scheduling, or interrupt service routines, after this command.
3) With hotplug, wakeup events that could restart a powered-down core are not expected on the cores that have been hot unplugged.
另外，相对而言，热插拔和其他核心启动基本上是类似的事件，所以他提供了一样的接口

需要提供一个相应的启动地址

#### psci的function
相关的函数参数，我只列举必要的，更为重要的是如何调用
首先这里的smc和hvc其实是一样的，所以下面的smc32在对应的情况下是hvc32
对于参数，他分为两种情况，smc32和smc64
对于只使用32位的参数的函数调用
aarch32中，他通过r0-r3传递参数，并返回在r0
aarch64中，他通过w0-w3传递参数，并返回在w0

对于使用64位参数的函数调用（只有aarch64了）
通过x0-x3传递参数
并返回在x0

对于smc和hvc的指令，后面应该跟一个立即数，这个数值必须是0



## riscv直接使用sbi去多核启动即可
