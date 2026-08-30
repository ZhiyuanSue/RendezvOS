# PSCI 与处理器电源（aarch64）

v0.1 · 2026-08-29

本篇覆盖：`arch/aarch64/psci/*`（**不是** `modules/psci`）、以及与 SMP `cpu_on`、**powerd / `arch_shutdown`** 的边界。

DTB 查找见 DTB 篇；SMP 串行启核见拓扑篇；关机 IPC/opcodes 见 kmsg + 模块初始化 / 错误码·panic 篇（本篇只钉「终点 = PSCI system_off」）。

---

## 1. 概述

aarch64 AP 上电与关机走固件 **SMCCC**（SMC/HVC）。内核绑 method + 标准 function id 包装。

PSCI 懂固件；powerd 只懂「谁请求关机」——两者解耦。

```text
优雅关机：request_poweroff → port "powerd" → SHUTDOWN kmsg
  → powerd_thread → kernel_halt → arch_shutdown → psci_func.system_off()
panic：直接 arch_shutdown，不经 powerd
REBOOT opcode：现多只打 log；arch_reset() 空
```

---

## 2. 目标与边界

**提供：** `psci_init`；`cpu_on/off/suspend`；`system_off/reset` 包装；SMC/HVC 桩。

**不做：** 从 DT **单元格**读 function id（属性**存在性**检查后挂**硬编码** id）；完整 1.x 全家桶；把 powerd 协议写进本篇正文。

---

## 3. 分层与调用方

| 调用方 | 用法 |
|--------|------|
| `arch_start_platform` | `psci_init()` |
| `arch_start_smp` | `psci_func.cpu_on(affinity, PHY(ap_entry), context=逻辑id)` |
| powerd / halt | `system_off` |
| panic | `arch_shutdown` |

`psci_func.enable`：**smp.c 不检查**——`cpu_on` 空指针即可炸（启动篇已警告）。

---

## 4. 数据结构与不变量

- DTB：`compatible` 前缀 `arm,psci`（可命中 `-0.2`）；`method` = `smc|hvc`。  
- 若缺 `migrate` / `cpu_on` / `cpu_off` / `cpu_suspend` 等**属性存在性** → print 并 return（**不**读单元格值）——依赖 QEMU 旧式 DT 常带这些属性。  
- `cpu_on`：`reg`=MPIDR affinity；`context_id`=稠密逻辑 id（与 x86「APIC=下标」不同）。

---

## 5. 代码对应

| 路径 | 职责 |
|------|------|
| `arch/aarch64/psci/` | init、SMC/HVC、包装函数 |
| `arch/.../smp` / start_arch | cpu_on 循环 |
| powerd（kernel/server） | IPC 关机策略 |

---

## 6. 流程

### 6.1 `psci_init`

找节点 → method → `enable=true` → 绑 version/off/reset → 检查 cpu_* 属性存在 → 挂硬编码 id 包装。

### 6.2 启核

见拓扑篇：串行 `cpu_on` + wait `CPU_STATE`。`enable-method` 必须 `"psci"`（拒绝 spin-table）。

### 6.3 关机

powerd（BSP 门控 init）→ `arch_shutdown` → `system_off`。reboot 路径未齐。

---

## 7. 公开 API

以 `psci_func` 函数指针表与 `psci_init` 为准。上层关机走 `rendezvos_request_poweroff` 一类 IPC，勿直接乱调 `cpu_off`。

---

## 8. 多架构

仅 aarch64。x86 关机不经 PSCI。

---

## 9. 测试

间接：SMP + shutdown。本篇未复测。

---

## 10. 限制与后续

- DT 属性存在性脆弱；标准 0.2 可能无 `cpu_on` 属性单元格。  
- smp 不查 `enable`。  
- reboot/`arch_reset` 空。  
- powerd 协议细节回链其他篇。

---

## 11. 变更记录

- 2026-08-29：整篇重做——arch 路径；属性 vs 硬编码 id；powerd 边界；smp 不查 enable；与拓扑 id 模型。
- 2026-08-27：初稿。
