# Port管理机制实现文档

## 一、概述

Port管理机制是RendezvOS混合内核中IPC系统的核心组件，允许线程通过字符串名称注册和查找消息端口（Message Port），实现线程间的动态通信。

**实现位置**：所有port相关功能（包括基础port操作和发现机制）都实现在 `include/rendezvos/task/port.h` 和 `kernel/task/port.c` 中。

### 1.1 核心功能

- **端口注册**：线程可以将自己的端口注册到全局注册表，供其他线程查找
- **端口查找**：线程可以通过名称查找其他线程的端口
- **端口注销**：线程可以注销自己注册的端口
- **缓存机制**：每个线程维护一个本地端口缓存，减少全局表查找开销
- **并发安全**：使用引用计数和版本号机制防止ABA问题

### 1.2 设计原则

- **单端口设计**：每个线程只暴露一个端口，通过消息类型（`msg_type`）区分不同消息
- **逻辑删除**：注销时只标记为删除，等待引用计数归零后再物理删除
- **两级缓存**：线程级缓存 + 全局注册表，优化查找性能

## 二、API参考

### 2.1 初始化

#### `port_discovery_init()`

初始化全局端口注册表。必须在任何端口操作之前调用。

```c
void port_discovery_init(void);
```

**使用示例**：
```c
// 在系统初始化时调用
port_discovery_init();
```

### 2.2 端口注册

#### `thread_register_port()`

将当前线程的端口注册到全局注册表。

```c
error_t thread_register_port(Thread_Base* thread, const char* name);
```

**参数**：
- `thread`：要注册端口的线程（通常使用 `get_cpu_current_thread()`）
- `name`：端口名称（字符串，最大长度64字符）

**返回值**：
- `REND_SUCCESS`：注册成功
- `-E_IN_PARAM`：参数无效
- `-E_RENDEZVOS`：内存分配失败或系统错误

**说明**：
- 如果线程的 `exposed_port` 为 `NULL`，函数会自动创建一个新的端口
- 如果名称已存在，会覆盖原有注册并递增版本号
- 注册成功后，线程的 `exposed_port_name` 会被设置为注册的名称

**使用示例**：
```c
Thread_Base* self = get_cpu_current_thread();
error_t ret = thread_register_port(self, "file_server");
if (ret != REND_SUCCESS) {
    pr_error("Failed to register port\n");
    return ret;
}
```

### 2.3 端口查找

#### `thread_lookup_port()`

通过名称查找端口。

```c
Message_Port_t* thread_lookup_port(const char* name);
```

**参数**：
- `name`：要查找的端口名称

**返回值**：
- `Message_Port_t*`：找到的端口指针，可用于 `send_msg()` 或 `recv_msg()`
- `NULL`：端口不存在或已注销

**说明**：
- 函数首先检查线程的本地缓存
- 如果缓存未命中，从全局注册表查找并更新缓存
- 返回的端口指针在缓存有效期间一直可用，无需额外引用计数管理
- 如果端口被注销，后续查找会返回 `NULL`

**使用示例**：
```c
Message_Port_t* file_server_port = thread_lookup_port("file_server");
if (!file_server_port) {
    pr_error("File server port not found\n");
    return -E_RENDEZVOS;
}

// 准备消息
Message_t* msg = create_message_with_msg(msgdata);
enqueue_msg_for_send(msg);

// 发送消息
error_t ret = send_msg(file_server_port);
if (ret != REND_SUCCESS) {
    pr_error("Failed to send message\n");
}
```

### 2.4 端口注销

#### `thread_unregister_port()`

注销当前线程注册的端口。

```c
error_t thread_unregister_port(Thread_Base* thread);
```

**参数**：
- `thread`：要注销端口的线程

**返回值**：
- `REND_SUCCESS`：注销成功
- `-E_IN_PARAM`：线程未注册端口

**说明**：
- 执行逻辑删除：端口在全局表中被标记为已注销，但entry不会立即删除
- 等待所有引用该entry的线程释放引用后，entry才会被物理删除
- 注销后，`exposed_port` 不会被释放，线程仍可使用该端口（只是不再对外暴露）
- `exposed_port_name` 会被释放并置为 `NULL`

**使用示例**：
```c
Thread_Base* self = get_cpu_current_thread();
if (thread_unregister_port(self) != REND_SUCCESS) {
    pr_error("Failed to unregister port\n");
}
```

## 三、数据结构

### 3.1 Thread_Base扩展

每个线程控制块（TCB）包含以下字段（定义在 `include/rendezvos/task/tcb.h`）：

