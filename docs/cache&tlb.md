# x86下的cache
缺少相应的指令
但是intel手册中有将页面控制cache的部分

对于cache和内存的一致性
分为如下情况
Strong uncachable（UC）非常强的顺序保证
uncachable（UC-）在PAT中设置
Write combination（WC）
Write Through（WT）
Write Back（WB）
Write Protected（WP）读可以cache，写不能
具体看表格11-2吧，不过，这些都挺好理解的不是吗

CR0 bit30 CD标志位
如果CD被清零，那么整个系统都是cache的，只不过个别的会因为其他比如页表设置等等来启用非cache
bit29 NW标志位
配合CD一起的，用于控制write的行为，比如都清零，会使用write back，但是总而言之，为了性能，这两位都清零即可。无需考虑其他



# x86下的barrier
sfence，在读指令之前，可以强制从主存加载数据
lfence，写指令之后插入写屏障，可以强制让写入缓存的数据写回主存
mfence，上面两者的综合

# x86下的tlb
invlpg指令
当然切换CR3会使得整个TLB失效

# x86下的缓存发现
可以使用eax=2的cpuid，具体见CPUID头文件里面的链接，里面有更为详细的表格

# aarch64的barrier
对于aarch64的barrier
划分为四种域
1、Non-shared
只有当前CPU访问
2、Inner shareable
一个CPU可以属于一个Inner sharable，然后一个域包含多个CPU，
3、Outer Shareable
可以访问其他域的内容
4、Full System
全局共享

然后还有三种
1、Load-Load，Load-Store
所有之前的load操作一定在这之前完成，其后的ld和st操作一定在这之后，但是在这之前的st操作可能在这之后完成
2、Store-Store
所有之前的st操作一定在这之前完成，所有之后的st一定在这之后，但是没有任何对于ld操作的限制
3、Any-Any
所有的都保证

从而导致了dmb和dsb指令的参数可以分为如下

outer作用域的
OSHLD	load-load，load-store
OSHST	store-store
OSH		any-any

Non-shared作用域的
NSHLD
NSHST
NSH
同上

inner作用域的
ISHLD
ISHST
ISH
同上

全系统的
LD	load-load，load-store
ST	store-store
SY	any-any

除此之外，就不介绍LDAR和STLR指令了

# aarch64的cache操作

必须提及的是PoC和PoU，前者是系统，后者是处理器的角度。

这里面也包括两种，指令缓存操作IC以及数据缓存操作DC

指令缓存操作：
都以IC开头
IALLU，所有的指令缓存失效
IALLUIS，内部共享域的所有指令缓存，对于SMP而言
IVAU，根据虚拟地址让特定行的指令缓存失效

数据缓存操作：
ZVA，把所有缓存块都清零，带有一个Xt寄存器，表示需要使用到的寄存器。
需要设置DCZID_EL0
DZP bit4：0表示允许ZVA指令，1表示禁止，需要开启（清零）
BS bit[3:0]表示需要清零的块数量，最大为9，也就是2KB

IVAC，根据address无效，到PoC
ISW，根据set/Way无效
bit[31:4]表示SetWay
bit[3:1]表示level

CSW，是clean，而不是invalid，清理setway
CVAC，根据address清理到PoU

CIVAC， clean and invalid 根据address，到PoC，应该是最强的了
CISW， Clean and invalid，根据setway

CVAC，清理到PoC
CVAU，清理到PoU



# aarch64的tlb操作
TLBI无效化TLB中的条目，TLBI后续的指令还是比较多的，这个真的是比较多

TLBI ALLE1：可以带一个参数xt，但是他必须是0b11111
TLBI ALLE1IS：inner shared版本
TLBI ALLE1OS：outer shared版本
同样的E2和E3也有相应的版本

TLBI ASIDE1：同样可以带一个参数，该寄存器的高16位表示asid
inner 和outer shared版本相同
无E2和E3

TLBI IPAS2E1：不太懂，需要enable EL2，估计没啥用
存在inner 和outer版本

TLBI IPAS2LE1：不懂，其他同上

TLBI RIPAS2E1：R表示range，其他同上及上上

TLBI RVAAE1：带一个参数，对所有的asid都生效
bit [47:46]表示TG，0b01表示4K页面，0b10表示16K页面，0b11表示64K页面
bit [45:44]表示SCALE，计算的指数
bit [43:49]表示NUM，计算数量的基数
bit [38:37]TTL，表示无效的缓存的level
bit [36:0]表示开始的base addr

TLBI RVAAE1IS和OS
同上

TLBI RVAALE1：参数是一样的，多出来的L表示Last level

TLBI RVAE1：参数基本和上面相同，但是bit [63:48]表示为ASID，用于指定ASID
包括inner和outer，
同样还有EL2和EL3版本

TLBI RVALE1：多的L表示last level
当然带inner和outer
存在EL2和EL3版本

TLBI VAAE1：不再是range，而是一个虚拟地址，只无效一个条目
对于参数，
TTL，bit[47:44]
VA，bit[43:0]表示VA[55:12]
存在inner和outer版本

带L的last level一样

TLBI VAE1：和之前一样bit [63:48]表示为ASID，指定ASID
包括inner和outer
还有EL2和EL3版本

带L的last level一样

TLBI VMALLE1：根据VMID进行无效，xt参数必须是0b11111
带有inner和outer版本
后面带VM的均是虚拟机情况下的，rendezvos目前并不涉及虚拟化相关内容，在此不做展开。

# aarch64的缓存发现
