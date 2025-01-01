# 关于本文档
本文档主要是记录在发生int和trap的时候，硬件的处理行为

# x86_64
## volume 1 chapter 6
首先是栈，一个栈是用SS:ESP来描述的，push会地址向下并写入，pop会地址增加并弹出。
当前栈是由ss段描述的
EBP则用于描述栈帧的返回地址

栈帧的size，在64位模式下，都是固定的8Bytes，所以无需考虑奇奇怪怪的事情
栈中，使用EBP保存弹出的地址，而把过程的返回地址压入栈
RET指令会弹出返回地址到EIP上

在64位下，ss段的DPL让他自动修改为等于CPL
LDS ，POP ES等指令变得无效

### 在32位模式下的near call和ret
会压入参数，这时候esp是指向最后一个参数，再放上返回地址，这时候参数指向最后一个参数之下的返回地址
然后返回的时候，esp现在指向返回地址，先弹出返回地址到eip上，再弹出n个参数

### 在32位模式下的far call和ret
则不仅仅是EIP，先压入cs，再压入eip
其他一样

### 32位模式下跨特权级的call
只能通过一个gate，也就是比如IDT表里面指定的东西
然后需要切换栈，这个栈在每个特权级都有存在。并且在切换的时候自动保存。
比如当前是ring 3，那么ring 2，1，0的statks保存在TSS中（我查到的资料是说，这是TSS存在的唯一的理由）

而一个跨特权级的调用，在栈上的表现是（见figure 6.4）
会在原本的栈上，压入参数，在新的栈上压入SS:ESP,参数(参数是复制过去的，在call gate里面指定了复制多少参数过去),CS:EIP
然后再使用新的CS:EIP取代原有的

### 64位模式下的branch扩展

### sysenter和sysexit在64位下的扩展成了syscall和sysret
见chapter 4 volumn 2B（我还真有这本实体书，我翻书去）
这里总共只有syscall，sysenter，sysexit，sysret
#### syscall
保存syscall指令的下一条指令到RCX，从IA32_LSTAR载入到RIP
SYSRET则将RCX复制到RIP
syscall将RFLAGS的低32保存到R11，随后将IA32_FMASK(MSR C000 0084)载入到RFLAGS
除了RF之外的其他RFLAGS不会自动清除掉，在sysret的时候会从R11保存回去

syscall的CS的目标必须是ring 0
sysret的cs目标必须是ring 3

看具体操作
条件：CS.L == 1 && IA32_EFER.LMA == 1 && IA32_EFER.SCE ==1
RIP的操作如上描述
RFLAGS操作如上（应该是EFLAGS）
CPL = 0
用IA32_STAR_MSR[47:32]指定的CS段（段描述符见书）
用 IA32_STAR_MSR[47:32]+8指定的SS段（段描述符见书）

#### sysenter
包括三个寄存器
IA32_SYSENTER_CS:0x174
IA32_SYSENTER_ESP:0x175
IA32_SYSENTER_EIP:0x176

（32位模式下）
CS是32位的，但是低16位用于指定CS段描述符，同样用于计算SS段（CS段的下一个）
EIP也是32位的。ESP也是

他并不保存调用者的信息（应该也是因此比较快吧）
需要保证CPUID SEP位，以及包括family和model和stepping
（以下不再描述32位情况下的sysenter的实现情况）

（64位模式下）
EIP和ESP两个MSR都是保存了64位的地址（canonical模式）而CS则包含一个NULL选择子

#### sysexit
和sysenter是一对
IA32_SYSENTER_CS仍然用来做段寻址
EDX相当于EIP
ECX相当于ESP
多余也不说了（感觉没啥意义，在Shampoos下应该是纯64位模式，不用这个）

#### sysret
sysret按照syscall的相反步骤，从rcx复制到rip
对于段选择子，CS被选择为 IA32_STAR[63:48]+16而SS则被选择为+8（和syscall并不相同，他是先ss再cs）
这里返回的时候还需要考虑compatibility mode（应该不用管）

### 中断和异常（应该也是32位）
这里的简单介绍，值得注意的两点（其他对我而言都是常识）
1、并不是所有的额中断都是对应于一个IDT entry，比如SMI
2、当查看到中断时或者异常的时候，处理器可以选择其中之一做：1、执行一个handler procedure（应该是一段过程） 2、执行一个handler task

这里说，类似于call gate
int和trap有点不同的是，int会清理掉if位，然后禁止中断嵌套（直到内核主动打开返回或者允许嵌套）

如果目标代码段和源代码段处于同一个特权级，那么会沿用原有的stack，否则会切换stack
如果没有发生stack切换（也就是同一个特权级）
会从前到后，push eflags，cs，eip
再push error code

而如果一个stack 切换发生了（也就是切换了特权级）
会从前到后，先保存需要后面push的内容，并按照TSS中给定的情况切换新的栈，然后在新的栈中push ss esp（压入原有的堆栈） ，eflags，cs，eip
再压入error code

文档中也描述了前面说的可以执行一个handler task，通过调用一个task gate desc（但是他在64位模式下我不确定是否可行）

### 64位模式下的中断和异常
所有的handlers都是64位的

中断栈都是64bit对齐的

**无论是否切换栈，都会压入SS：RSP，**（32位模式下只有CPL发生变化才切换）

IRET行为相应的发生变化

新的中断栈切换行为（但是没说是啥，估计得看volumn 3？）

栈帧的对齐也不同

（总而言之，我们继续来看volumn 3中对这个尤其是栈的介绍吧，感觉volumn 1介绍还是太少了）

## volume 3 chapter 6


