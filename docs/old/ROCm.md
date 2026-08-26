# README
我试图支持AMD的GPU用于推理和计算，因此，尝试搜集，阅读Linux内核中关于ROCm相关的内容

# 使用虚拟机显卡直通的方式
我还是记录一下我在这台机器上的操作。

首先需要找到对应的pci

```
lspci -nn | grep -i amd
00:00.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Root Complex [1022:14d8]
00:00.2 IOMMU [0806]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge IOMMU [1022:14d9]
00:01.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Dummy Host Bridge [1022:14da]
00:01.1 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge GPP Bridge [1022:14db]
00:01.2 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge GPP Bridge [1022:14db]
00:02.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Dummy Host Bridge [1022:14da]
00:02.1 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge GPP Bridge [1022:14db]
00:03.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Dummy Host Bridge [1022:14da]
00:04.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Dummy Host Bridge [1022:14da]
00:08.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Dummy Host Bridge [1022:14da]
00:08.1 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Internal GPP Bridge to Bus [C:A] [1022:14dd]
00:08.3 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Internal GPP Bridge to Bus [C:A] [1022:14dd]
00:14.0 SMBus [0c05]: Advanced Micro Devices, Inc. [AMD] FCH SMBus Controller [1022:790b] (rev 71)
00:14.3 ISA bridge [0601]: Advanced Micro Devices, Inc. [AMD] FCH LPC Bridge [1022:790e] (rev 51)
00:18.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 0 [1022:14e0]
00:18.1 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 1 [1022:14e1]
00:18.2 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 2 [1022:14e2]
00:18.3 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 3 [1022:14e3]
00:18.4 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 4 [1022:14e4]
00:18.5 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 5 [1022:14e5]
00:18.6 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 6 [1022:14e6]
00:18.7 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge Data Fabric; Function 7 [1022:14e7]
01:00.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD/ATI] Navi 10 XL Upstream Port of PCI Express Switch [1002:1478] (rev 10)
02:00.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD/ATI] Navi 10 XL Downstream Port of PCI Express Switch [1002:1479] (rev 10)
03:00.0 VGA compatible controller [0300]: Advanced Micro Devices, Inc. [AMD/ATI] Navi 31 [Radeon RX 7900 XT/7900 XTX/7900 GRE/7900M] [1002:744c] (rev c8)
03:00.1 Audio device [0403]: Advanced Micro Devices, Inc. [AMD/ATI] Navi 31 HDMI/DP Audio [1002:ab30]
05:00.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Upstream Port [1022:43f4] (rev 01)
06:00.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
06:08.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
06:09.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
06:0a.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
06:0b.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
06:0c.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
06:0d.0 PCI bridge [0604]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset PCIe Switch Downstream Port [1022:43f5] (rev 01)
0c:00.0 USB controller [0c03]: Advanced Micro Devices, Inc. [AMD] 800 Series Chipset USB 3.x XHCI Controller [1022:43fc] (rev 01)
0d:00.0 SATA controller [0106]: Advanced Micro Devices, Inc. [AMD] 600 Series Chipset SATA Controller [1022:43f6] (rev 01)
0e:00.0 VGA compatible controller [0300]: Advanced Micro Devices, Inc. [AMD/ATI] Granite Ridge [Radeon Graphics] [1002:13c0] (rev c9)
0e:00.1 Audio device [0403]: Advanced Micro Devices, Inc. [AMD/ATI] Radeon High Definition Audio Controller [Rembrandt/Strix] [1002:1640]
0e:00.2 Encryption controller [1080]: Advanced Micro Devices, Inc. [AMD] Family 19h PSP/CCP [1022:1649]
0e:00.3 USB controller [0c03]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge USB 3.1 xHCI [1022:15b6]
0e:00.4 USB controller [0c03]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge USB 3.1 xHCI [1022:15b7]
0e:00.6 Audio device [0403]: Advanced Micro Devices, Inc. [AMD] Family 17h/19h/1ah HD Audio Controller [1022:15e3]
0f:00.0 USB controller [0c03]: Advanced Micro Devices, Inc. [AMD] Raphael/Granite Ridge USB 2.0 xHCI [1022:15b8]
```

找到的显卡是03:00.0的pci的值

然后03:00.1是同一个组里面的audio的值

查找到他们的设备id是1002:744c,1002:ab30

从而在`/etc/default/grup`文件中增加

```
GRUB_CMDLINE_LINUX="... amd_iommu=on iommu=pt vfio-pci.ids=1002:744c,1002:ab30"
```

随后更新配置

```
sudo update-grub
sudo update-initramfs -u
```

更新重启之后，可以看到

```
lspci -s 03:00.0 -k
03:00.0 VGA compatible controller: Advanced Micro Devices, Inc. [AMD/ATI] Navi 31 [Radeon RX 7900 XT/7900 XTX/7900 GRE/7900M] (rev c8)
	Subsystem: XFX Limited RX-79XMERCB9 [SPEEDSTER MERC 310 RX 7900 XTX]
	Kernel driver in use: vfio-pci
	Kernel modules: amdgpu
```



我使用的命令为

```
sudo qemu-system-x86_64 -kernel build/kernel.bin  -smp 4 -m 1G -machine q35 -nographic -device vfio-pci,host=03:00.0,multifunction=on -device vfio-pci,host=0000:03:00.1
```
需要注意一点，这里必须还有一个音频设备也需要加进去，不然会有问题

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

