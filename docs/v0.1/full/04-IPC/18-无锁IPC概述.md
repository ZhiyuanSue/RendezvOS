# 无锁队列与 EBR 设计

v0.1 · 2026-10-02

本篇覆盖：`include/common/dsa/ms_queue.h`、`include/common/taggedptr.h`；以及 IPC / message 路径上 **如何使用** EBR（`kernel/task/ebr.c`）。`ipc.c` / `message.c` 的对象与收发契约分别以 Port 篇、收发篇为准——本篇只写「在队列算法约束下对他们的影响」。

EBR 机制与「谁推进 reclaim」见 `03-任务与调度/17-EBR与线程资源回收.md`：`schedule()` 每次切线程会调 `ebr_try_reclaim()`；线程 teardown 同步释放 `Thread_Base` 本身，但排空其 send/recv 队列时会对 `Message_t` / `Ipc_Request` 走 `ebr_retire_ref`。Port 对象相关见 `19`；会合收发见 `20`；tagged pointer 位布局亦见 `taggedptr.h`。

### 在 04-IPC 章里怎么读

| 读什么 | 钉什么 |
|--------|--------|
| 本篇 **§1.1–1.2** | 锁与IPC 同构、基于IPC的混合内核和微内核框架概述；§1.2 末可转 `19`/`20`（如果不要了解无锁队列实现细节的话） |
| 本篇 **§1.3–1.8** | MSQ 约束与对原始算法的改造：假出队、Msg 拆开、ABA、单状态、外置节点内存等问题和对MSQ算法的影响 |
| **`19` → `20`** | Port / Message 对象 → 基于无锁队列的IPC match与transfer行为等 |
| **`21` / `22`** | kmsg消息封装、Port的权限管理hooks等 |
| 本篇 **§2 起** | MSQ / tagged ptr / EBR 算法与 API |

---

## 1. 概述

IPC机制是混合内核或者微内核的核心机制。当然在某些情况下实质上是ITC（inter-thread communication），不过正如seL4的文档中的处理一样，为了更好的便于理解，我们还是用IPC这个词。

基于此，我们这里的IPC机制，定义为两个执行流之间互相通信以进行协作的机制。

在 §1.1 我们会介绍锁并发与基于IPC并发的同构性，并解释了，我们试图使用无锁IPC来规避内核中多执行流对一个临界区的锁并发的核心思路。

基于这种同构性，我们在 §1.2 讨论了，基于无锁 IPC 方案来解构锁，同时使用单执行流来循环执行临界区，使用无锁IPC方案进行通信协作，这样来实现混合内核的思路。其最终的目的是，为了让兼容层实现的代码，可以以单执行流的方式实现，以规避复杂的内核并发实现问题。

RendezvOS v0.1 的并发无锁IPC核心是 **Michael–Scott 无锁 MSQ**（Maged M. Michael 与 Michael L. Scott 提出的多生产者多消费者无锁队列算法，`ms_queue_t`），同时添加 **tagged pointer** 在指针里携带 small tag 来解决ABA问题和带状态的 port 会合问题，再配 **EBR**（Epoch-Based Reclamation，基于世代的延迟回收）延迟释放节点，避免「队列上还能看见、堆里已经 free」的 UAF（Use-After-Free，释放后使用）。

另外，和典型实现（例如我在网上搜到的：  https://www.cnblogs.com/lijingcheng/p/4454848.html  ）不同的是，我们也需要考虑节点的内存分配问题，使用外置而非MSQ的 enqueue/dequeue 申请分配内存节点的分配策略，从而大幅度减少 enqueue/dequeue 的不确定性问题。

当然，本篇的主要内容，也是来讲述我们是如何做MSQ的实现及标准算法上的改造的。

在 §1.3 到 §1.8 的剩余几个小节，概述了细节实现上会遇到的一些问题，以及这些问题是如何影响我们最终对基础数据结构 MSQ 的一些要求的。

MSQ 用在 port 的 `thread_queue`以及每线程 send / recv 消息队列，除此之外，也在kmalloc 跨核 free 中使用（见 kmalloc 篇）。

### 1.1 锁并发与基于IPC并发的同构性——同步没有消失，只是搬家了

宏内核里当两个核心都要访问同一块共享资源时，一个核进临界区，另一核自旋，一旦核心数量增加，各核轮流等待，总等待量会急剧上升。

但是实际上，我们可以把它变成一个IPC的方案。

![锁同步 vs IPC 同步](figures/lock-vs-ipc.png)

如图所示：

