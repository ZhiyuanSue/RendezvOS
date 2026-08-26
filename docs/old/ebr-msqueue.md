# RendezvOS 中的 EBR（ms_queue / IPC）

> 说明：**本文档由 AI（助手）根据当前代码状态生成与整理，不是由仓库作者本人手写。**

本文说明当前 EBR（Epoch-Based Reclamation，基于代际的延迟回收）在
`ms_queue`、`ipc`、`message` 路径中的接入方式、语义和边界。

## 1. 为什么这里需要 EBR

`ms_queue` 是无锁队列，核心路径会并发遍历 `head/tail/next` 指针。  
即使后续通过 `ref_get_not_zero()` 获取到引用计数，竞争窗口中仍可能先读到“即将被释放”的陈旧指针。

因此，对于可能被无锁读者并发观察到的节点，不能在 `ref_put` 归零时立即释放，而是先“退休（retire）”，等到安全时机再真正释放。

## 2. 读侧临界区映射

在以下队列操作中，已接入 EBR 读侧进入/退出：

- `msq_enqueue()`
- `msq_dequeue()`
- `msq_enqueue_check_tail()`
- `msq_dequeue_check_head()`

对应规则：

- 进入无锁指针遍历/CAS 循环前调用 `ebr_enter()`
- 所有返回路径保证调用 `ebr_exit()`

语义上表示：“当前 CPU 可能仍在解引用 epoch=E 的队列节点”。

## 3. 退休路径映射

以下释放回调已从“直接释放”改为“先退休，再延迟释放”：

- `free_message_ref()` -> `ebr_retire_ref(..., free_message_ref_real)`
- `free_ipc_request()` -> `ebr_retire_ref(..., free_ipc_request_real)`

其中 `*_real` 保留原本释放逻辑，EBR 只负责“何时可以安全执行”。

## 4. 回收判定条件

当前最小实现维护了：

- 全局 epoch
- 每 CPU active 标志
- 每 CPU local epoch
- 每 CPU retired slots

若某条记录的 `retire_epoch = R`，仅当“所有 active CPU 的 local epoch 都大于 `R`”时才允许回收。

## 5. 当前范围与限制

这是一个面向当前 IPC/ms_queue 场景的最小 EBR：

- 重点覆盖 `ms_queue` 节点在 IPC/message 路径上的并发释放风险
- retire 槽位有上限（`EBR_RETIRE_SLOTS`）
- 溢出时当前策略是记录并丢弃本次回收机会（避免不安全的立即 free）
- 不替代 IPC 中所有 refcount 语义，而是与 refcount 组合使用

### 5.1 当前实测结论（x86_64，4核）

在当前仓库测试负载下（`smp ms queue` 各测试 + `smp ipc test`），观察到：

- retire 压力很低
- 典型 `ebr_dump_stats()`：`peak=1`、`retire=2`、`overflow=0`
- `EBR_RETIRE_SLOTS=512` 明显富余

因此，对当前负载模型而言：

- `overflow` 不应视为常态
- 若出现 `overflow`，优先按“实现回归/路径不平衡/工作负载改变”排查

## 6. 水位统计与宏开关

当前相关编译期宏位于 `include/rendezvos/task/ebr.h`：

- `EBR_ENABLE_WATERMARK`：是否启用水位统计更新（默认可关闭）
- `EBR_ENABLE_WATERMARK_LOG`：是否启用水位跨档日志
- `EBR_RETIRE_SLOTS`：每 CPU retire 槽位上限（默认 512）

建议：

- 日常运行：可关闭水位日志，保留核心 EBR 语义
- 定向调试：临时开启水位统计/日志，再用 `ebr_dump_stats()` 观察

## 7. 当前默认建议

- 默认保持 `EBR_RETIRE_SLOTS=512`
- 正常回归中若出现 `overflow`，先按 bug 调查，不先扩槽位
- 仅在负载模型变化明显时，再做容量重新评估

## 8. 使用建议（实践）

- 无锁指针遍历必须完整包在 `ebr_enter/ebr_exit` 内
- 可能被无锁读者并发观察到的对象，使用 `ebr_retire_ref` 而不是直接释放
- 仅对“不会暴露给无锁共享指针”的对象保留直接 free
