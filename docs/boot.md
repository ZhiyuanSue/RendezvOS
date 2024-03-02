# x86架构下
## 关于multiboot2
我试图shampoos中使用multiboot2协议来引导，但是，当我按照multiboot2的格式写了引导程序之后，发现事情并没有那么简单，现有的qemu版本并不能支持这个协议。
同时我发现multiboot1协议在qemu中无法直接支持x86_64的启动引导。
我找到了一个可能存在的RFC：
https://lore.kernel.org/qemu-devel/20240206135231.234184-1-jens.nyberg@gmail.com/
但是在我编写这份代码的时候（2024年3月），这个patch才在一个月之前才被提交到内核邮件列表
但是，对于X86_64文件的支持，仍然是必须的，否则我就必须想别的办法去完成引导 。

## PVH
直接加载x86_64的elf文件，可以在其中加入一个ELF note
https://stackoverflow.com/questions/64492509/how-can-i-create-a-pvh-kernel-that-will-be-run-by-qemu
原因在于
https://stefano-garzarella.github.io/posts/2019-08-23-qemu-linux-kernel-pvh/
可以直接支持x86_64镜像的协议必须这样子写
对于相关协议，可以看
https://xenbits.xen.org/docs/unstable/misc/pvh.html

对于i386和x86_64上面的引导，我仍然使用multiboot1，因为他确实可以很好的引导32位的代码，当然我想这不是决定性的解决方案，决定性的问题在于，我应该使用bin文件而不是elf文件格式来作为加载的内核镜像。

## 生成bin文件并加载
直接使用elf文件无法加载
因此可以使用objcopy生成bin文件再进行加载。
在这种情况下，就可以使用multiboot1协议进行加载了
OK，问题解决。
对于multiboot1协议，可以参考以下链接
https://www.gnu.org/software/grub/manual/multiboot/multiboot.html

# riscv64

# arm64