左图「使用锁同步」：两核各自跑非临界区（蓝），争用时A核进临界区（橙），另一核B **spin**（白）直到A核心释放临界区的锁。如果这过于抽象，可以想象内核里面，两个核心同时试图使用网卡发送消息，总有一个核心，比如A，先抢到锁，然后去临界区发出它想要发的包，然后另一个核心B，等A发完之后，再使用网卡去发他的包。

右图「使用 IPC 同步」：临界区代码可以写成「单线程循环收消息」的线程（橙框）；其它线程只发 IPC。同样的，按照我们前面的场景，网卡发消息不再是内核中临界区的一段代码，而是变成一个线程（一个server），循环等待是否有需要发送的消息发给他；A和B两个核心上各有一个线程，同时试图用网卡发消息，此时只给网卡server线程发消息，一个核心会执行网卡server线程并让他发包，而另一个核心则可以切换走执行或者继续执行，无需spin。

从这个角度来说，锁和ipc的方案，在并发的处理上，是同构的。

但多核同步之间的问题**没有消失**：两个客户端同时向 server 投递请求，**谁先谁后、会不会丢失配对**，全变成 **IPC 框架自己的同步问题**。

若 IPC 还用一把大锁做会合，核数增多时争用情况和宏内核大锁差不多，而且因为还涉及到上下文切换以及调度延迟，性能肯定更糟，但是如果使用无锁ipc，则可以完全去掉这里的锁问题。RendezvOS 的选择是 port 会合与消息队列走 **无锁 MSQ**这样的方案。

**基本性能问题探讨：**
1. 核少时，精心设计的细粒度锁往往更便宜（切换比获取锁贵）
2. 核变多、临界区都线程化之后，才能在整体吞吐量上有所优势。这里临界区反而建议较大，而非细粒度锁的模式，以减少上下文切换和调度成本，让更多同类操作可以在一个server线程中串行执行。
3. N 个核心争同一把锁，最坏等待是按照 **O(N²)** 来累计的，第一个拿到锁的核心几乎没有成本，第二个拿到的核心就需要等待第一个核心执行完临界区，第三个就需要等待前两个执行完临界区，以此类推，最坏情况下实际上，就是O(N²)。
4. 「使用单执行流 server 循环跑临界区 + 无锁 IPC 」每个核心承担一次IPC发消息，加上切换的成本，总成本更接近 **O(N)**——争用从「自旋消耗 CPU」变成「排队进 server」。