```c
struct Thread_Base {
    // ... 其他字段 ...
    
    Message_Port_t* exposed_port;      // 对外暴露的端口（可为NULL）
    char* exposed_port_name;            // 注册时使用的名称（用于注销）
    struct thread_port_cache port_cache; // 已知端口的本地缓存
};
```

### 3.2 端口缓存（带LRU淘汰）

每个线程维护一个固定大小的端口缓存（默认32个条目，定义在 `include/rendezvos/task/port.h`），**使用LRU（Least Recently Used）淘汰策略**：

```c
#define THREAD_MAX_KNOWN_PORTS 32
#define PORT_CACHE_NAME_LEN    64

struct thread_port_cache_entry {
    char name[PORT_CACHE_NAME_LEN];              // 端口名称
    struct port_registry_entry* entry;            // 指向全局注册表entry
    u16 version;                                  // 缓存时的版本号
    u16 lru_counter;                              // LRU: 计数越大表示越久未使用，0表示最近使用
};

struct thread_port_cache {
    struct thread_port_cache_entry entries[THREAD_MAX_KNOWN_PORTS];
    u32 count;                                    // 当前缓存的条目数
    cas_lock_t lock;                              // 保护缓存的锁
};
```

**LRU淘汰机制**（基于计数，无需时间戳）：
- **访问时更新**：每次缓存命中时，将该条目的 `lru_counter` 设为 `0`，其他所有条目的 `lru_counter++`
- **淘汰策略**：当缓存满时，找到 `lru_counter` 最大的条目（最久未使用），淘汰它并放入新条目
- **优势**：
  - 无需读取时间戳，性能更好
  - 只需简单的整数操作（+1和比较）
  - 自动管理缓存，无需手动清理
  - 计数溢出问题：`u16` 最大65535，即使每次访问都+1，也需要65535次访问才会溢出，实际使用中不会出现问题

### 3.3 全局注册表Entry（内部实现）

全局注册表entry结构（定义在 `include/rendezvos/task/port.h`，内部使用）：

```c
struct port_registry_entry {
    struct rb_node rb_node;           // 红黑树节点（按name排序）
    char* name;                       // 端口名称（键）
    Message_Port_t* port;             // 端口指针（注销时为NULL）
    u16 version;                      // 版本号（防止ABA问题）
    ref_count_t refcount;             // 引用计数
    bool registered;                  // 是否已注册
    struct port_registry* registry;   // 所属注册表
};
```

**注意**：这些结构是内部实现细节，用户代码通常不需要直接访问。

## 四、使用流程

### 4.1 服务端线程（注册端口）

```c
void* server_thread(void* arg)
{
    Thread_Base* self = get_cpu_current_thread();
    
    // 1. 注册端口
    if (thread_register_port(self, "my_service") != REND_SUCCESS) {
        pr_error("Failed to register port\n");
        return NULL;
    }
    
    // 2. 接收消息循环
    while (running) {
        if (recv_msg(self->exposed_port) != REND_SUCCESS)
            continue;
            
        Message_t* msg = dequeue_recv_msg();
        if (!msg)
            continue;
            
        // 处理消息
        process_message(msg);
        
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
    }
    
    // 3. 注销端口（可选）
    thread_unregister_port(self);
    
    return NULL;
}
```

### 4.2 客户端线程（查找并使用端口）

```c
void* client_thread(void* arg)
{
    // 1. 查找服务端端口
    Message_Port_t* server_port = thread_lookup_port("my_service");
    if (!server_port) {
        pr_error("Service not found\n");
        return NULL;
    }
    
    // 2. 准备消息
    Msg_Data_t* msgdata = create_message_data(
        MSG_TYPE_REQUEST,
        data_len,
        &data_ptr,
        free_msgdata_ref_default
    );
    Message_t* msg = create_message_with_msg(msgdata);
    ref_put(&msgdata->refcount, free_msgdata_ref_default);
    
    // 3. 发送消息
    enqueue_msg_for_send(msg);
    if (send_msg(server_port) != REND_SUCCESS) {
        pr_error("Failed to send message\n");
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
        return NULL;
    }
    
    return NULL;
}
```

### 4.3 完整示例

参考 `modules/test/port_discovery_test.c` 中的测试代码，展示了完整的注册、查找、通信、注销流程。

## 五、实现细节

### 5.1 红黑树查找

全局注册表使用红黑树（rb_tree）实现，键为端口名称字符串，比较函数为 `strcmp()`。

