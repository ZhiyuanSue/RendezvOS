# 关于smp启动
首先在我增加这个md的commit中，我修改了smp的值为4，并且修改了aarch64的dts为对应的多核cpu的版本

## x86的acpi


## aarch64的pcsi
在aarch64的启动中，有spin-table和pcsi两种
但是很显然，我的qemu里面dts打印出来发现他支持pcsi

## riscv直接使用sbi去多核启动即可