更进一步的，如果有某个核心专门执行上述的server线程，那么其他核心执行的时候，就可以通过IPC的方式直接快速执行，无需等待。而这也是 Shenango [NSDI 19']等论文的方向（Shenango用一个核心专门做IO，称为IOkernel，这和我们的方向其实是类似的）。

对比其他的微内核的IPC：有的系统用大内核锁换验证简单（seL4）；有的假设 SPSC（Single-Producer Single-Consumer，单生产者单消费者）环形缓冲区。但是我们实现的是混合内核里常见的 **MPMC（Multi-Producer Multi-Consumer，多生产者多消费者）会合**（多客户端对一个 server port）。

 Port 先会合然后进行消息传输的方案，见 `19` §1。

### 1.2 混合内核与微内核在 IPC 轴上同构

![多内核栈：混合内核 vs 微内核](figures/hybird-vs-micro.png)

在阐明了我们使用无锁IPC，可以实现与上锁方案同构之后，我们还需要阐明，我们基于此的内核架构方案。

我们的设计基于如下一个事实：用户线程进内核之后有自己的内核栈，内核态的延伸执行流也可以当一个「能收发 IPC 的执行流」看。所以我们可以让 core 侧的视角，统一的将只在内核态运行的内核线程，与具有用户态的用户线程，都当做某种内核线程来看待，具有一种统一的抽象视角。

这在实际上延伸出来混合内核和微内核的差异

左图：就是我们试图实现的混合内核。绿为纯内核 server 执行流，紫为用户线程陷入内核后的内核执行流延伸；他们在core层的视角都是一致的一个内核态执行流，区别是有无用户态延伸罢了。

右图：微内核方案。在这种模式下，我们原本在混合内核方案中，实现的内核态执行流，大部分工作都下放到用户态执行，在内核态的延伸，都只做简单的消息收发即可。

**理解了上述内核设计之后，本节之后的内容都是关于无锁队列的具体实现问题了，如果更关心基于无锁队列在我们这里如何实现无锁IPC，可以直接看`19`,`20`。**

### 1.3 为何朴素链表做不成无锁队列

然后我们来展开说无锁ipc队列要解决的问题。

以下图一个固定 dummy 的单链表为例，看起来只要把 `dummy→next` 从 A 改成 B 就能出队：

![固定 dummy 单链表](figures/dummy-link-list.png)

并发下却要在**两次非相邻读**之后再写一次：`读 dummy→next(=A)` → `读 A→next(=B)` → `写 dummy→next = B`。竞态一例：

1. T1 读到 `dummy→next = A`
2. T2 先把 A、B 都出队，队列空（`dummy→next = NULL`）
3. T1 仍持有过期的 A，读到 `A→next = B`，再把 `dummy→next` 写成 B

于是 **已被出队的 B「复活」**。无 dummy 时用独立 `head` 指向首元素，矛盾是相同的，也需要改 `head` 指针。

根因：需要**同时修改**两个不相邻的指针（u64/u32，看架构）；硬件 CAS 一次只能动一个u64/u32，从而导致上述的单链表无法实现无锁。

Michael–Scott 队列用「假出队 + 帮助推进 tail」绕开了这个双指针硬性需求。我找到一个相关的数据结构和算法的示例实现 ： https://www.cnblogs.com/lijingcheng/p/4454848.html  。

### 1.4 MSQ 队列具有的性质

msqueue有以下几个特性（下图用于示例），这些特性深刻的影响了本文后续代码开发中的诸多细节。

![MSQ 空队列与假出队](figures/ms-queue-feature.png)

1. msqueue本身是一个多发送者多接收者的一个无锁队列。

2. msqueue本身具有一个dummy节点，所以只有一个dummy节点的时候，他就意味着队列空，此时队列的head和tail指针都指向dummy，如图上半图所示。

3. msqueue 的出队过程，是把旧的 dummy 节点出队，让原本跟在 dummy 节点后的节点（图中是 A；这个节点必须存在，否则只剩下一个 dummy，那么出队失败）成为新的 dummy 节点。虽然逻辑上这个新的 dummy 节点已经出队了，但是实质上，新的 dummy 必须还「放」在队列中。同时，我们难以确定这个新的 dummy 节点的后继指针 `next` 是否被其他线程所使用，所以直接把它当成「已经被弹出、可以整节点挪走」是不可行的——这就是**假出队**问题。

4. 辅助 tail 更新。无论是 enqueue 还是 dequeue，在失败的时候都试图推进 tail 节点。


### 1.5 假出队迫使 `Msg` / `Msg_Data` 拆开

![ipc_transfer_message：新建 Message_t、共享 Msg_Data](figures/ipc_transfer_message.png)

假出队问题，迫使我采用上图所示的这个数据结构。

当初我踩入这个坑的原理是这样的：如果直接尝试在发送者的发送队列中弹出一个 msg 节点，然后塞入接收队列的接收节点，因为假出队问题，其实弹出的节点仍在队列中排着，它的后继指向着发送队列的真正第一个节点，直接塞入接收队列，就会导致发送队列被破坏掉。

而如果直接进行复制，在数据块很大的时候，是成本很高的。

所以如图，我们使用`Message_t`在队列中排队，在转移到接收队列的时候我们进行复制。

而让`Msg_Data_t`用于消息数据的管理。这两个复制的`Message_t`均通过指针指向同一个`Msg_Data_t`，从而避免大量数据的拷贝。

另外，在图中`Msg_Data_t`和真正的数据内容的分离，则是因为，为了能够正确的管理生命周期并释放，因此我们在`Msg_Data_t`这里至少需要维护一个refcount。然而，当我给定一个任意的内核缓冲区的数据，我们能轻易的在前面加上一个refcount字段然后封装成一个表示msg的数据结构吗？这恐怕是困难的，它可能是紧密的跟前后的其他数据结合在一起。因此，我们只有一个办法，继续分离并用指针指向数据内容，这是唯一的办法。为此我们约定`Msg_Data_t`和具体数据内容一一对应。

这些MSQ的特性，尤其是假出队问题，导致的我们消息在两个队列中传递的时候，必须使用复制而非直接节点换队列的模式。是MSQ实现IPC需要考虑的典型问题。

### 1.6 ABA 与 tagged pointer

ABA问题是无锁算法中必须要处理的问题，tagged ptr则是ABA问题的典型处理方式，因此我们缝合了一个该功能进去。主要就用于MSQ的指针域。

ABA问题我这里只简单给个概念，实际上，大可以搜一搜，都是公开的资料。

CAS 只看「字是否仍等于期望值」。典型 ABA：

1. T1 读到某个指针值（某个节点的地址）
2. T2 把该节点出队然后释放，但是，分配器又把同一地址分给新节点再入队（尤其是分配器有缓存的时候，这很正常）
3. T1 的 CAS 仍看见地址 A，以为没人动过，误判为成功

队列里 head/tail/next 都是使用 CAS 的，肯定会遇到这个问题的

那么一个简单的办法就是引入带标签的指针（Tagged Pointer）。其核心思想是将指针与一个单调递增的版本号（标签）捆绑在一起，作为一个原子单元进行操作。即使指针值本身恢复原状，标签也会不同，因此CAS可以检测到中间发生过修改。在64位系统上，由于实际使用的虚拟地址位数通常少于64（如x86-64仅使用低48位，注，实际上应该为47位，因为canonical地址格式的48位用于表明符号扩展位），我们可以利用高16位来存储标签，将指针和标签打包在一个64位整数中。每次修改指针时，同时递增标签，确保每次更新都产生一个唯一的值组合。

本仓库：`tagged_ptr_t` 相关实现在`taggedptr.h`。


### 1.7 单状态无锁队列扩展

先说概念，目标就是，每个节点都带一个n位的状态，我们期望维护一个基于MSQ算法基础上的扩展的队列算法，从而使得队列中的所有节点，全都是这n位能表示的2的n次个状态中的一种。另外，这2的n次个状态必须有1个状态用于描述空的情况。

具体使用场景，则见`19`，我们需要维护一个要么全都是send类型，要么全都是recv类型的，实际上我们还需要一个空状态用于中间转换，所以使用了一个2位的状态。（当然实际上我们实现的MSQ代码，接受0-15位长度的状态嵌入到tagged ptr的tag中，必须留一位用于ABA问题）

细节实现见 §6.2 / §6.3；enqueue_wait / try_match 调用序见 `20`。

### 1.8 enqueue/dequeue外的内存管理

这主要包括两个，一个是分配，一个是释放，前者是单独的在enqueue/dequeue之外的申请并构造，后者主要是refcount，虽然封装在enqueue/dequeue过程中，但是使用传递给refput的函数指针来实现自定义类型的释放，让MSQ算法具有通用性，实际上也是外置，只不过使用了注入hook的方案。

在我找到的[示例链接](https://www.cnblogs.com/lijingcheng/p/4454848.html) 中，一个新的节点的构造，就是传入数据，然后按照一个固定的数据结构大小，分配空间，填入数据。

但是我们必须要考虑到，我们试图实现的是一个通用数据结构，所以我们试图实现的MSQ节点，必须像一个普通内核嵌入式链表节点的模式，把节点嵌入各类数据结构，并通过container_of宏来访问具体的数据结构。

所以这注定了，我们节点的空间分配时间，要在MSQ算法之外。

这带来另一个问题，示例中的节点生命周期，开始于数据传入enqueue并构造节点，结束于出队。然而节点分配时间在MSQ enqueue之前，同时我们也期望在实际算法中，出队之后，还能被继续使用，这导致了，实际我们需要的节点生命周期，长于示例中的 enqueue/dequeue 配对的生命周期。

所以我们不得不加上一个refcount。在构造，入队，出队，结束使用的时候，进行引用计数的管理，从而正确的使用节点。

节点的释放，必须依赖于引用计数，和传入 `ref_put` 的每个节点嵌入的数据结构的析构函数，从而只有在最后一个引用计数释放的时候，才调用析构函数释放数据结构和其中的节点。

在实际的问题中，我们还需要依赖于 EBR （epoch-based reclaimation），这个问题更简单直接粗暴——当我们在msqueue中试图ref get获取引用计数的时候，我们无法保证该节点不是已经释放掉了的，从而访问引用计数本身就会导致问题。核心在于**取指针和根据指针得到的节点来访问修改refcount两件事情，也不是原子的**。复杂的引用计数设计也许能解决问题，但是我觉得直接叠 EBR 机制就可以了。

常见的办法除了 EBR 之外，还有危险指针等其他方法。

在传入的析构函数 `free_func` 通常再进 **EBR retire** 来避免「队列上还能看见、堆里已经 free」的问题；EBR 机制与 `schedule` 推进 reclaim 见篇首与 `17`，算法接合见本篇 §4.4 。

基于以上 §1.3 到 §1.8 的考量，我们实现了对原始的 MSQ 简单算法的扩展和适配。

---

## 2. 目标与边界

**提供：** MSQ + tagged ptr + 与 EBR 的接合约定；支撑「将锁临界区线程化 + 无锁IPC实现协作」的混合内核模型（§1.1–1.2）。介绍假出队、Msg 拆开、单状态、外置节点+refcount（§1.3–1.8）等**设计实现原因**，供 `19`/`20` 阅读理解。

**不做：** Port / Message 对象契约（`19`）；`send_msg` / `recv_msg` / system 投递状态机（`20`）；通用阻塞队列；优先级队列；内核 malloc 层对 EBR 的隐藏（caller 显式 `ebr_retire_ref`）；跨进程队列；形式化验证完备性声明。

权衡：

- MSQ dequeue 会 **移动 dummy 节点** 并 `ref_put` 旧 dummy，故 message 拆成 `Msg_Data_t` + `Message_t`（`19` §4.2）。
- EBR retire 表 overflow 时 **leak**（泄漏，保留节点不回收）而非 UAF（见 EBR 篇）——用可观测泄漏换并发安全上界。
- 无锁正确性依赖 tag / EBR 纪律；写错 `free_func` 或缺 `ebr_enter` 会导致稀有崩溃，框架将难度留给少数原语实现者，而非每个 server 作者。

与相邻子系统：跨核传递的消息 / 对象常在 **A 核 `kallocator` 分配、B 核释放**，堆必须承认所有权（见 kmalloc 篇跨核 free MSQ）。单执行流 server 依赖本篇原语，而不是在业务里自写多核锁协议。

---

## 3. 分层与调用方

**IPC 实现**（`ipc.c`，契约见 `20`）— 只通过 `msq_enqueue` / `msq_dequeue` / `msq_enqueue_check_tail` / `msq_dequeue_check_head` 操作 port 队列；**不**直接 malloc 队列节点（Ipc_Request 在外部分配）。

**Message**（`message.c`，契约见 `19`）— `free_message_ref` → EBR → `free_message_ref_real` 真正销毁。

**任意 MSQ 读者** — 必须在 `ebr_enter()` 与 `ebr_exit()` 之间完成指针遍历（`ms_queue.h` inline 已包裹）。

**集成方** — 一般 **不**直接操作 port `thread_queue`；仅使用 send / recv API。若扩展新无锁结构，须同样配对 EBR 或证明无并发读。

---

## 4. 数据结构与不变量

### 4.1 ms_queue_node_t / ms_queue_t

```c
typedef struct {
        ref_count_t refcount;
        tagged_ptr_t next;
} ms_queue_node_t;

typedef struct Michael_Scott_Queue {
        tagged_ptr_t head;
        tagged_ptr_t tail;
        size_t append_info_bits;  /* tag 可用位数，≤15 */
} ms_queue_t;
```

- 队列 **不**内嵌 payload；用 `container_of` 从 node 得 `Ipc_Request_t` / `Message_t`。
- **Dummy 节点** — `msq_init` 用 caller 提供的空 node 作初始 head / tail；dequeue 成功后旧 dummy 经 `free_func` 释放（可能 EBR）。

### 4.2 tagged pointer

布局见 §1.6。Port 用 tag 低 **2 bit**（`IPC_PORT_APPEND_BITS`）存 `IPC_PORT_STATE_*`；`msq_enqueue_check_tail` 要求 tail append 与期望一致才 enqueue。内核堆对齐保证地址域低位可给 MS 方案腾空间。v0.1 **不**另开 wide-CAS ABA 计数。

### 4.3 refcount 约定

- 入队节点：caller 分配，`ref_init` 或 **`ref_get_not_zero`** 后 enqueue。后者用 CAS 环：仅当 count ≠ 0 才加一——避免「`fetch_dec` 已到 0、即将 free」与「另一核 `ref_get` 又把死对象救活」的竞态。普通 `ref_get` 在 MS 节点上不够安全。
- dequeue 返回数据 node 时已 **increment** refcount；caller `ref_put(..., free_func)`。
- `free_func` 对 IPC / message 通常为 `free_ipc_request` / `free_message_ref` → **EBR retire**。EBR 保护读侧仍可能看见的节点；refcount 保护对象逻辑寿命。

### 4.4 EBR 与 MSQ 协作（摘要）

| 角色 | 动作 |
|------|------|
| MSQ 读者 | `ebr_enter` … 读 next 指针 … `ebr_exit` |
| 节点最后一个 ref_put | `ebr_retire_ref(ref, real_free)` |
| 任意 schedule | `ebr_try_reclaim` 扫描 retire_epoch < safe_epoch |

**safe epoch** — 所有 active CPU 的 `local_epoch` 最小值与 global epoch 的关系见 EBR 篇。本篇不重复公式。

EBR grace period 与 retire/reclaim 时序：

```text
   global epoch:        G0          G1          G2          G3
                       │           │           │           │
   CPU0 local_epoch:   G0 ──enter───────────exit── G2 ──enter──...
   CPU1 local_epoch:   G0 ──enter──exit── G1 ──enter──────────exit──...
                       │           │           │           │
   safe_epoch = min(active local_epoch)
                       │           │           │
   retire 表:        retire@G0 ────► safe≥G1 后可 reclaim
                       │           │
                       │           └─► retire@G1 ────► safe≥G2 后可 reclaim

   关键不变量：节点 retire 时的 epoch G 必须严格小于「所有 CPU 都已离开 G、进入 G+1 之后」
              的 safe_epoch，才能 reclaim——保证任何可能看见该节点的读者都已退出临界区
   overflow：retire 表满时 EBR 选择 leak（保留节点）而非冒险 reclaim → UAF
              （用可观测泄漏换并发安全上界，详见 `17`）
```

---

## 5. 代码对应

| 文件 | 职责 |
|------|------|
| `ms_queue.h` | MSQ 全部 inline 操作 + EBR 包裹 |
| `taggedptr.h` | tag / ptr 打包与 CAS |
| `ebr.c` / `ebr.h` | epoch 与 retire 表 |
| `ipc.c` | port check_head / dequeue、request free |
| `message.c` | message free retire |
| `task_manager.c` | schedule → reclaim |

主要 MSQ 入口：`msq_init`、`msq_enqueue`、`msq_dequeue`、`msq_enqueue_check_tail`、`msq_dequeue_check_head`、`msq_clean_queue`。

---

## 6. 流程

### 6.1 标准 enqueue / dequeue

```text
调用方:  alloc node → msq_enqueue / msq_dequeue（**不必**再自行 ebr_enter；
         两入口 inline 内部已 ebr_enter…ebr_exit）
         → 对返回的数据节点 ref_put(..., free_func)   // 可能进 EBR（旧 dummy 亦 put）
```

概念上「写端只链新节点、读端遍历可能看见已 retire 节点」仍成立；实现上 **enqueue/dequeue 都包了 EBR**，调用方勿双重 enter。

MS 队列 enqueue / dequeue 算法（伪码，省略 EBR 包裹与 tag 推进细节）：

```text
   msq_enqueue(q, node, free_func):
     loop:
       tail = q->tail                       # 读 tail（tagged ptr）
       next = tail->next
       if tail == q->tail:                   # tail 未变
         if next == NULL:                    # tail 是末元
           if CAS(&tail->next, NULL, node):  # 链上 node
             break                           # 成功
         else:                               # tail 落后于 next
           CAS(&q->tail, tail, next)         # 帮助推进 tail（help stale）
       # 否则重试
     CAS(&q->tail, tail, node)              # 推进 tail（tag+step）

   msq_dequeue(q, free_func) -> node | none:
     loop:
       head = q->head; tail = q->tail
       next = head->next                    # head 必是 dummy
       if head == q->head:
         if head == tail:                   # 队列空或 tail 落后
           if next == NULL: return none    # 真·空
           CAS(&q->tail, tail, next)       # 帮助推进 tail
         else:
           value = next->value             # 读首元数据
           if CAS(&q->head, head, next):   # 假出队：head 跳到 next
             free_func(head)               # 旧 dummy 经 EBR retire
             return next                   # next 成为新 dummy（仍挂队）
   注：CAS 均带 tag 比较；tag 不匹配则视为「已被别人改」，重试
        假出队 = 逻辑弹出 value，但 next 节点内存仍挂在队上当新 dummy
```

### 6.2 Port 会合 enqueue（check_tail）

`ipc_port_enqueue_wait` 调用 `msq_enqueue_check_tail(&port->thread_queue, &req->ms_queue_node, my_ipc_state, tp_new(NULL, expected_ipc_state), free_ipc_request)`：

- 仅当 tail tag 为 `expected_ipc_state`（EMPTY 或同侧 SEND / RECV）才链接；
- 成功后 tail tag 更新为 `my_ipc_state`；
- 失败 `-E_REND_AGAIN` → 调用方重试（并发侧可能已 match）。

### 6.3 try_match（check_head）

`ipc_port_try_match` → `msq_dequeue_check_head(..., MSQ_CHECK_FIELD_APPEND, tp_new(NULL, target_ipc_state), NULL)`：

- 校验 head 后首个真实节点的 append tag 为 **对侧** 状态；
- tag 不匹配则 continue dequeue。
- **IPC 层还要再滤一层：** `ipc_port_try_match` 在 dequeue 之后检查对头线程 status / `port_ptr`，不合格的 request 直接 put 掉再取下一个（见 `20` §6.0）。这不是 MSQ 算法的一部分。

### 6.4 msq_clean_queue（teardown）

`thread_release_owned_resources` 用 `msq_clean_queue(..., free_message_ref)` 排空 thread 队列。注释强调：**不要**对 dummy 二次 `ref_put`；空队列 dequeue 路径已处理 dummy refcount。

---

## 7. 公开 API

本篇涉及的接口分布在：`ms_queue.h`（MSQ 全套 inline）与 `taggedptr.h`。EBR 符号说明与完整约定见 **`17` §7**（本篇只钉与 MSQ 的咬合）。

IPC **不**把 MSQ 再包一层公开 API；上层经 `send_msg` / port 会合间接使用。

### 7.1 编排顺序（调用方须遵守）

| 场景 | 顺序 |
|------|------|
| 建队列 | 分配 dummy node → **`msq_init(q, dummy, append_info_bits)`** |
| 普通入/出队 | 节点 `ref_init` → **`msq_enqueue`** / **`msq_dequeue`** → 对返回节点 **`ref_put(..., free_func)`** |
| Port 单状态会合 | **`msq_enqueue_check_tail`** / **`msq_dequeue_check_head`**（tag=SEND/RECV） |
| 读临界区 | MSQ 内部已 **`ebr_enter`…`ebr_exit`**；节点 last put → **`ebr_retire_ref`**（message/ipc free） |
| teardown 排空 | **`msq_clean_queue(q, true, free_*)`** — **勿**对空队列路径的 dummy 再 put |

### 7.2 MSQ（`ms_queue.h`）

```c
void msq_init(ms_queue_t *q, ms_queue_node_t *dummy, size_t append_info_bits);
void msq_enqueue(ms_queue_t *q, ms_queue_node_t *node,
                 error_t (*free_func)(ref_count_t *));
tagged_ptr_t msq_dequeue(ms_queue_t *q, error_t (*free_func)(ref_count_t *));
error_t msq_enqueue_check_tail(ms_queue_t *q, ms_queue_node_t *node,
                               u64 append_info, tagged_ptr_t expect_tp,
                               error_t (*free_func)(ref_count_t *));
tagged_ptr_t msq_dequeue_check_head(ms_queue_t *q, u64 check_field_mask,
                                    tagged_ptr_t expect_tp,
                                    error_t (*free_func)(ref_count_t *));
void msq_clean_queue(ms_queue_t *q, bool zero_head_tail,
                     error_t (*free_func)(ref_count_t *));
```

| 接口 | 说明 |
|------|------|
| `msq_init` | dummy 作初始 head=tail；`append_info_bits` 钳制 ≤15。 |
| `msq_enqueue` | MS 入队 + EBR 包裹；可帮助推进 stale tail。 |
| `msq_dequeue` | **假出队**：返回数据节点（持一 ref），旧 dummy put；该节点留作新 dummy。空 → none（已 put 唯一 dummy）。 |
| `msq_enqueue_check_tail` | tail append-tag 须匹配 `expect_tp`，否则 `-E_REND_AGAIN`。 |
| `msq_dequeue_check_head` | 对 next 做 PTR/APPEND 检查（`MSQ_CHECK_FIELD_*`）。 |
| `msq_clean_queue` | 循环 dequeue+put；空路径勿二次 put dummy。 |

队列不内嵌 payload：`container_of` → `Message_t` / `Ipc_Request_t`。

### 7.3 tagged pointer（`taggedptr.h`）

```c
tagged_ptr_t tp_new(void *ptr, u16 tag);
void *tp_get_ptr(tagged_ptr_t);
u16 tp_get_tag(tagged_ptr_t);
tagged_ptr_t tp_new_none(void);
bool tp_is_none(tagged_ptr_t);
```

48-bit 地址 + 16-bit tag；`tp_get_ptr` 做 canonical 符号扩展。Port 用 tag 低 **2 bit** 存 `IPC_PORT_STATE_*`。

### 7.4 EBR（交叉引用 `17`）

```c
void ebr_enter(void); void ebr_exit(void);
void ebr_try_reclaim(void);
error_t ebr_retire_ref(ref_count_t *, error_t (*free_func)(ref_count_t *));
```

MSQ 读者路径已包 enter/exit；`free_message_ref` / `free_ipc_request` → retire。`schedule` 推进 reclaim。安全条件 `retire_epoch < safe`、overflow leak：见 `17`。

---

## 8. 多架构

MSQ / EBR 纯 C + atomics，**arch 无关**。`tagged_ptr` 假设指针低位对齐留出 tag 位（内核 heap 对齐满足）。CAS 具体指令随 arch（`lock cmpxchg` / `ldaxr`+`stlxr` 等），语义相同。

---

## 9. 测试

- SMP IPC 测试用例对 MSQ + EBR 施加并发压力。
- `ebr_dump_stats` — 调试 retire / overflow（非正式测试用例）。

```bash
cd core && make ARCH=x86_64 config && make all && make run
```


---

## 10. 限制与后续

- **MPMC 假设** — 依赖 MS 算法与正确 tag；错误 `free_func` 或缺 EBR enter 会导致 rare crash。
- **EBR_RETIRE_SLOTS=512** — 极高 churn 可能 overflow leak（见 EBR 篇）。
- **append_info_bits 上限 15** — tag 空间受限；port 仅用 2 bit。
- **无 hazard pointer 备选** — 全库统一 EBR；其他子系统复用须遵守 enter / exit 纪律。

更形式化正确性论证不在本篇展开；若替换队列实现须同步更新本篇与 EBR / IPC 相关 full。性能、广播、批量绕开会合等**尚未实现**的项见 `20` §10.1 与 `v0.1/evolution/TODO.md`（E2）——**现行设计动机与约定以本篇正文为准**。

---

## 11. 变更记录

- 2026-10-09：§1 按 Maintainer 重写（§1.1–1.8：同构/混合≈微 → MSQ 约束 → 外置节点内存）；开篇补章读序表；与 `19`/`20`/README 交叉引用对齐（单状态钉 §1.7；假出队/Msg 仍 §1.4–1.5）。
- 2026-10-05：§6.3 标明 IPC `try_match` 在 MSQ dequeue 之后还要丢 status/`port_ptr` 不合格的 request（权威在 `20` §6.0）；§10 未实现项回链 `20` §10.1。
- 2026-10-05：任务 1/3/5 精读——口语词与翻译腔清理（涨得很陡→急剧上升、丢配→丢失配对、塌掉→崩塌、故事→情况、付自己那份→承担自己那份、精心细锁→精心设计的细粒度锁、堆起来→累积、烧核→消耗 CPU、环假设 SPSC→假设 SPSC 环形缓冲区、边角插件→边缘组件、饿死→饥饿、硬需求→硬性需求、摘掉→摘除、搬走/挪→迁移、硬加→强行添加、进不来→无法进入、会错→会出错）；为首现缩写补全称/释义（Michael–Scott、EBR、UAF、SPSC、MPMC、AS、leak）；§9「测例」→「测试用例」。
- 2026-10-02：整章表述逻辑：§1 定为 04-IPC 设计框架（五图 + 图→18/19 后果表）；收窄本篇覆盖面（不拥有 ipc/message 契约）；§1.4–1.5 明确牵出单状态 port / Ipc_Request / Msg 拆分。
- 2026-10-02：迁入 lockfree-IPC 五张设计图到 `figures/`，嵌入 §1.1–§1.5（锁 vs IPC、混合≈微、dummy 复活、MSQ 假出队、Msg 壳复制）；§1.6 ABA、§1.7 EMPTY；图为动机真源。
- 2026-10-02：§1.1 补 O(N²) 锁等待 vs O(N) 无锁会合的代价直觉；回灌设计动机——朴素链表复活竞态、MSQ 四性质、ABA/tagged-ptr、EMPTY 枢纽、`ref_get_not_zero`；成稿不引用归档笔记路径。
- 2026-09-27：中文表述润色（母语习惯）；「真源 / 契约」改为「以…为准 / 约定」。
- 2026-09-26：§6.1 纠正「writer 通常不需 ebr_enter」——与 `ms_queue.h` 一致：enqueue/dequeue inline **均**已包裹 enter/exit；调用方勿再包一层。
- 2026-09-26：§7 全文审阅——`ms_queue.h` / `taggedptr.h` Doxygen（假出队、check_tail、EBR 包裹）；写清编排并链 `17`。
- 2026-09-25：语言整理；§6.1 改文字流；§8 点明 CAS 随 arch。
- 2026-08-29：回灌设计动机：同步搬家、混合≈微内核同构、MSQ 假出队→Msg 拆分；去掉审计稿外链。
- 2026-08-27：初稿：MSQ、tag、EBR 配合与三处用法。
- 2026-10-05：最终词句顺畅。