**查找算法**：
```c
// 伪代码
struct port_registry_entry* search(struct port_registry* registry, const char* name)
{
    struct rb_node* node = registry->root.rb_root;
    while (node) {
        struct port_registry_entry* entry = container_of(node, ...);
        int cmp = strcmp(name, entry->name);
        if (cmp < 0)
            node = node->left_child;
        else if (cmp > 0)
            node = node->right_child;
        else
            return entry;  // 找到
    }
    return NULL;  // 未找到
}
```

### 5.2 逻辑删除机制

注销端口时采用逻辑删除：

1. **注销操作**：
   ```c
   entry->port = NULL;
   entry->registered = false;
   entry->version++;
   ref_put(&entry->refcount, free_port_registry_entry);
   ```

2. **查找时的检查**：
   ```c
   if (!entry->registered || !entry->port)
       return NULL;  // 视为未找到
   ```

3. **物理删除**：
   当 `refcount` 降为0时，`free_port_registry_entry` 被调用，从树中移除entry并释放内存。

### 5.3 缓存验证

线程缓存命中时的验证（无需查全局表）：

```c
// 从缓存中读取 entry 和 version（在锁保护下）
struct port_registry_entry* entry = cache->entries[i].entry;
u16 cached_version = cache->entries[i].version;

// 解锁后验证（entry 指针仍然有效，因为缓存持有引用）
if (entry->registered && 
    entry->port != NULL && 
    entry->version == cached_version) {
    return entry->port;  // 缓存有效
}
// 否则缓存失效，需要重新查找
```

### 5.4 ABA问题防护

通过版本号机制防止ABA问题：

- **版本号递增**：每次注册/注销时，`entry->version++`
- **缓存版本检查**：缓存时保存版本号，使用时比较当前版本号
- **版本不匹配**：如果版本号不匹配，说明端口被注销并重新注册，缓存失效

## 六、并发安全

### 6.1 全局注册表

- **写操作**（注册/注销）：使用 `cas_lock` 保护整个注册表
- **读操作**（查找）：使用 `cas_lock` 保护查找过程，找到entry后增加引用计数再解锁

### 6.2 线程缓存

- 使用 `cas_lock` 保护缓存数组的读写
- 缓存中的 `entry` 指针持有全局注册表entry的引用，确保entry在使用期间不会被释放

### 6.3 引用计数

- **增加引用**：`lookup` 时调用 `ref_get_not_zero()`，确保entry在使用期间不被释放
- **释放引用**：`put_entry` 时调用 `ref_put()`，当引用计数归零时自动释放entry

## 七、性能考虑

### 7.1 缓存命中率

- **缓存大小**：默认32个条目，可根据实际使用情况调整 `THREAD_MAX_KNOWN_PORTS`
- **LRU淘汰**：已实现基于计数的LRU淘汰策略，缓存满时自动淘汰最久未使用的条目
- **计数更新**：每次缓存命中时，被访问条目计数设为0，其他条目计数+1，无需读取时间戳

### 7.2 查找性能

- **缓存命中**：O(n) 线性查找，n通常很小（≤32）
- **缓存未命中**：O(log m) 红黑树查找，m为全局注册的端口数
- **平均情况**：大多数查找命中缓存，性能接近O(1)

### 7.3 优化建议

1. **RCU优化**：全局表查找可以使用RCU，避免写锁阻塞读操作
2. **批量操作**：如果需要查找多个端口，可以考虑批量查找接口
3. **预取**：在已知会使用的场景，可以提前查找并缓存端口

## 八、错误处理

### 8.1 常见错误

| 错误码 | 说明 | 处理建议 |
|--------|------|----------|
| `-E_IN_PARAM` | 参数无效（NULL指针、空名称等） | 检查参数有效性 |
| `-E_RENDEZVOS` | 系统错误（内存分配失败等） | 检查系统资源 |
| `REND_SUCCESS` | 操作成功 | - |

### 8.2 错误处理示例

```c
error_t ret = thread_register_port(self, "my_service");
switch (ret) {
case REND_SUCCESS:
    pr_info("Port registered successfully\n");
    break;
case -E_IN_PARAM:
    pr_error("Invalid parameters\n");
    break;
case -E_RENDEZVOS:
    pr_error("System error: out of memory?\n");
    break;
default:
    pr_error("Unknown error: %d\n", ret);
    break;
}
```

## 九、生命周期管理

### 9.1 端口生命周期

1. **创建**：调用 `create_message_port()` 或由 `thread_register_port()` 自动创建
2. **注册**：调用 `thread_register_port()` 注册到全局表
3. **使用**：其他线程通过 `thread_lookup_port()` 查找并使用
4. **注销**：调用 `thread_unregister_port()` 从全局表注销
5. **销毁**：线程退出时自动清理，或手动调用 `delete_message_port()`

