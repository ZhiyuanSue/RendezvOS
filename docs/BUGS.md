# AArch64

1、开机在多核启动的时候，有概率卡死，有一定概率报重复map
2、ipc_multi_round测试结束之后，测试smp nexus test，可能会报错 [ BUDDY ]this zone have no memory to alloc，推测是由于这个测试会不停地向其他核心的分配器free，导致需要对方释放的分配队列过长导致的。


# x86_64