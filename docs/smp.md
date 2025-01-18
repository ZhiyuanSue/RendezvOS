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
然后继续等待200ms

## aarch64的pcsi
在aarch64的启动中，有spin-table和pcsi两种
但是很显然，我的qemu里面dts打印出来发现他支持pcsi

## riscv直接使用sbi去多核启动即可