### 9.2 Entry生命周期

1. **创建**：首次注册时创建entry并插入红黑树
2. **引用**：每次 `lookup` 增加引用计数
3. **逻辑删除**：注销时标记为已删除，但entry仍在树中
4. **物理删除**：当引用计数归零时，从树中移除并释放

### 9.3 线程退出时的清理

线程退出时（`del_thread_structure`）会自动：
1. 注销端口（如果已注册）
2. 清空端口缓存
3. 释放 `exposed_port`（如果存在）

## 十、限制与注意事项

### 10.1 端口名称限制

- **最大长度**：64字符（`PORT_CACHE_NAME_LEN`）
- **字符集**：标准C字符串（以 `\0` 结尾）
- **唯一性**：同一名称只能注册一个端口，后续注册会覆盖

### 10.2 缓存限制

- **最大条目数**：32个（`THREAD_MAX_KNOWN_PORTS`）
- **缓存满时**：自动使用LRU淘汰最久未使用的条目（`lru_counter` 最大的），无需返回错误
- **LRU策略**：基于计数机制，无需时间戳，性能更好
- **建议**：如果经常需要访问超过32个端口，可以考虑增加 `THREAD_MAX_KNOWN_PORTS` 的值

### 10.3 线程安全

- **注册/注销**：必须由拥有端口的线程调用
- **查找**：任何线程都可以查找已注册的端口
- **并发查找**：多个线程可以同时查找同一个端口，完全安全

### 10.4 内存管理

- **端口名称**：注册时分配，注销时释放
- **端口对象**：由线程拥有，线程退出时释放
- **注册表entry**：由注册表管理，引用计数归零时释放

## 十一、测试

### 11.1 测试用例

参考 `modules/test/port_discovery_test.c`，包含以下测试场景：

1. **基本注册查找**：注册端口，查找端口，验证找到的端口可用
2. **消息传递**：通过发现的端口发送和接收消息
3. **注销验证**：注销后查找应返回 `NULL`

### 11.2 运行测试

```bash
# 在系统启动后，测试框架会自动运行 port_discovery_test
# 或手动调用
int port_discovery_test(void);
```

## 十二、未来改进

### 12.1 计划中的优化

1. **RCU支持**：使用RCU优化全局表查找，减少锁竞争
2. **批量查找**：支持一次查找多个端口
3. **缓存统计**：添加缓存命中率统计，便于性能分析

### 12.2 可能的扩展

1. **多端口支持**：如果单端口不够用，可以扩展为每个线程支持多个端口
2. **Hash表实现**：如果端口数量很大，可以考虑用hash表替代红黑树
3. **权限控制**：添加端口访问权限控制机制

## 十三、相关文档

- [无锁IPC实现](./lockfree-ipc.md)：底层IPC机制说明
- [测试代码](../modules/test/port_discovery_test.c)：完整的使用示例
- [源代码](../include/rendezvos/task/port.h)：API定义
- [实现代码](../kernel/task/port.c)：完整实现

## 十四、常见问题

### Q1: 端口查找返回NULL怎么办？

**A**: 可能的原因：
1. 端口未注册
2. 端口已注销
3. 名称拼写错误

**解决**：检查端口名称是否正确，确认服务端线程已注册端口。

### Q2: 缓存满了怎么办？

**A**: 已实现LRU淘汰机制，缓存满时会自动淘汰最久未使用的条目：
1. 系统会自动找到 `lru_counter` 最大的条目（最久未使用）
2. 释放该条目的引用计数
3. 将新条目放入该位置，并设置 `lru_counter = 0`
4. 其他所有条目的 `lru_counter++`
5. 无需手动处理，完全自动化

如果确实需要更多缓存空间，可以增加 `THREAD_MAX_KNOWN_PORTS` 的值。

### Q3: 端口注销后还能使用吗？

**A**: 注销后，端口在全局表中被标记为已删除，其他线程无法再查找。但拥有该端口的线程仍可以使用 `exposed_port` 进行通信（只是不再对外暴露）。

### Q4: 如何知道端口是否有效？

**A**: `thread_lookup_port()` 返回非NULL表示端口有效。如果返回NULL，说明端口不存在或已注销。

### Q5: 版本号的作用是什么？

**A**: 版本号用于防止ABA问题。如果端口被注销后重新注册，版本号会递增，缓存的旧版本会被识别为无效。
