# README
我试图支持AMD的GPU用于推理和计算，因此，尝试搜集，阅读Linux内核中关于ROCm相关的内容

# 仓库
https://github.com/ROCm/ROCK-Kernel-Driver.git
这下载下来是一个完整的Linux仓库，按照情况，如果要启用AMD的功能，还需要开启AMD相关的内核支持
如果只是计算部分的ROCm的话，需要哪些内容尚不清楚
除此之外，还有一个仓库
https://github.com/ROCm/ROCm.git
这个作为ROCm的主仓库，应该也是有价值的。

很多地方都是缩写，因此，我需要首先尽可能弄明白，这些缩写都是什么意思（包括询问大模型）

gfx是graphics processing（看上去不是）
KFD：根据deepseek的说法，这个地方是整个ROCm主要涉及的代码

Doorbell：用于CPU和GPU之间的通信机制

# kfd_chardev.c
用户和驱动之间通过
/dev/kfd
这个文件，通过ioctl，进行通信，这玩意儿被伪装成一个字符设备。

所以这里可以理解为rocm的一个面向用户的前端。

对于这个函数的调用路径是这样的

```
amdgpu_init
---->amdgpu_amdkfd_init
-------->kgd2kfd_init
------------>kfd_init
---------------->kfd_chardev_init //最终注册chardev
---->pci_register_driver
-------->amdgpu_pci_probe
------------>amdgpu_driver_load_kms
---------------->amdgpu_device_init //这里设置了amdgpu_device这个设备
```

```
amdgpu_device_init
---->设置一些adev的内容的初始化
---->amdgpu_device_ip_early_init //这里试图初始化一些IP
---->aperture_remove_conflicting_pci_devices //这个函数用于处理冲突的mmio的，没有大用
---->amdgpu_gmc_tmz_set //配置内存加密，好像也没啥用
---->amdgpu_gmc_noretry_set //配置内存的无重试模式，简单来说，访问内存出错，直接重试一次和直接报错的区别
---->adev->have_atomics_support //接下来配置atomic
---->amdgpu_doorbell_init //doorbell是一种特殊的cpu通知gpu的机制。写入特定的doorbell寄存器
---->amdgpu_reset_init //字面意思吧
---->amdgpu_device_detect_sriov_bios //SR-IOV 是一种硬件虚拟化技术，允许单个物理 GPU 被多个虚拟机（VM）直接共享，提高虚拟化环境中的性能。
...
---->amdgpu_device_ip_init

```
按照一阶段任务，最重要的是各个ip核的初始化

