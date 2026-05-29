# 数据模型：system_context 组合根生命周期收口

## 1. `system_context_t`

**角色**：主控运行时组合根的真实运行态对象；对外保持不透明，只能通过正式生命周期接口获取。  
**可见性**：公开头文件前置声明；真实字段只存在于私有头或实现文件。  
**核心字段**：

- 运行态：`wash_program`、`vehicle_type`、`wash_session`、`wash_execution`、`wait_condition`
- 快照与记录：`program_snapshot`、`runtime_snapshot`、`last_transition_record`
- 待处理队列：`pending_triggers[]`、`pending_trigger_count`
- 时钟与序列：`current_time_ms`、`next_session_sequence`、`next_execution_sequence`、`next_wait_condition_sequence`
- 故障与结果：`global_fault_present`、`global_fault_code`、`global_fault_reason`、`last_result_code`、`last_reason_code`
- 装配端口：`sensor_port`、`actuator_port`、`event_logger_port`、`program_repository_port`

**校验规则**：

- 只能在正式 acquire 成功后对外可用
- reset 后运行态字段必须回到干净初始值，但端口绑定继续保留
- release 后外部不得再通过该句柄驱动运行态

## 2. `system_context_pool_slot_t`

**角色**：内部对象池槽位，持有一个预先静态准备好的正式 `system_context_t` 实例及其最小分配元数据。  
**可见性**：仅内部实现可见。  
**建议字段**：

- `bool in_use`：当前槽位是否处于合法占用状态，仅用于句柄合法性校验
- `unsigned int next_free_index`：空闲链表下一个槽位索引
- `system_context_t runtime`：真实组合根运行态对象

**校验规则**：

- acquire 时必须从 `free_head_index` 指向的链表头弹出槽位
- release 时必须把槽位重新挂回空闲链表头
- `in_use == true` 的槽位不得再次被 acquire
- `in_use == false` 的槽位不得被 reset / release / 运行态 API 当作合法正式实例使用

## 3. `system_context_pool_state_t`

**角色**：对象池全局管理状态。  
**可见性**：仅 `system_context.c` 或对应私有实现文件可见。  
**建议字段**：

- `system_context_pool_slot_t slots[POOL_CAPACITY]`
- `unsigned int free_head_index`
- `unsigned int free_count`

**校验规则**：

- `free_head_index` 只能指向当前空闲链表头节点或空链表哨兵值
- `free_count` 必须与空闲链表节点数一致
- acquire 必须执行“头节点弹出 + `free_count--` + `in_use=true`”
- release 必须执行“节点头插回收 + `free_count++` + `in_use=false`”

## 4. `system_context_owner`

**角色**：当前正式持有 `system_context_t *` 的外部使用方，是概念实体，不一定落成具体结构体。  
**典型来源**：

- `main`
- `controller_scheduler_linux`
- 测试辅助入口

**约束**：

- 持有者负责在生命周期结束时调用 release
- 持有者可以 reset 已占用实例，但不能把同一实例转借为新的正式拥有者
- 持有者不得在 release 后继续缓存并复用旧句柄

## 生命周期状态转换

```text
空闲槽位（在空闲链表中）
  └─ acquire 成功：从链表头弹出 -> 已占用

已占用
  ├─ reset -> 已占用（运行态被清空，端口保留）
  ├─ release：头插回空闲链表 -> 空闲槽位
  ├─ 再次 acquire -> 非法，返回资源不可用或无效状态
  └─ 运行态 API -> 合法

已释放旧句柄
  ├─ reset / release / 运行态 API -> 非法，返回显式错误或边界违规
  └─ 不得重新成为合法拥有者
```

## 与现有实现的映射

- 当前 `struct system_context_t` 中的 `owns_storage` 字段在新模型下应被移除。
- 当前 `reset_runtime_state()` 仍可作为“清空运行态”的核心逻辑来源，但需要从“堆/栈拥有权判断”中剥离。
- 当前 `main_loop_*()`、`process_formal_command_*()`、`controller_scheduler_*()` 等仍围绕 `system_context_t *` 工作，但句柄来源改为正式 acquire。
- 对象池分配逻辑应围绕空闲链表头操作实现，`in_use` 只作为正式句柄合法性检查，不额外承担空闲节点查找职责。
