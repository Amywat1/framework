# 数据模型：主控骨架边界收紧

本特性的核心不是引入新业务实体，而是把现有运行期对象重新按“共享事实、拥有者、只读视图、最小依赖切片”四种角色划清边界。下面的数据模型以当前代码中的结构体和服务参数对象为基础，定义谁拥有什么、谁可以改什么、谁只能读取什么。

## 1. 组合根（`system_context_t`）

**说明**：应用层长期持有的运行期总容器，负责聚合共享状态、端口、计数器和运行时对象，但不是领域层直接依赖的业务对象。

| 字段分组 | 内容 | 说明 |
|------|------|------|
| 程序与车辆 | `wash_program`、`vehicle_type` | 当前装载的程序与车辆上下文 |
| 运行对象 | `wash_session`、`wash_execution`、`wait_condition`、`program_snapshot`、`runtime_snapshot` | 由不同拥有者解释的运行时对象 |
| 结果投影 | `last_transition_record`、`last_result_code`、`last_reason_code` | 对外暴露最近一次处理结果 |
| 调度状态 | `pending_triggers[]`、`pending_trigger_count`、`current_time_ms` | 主循环拥有的时间与触发队列 |
| 标识计数器 | `next_session_sequence`、`next_execution_sequence`、`next_wait_condition_sequence` | 主入口和领域服务生成标识时使用 |
| 全局故障 | `global_fault_present`、`global_fault_code`、`global_fault_reason` | 空闲阻断和显式故障状态 |
| 端口 | `sensor_port`、`actuator_port`、`event_logger_port`、`program_repository_port` | 六边形边界对外端口 |

**关系**：
- `system_context_t` 聚合所有运行期对象，但不拥有所有对象的业务解释权。
- `system_context_t` 只能在应用层、主循环和结果投影器中直接出现。

**校验规则**：
- 只有真正跨对象共享、且需要统一解释的事实才允许进入该结构。
- 临时计算结果、单次查询缓存、局部流程标记不得默认塞入该结构。
- 新增共享字段时必须同时定义拥有者、合法写入口和对外语义。

## 2. 触发队列状态（`pending_triggers[]` + `pending_trigger_count`）

**说明**：表示主循环中尚未处理的触发事件集合，是调度事实而不是领域业务状态。

| 字段 | 类型 | 说明 |
|------|------|------|
| pending_triggers | 固定数组 `wash_trigger_event_t[]` | 待处理触发事件 |
| pending_trigger_count | 无符号整数 | 当前排队数量 |

**拥有者**：`main_loop_submit_trigger()`、`enqueue_timeout_if_needed()`、`remove_pending_trigger_at()`  
**只读方**：状态展示、正式命令入口的只读检查、调试辅助路径  

**校验规则**：
- 非主循环路径不得绕过正式入口直接改写队列内容或计数。
- 超时触发只能由主循环依据 `wait_condition` 自动注入。
- 队列满时必须返回明确失败，而不是静默丢弃或覆盖。

## 3. 运行时钟状态（`current_time_ms`）

**说明**：表示主控当前运行时间事实，供等待、超时和结果记录统一引用。

| 字段 | 类型 | 说明 |
|------|------|------|
| current_time_ms | `unsigned long` | 当前主循环时间 |

**拥有者**：`main_loop_advance_time()`  
**读取方**：应用入口、结果投影器、领域服务参数切片、超时逻辑

**校验规则**：
- 只有主循环可以推进时钟。
- 业务服务只能消费时间值，不得直接回写。

## 4. 全局故障状态（`global_fault_*`）

**说明**：表示主控当前是否处于全局故障阻断状态，以及对应故障码和原因。

| 字段 | 类型 | 说明 |
|------|------|------|
| global_fault_present | bool | 当前是否存在全局故障 |
| global_fault_code | 字符串 | 故障标识 |
| global_fault_reason | 字符串 | 故障原因 |

**拥有者**：`process_wash_trigger_execute()` 内的故障处理路径  
**读取方**：状态查询、正式命令展示、启动前检查

**状态转换**：
1. `CLEAR`：无全局故障
2. `ACTIVE`：收到正式故障记录后进入阻断状态
3. `CLEAR_REQUESTED`：仅允许通过正式清除路径返回 `CLEAR`

**校验规则**：
- 非正式路径不得直接设置或清除全局故障。
- 查询、适配层和领域服务只能返回故障事实或建议，不得自行改写全局故障字段。

## 5. 最近结果投影状态（`last_result_code`、`last_reason_code`、`last_transition_record`）

**说明**：表示最近一次正式处理对外暴露的结果与迁移记录。

| 字段 | 类型 | 说明 |
|------|------|------|
| last_result_code | 字符串 | 最近结果码 |
| last_reason_code | 字符串 | 最近原因码 |
| last_transition_record | `state_transition_record_t` | 最近迁移或拒绝/忽略记录 |

**拥有者**：`runtime_event_recorder_set_latest_result()`、`runtime_event_recorder_apply_projection()`  
**触发方**：正式应用入口在完成处理后调用投影器  
**读取方**：CLI 结果显示、状态查询、日志接口

**校验规则**：
- 最近结果只能通过统一记录辅助逻辑刷新。
- 查询、适配层和普通领域服务不得顺手写入最近结果。
- 迁移记录与最近结果必须来自同一次正式投影，不得分散更新。

## 6. 会话状态（`wash_session_t`）

**说明**：表示一次洗车会话的生命周期与最终业务结论。

| 字段 | 类型 | 说明 |
|------|------|------|
| session_id | 字符串 | 会话标识 |
| session_state | 枚举 | 会话状态 |
| selected_program_id | 字符串 | 选中的程序标识 |
| program_snapshot_id | 字符串 | 绑定的程序快照标识 |
| current_execution_id | 字符串 | 当前执行标识 |
| progress_segment_id | 字符串 | 当前推进工步标识 |
| latest_execution_result | 枚举 | 最近执行结果 |
| final_session_result | 枚举 | 会话最终结果 |
| abort_reason | 字符串 | 中止原因 |
| started_at_ms / ended_at_ms | 时间戳 | 会话开始与结束时间 |

**拥有者**：`wash_session_state_machine_*()`  
**读取方**：应用编排、状态查询、结果投影

**状态转换**：
1. `RESET`
2. `CREATED`
3. `RUNNING`
4. `COMPLETED` 或 `ABORTED`

**校验规则**：
- 会话最终结果只允许落在 `final_session_result`。
- 会话结束后必须存在唯一终态，不得由其他对象重复解释为另一个最终结论。

## 7. 执行状态（`wash_execution_t`）

**说明**：表示当前工步段执行态与退出动作运行态。

| 字段 | 类型 | 说明 |
|------|------|------|
| execution_id | 字符串 | 执行标识 |
| execution_state | 枚举 | 执行总状态 |
| lifecycle_state | 枚举 | 当前工步生命周期 |
| segment_index / segment_id | 索引/字符串 | 当前工步定位 |
| active_conditional_controls | 固定数组 | 已激活条件控制 |
| exit_runtime | `exit_action_runtime_t` | 收尾动作运行态 |
| started_at_ms / segment_started_at_ms / exit_started_at_ms / ended_at_ms | 时间戳 | 各阶段时间 |
| execution_result / end_reason | 枚举 | 执行结果与结束原因 |
| result_code / reason_code | 字符串 | 当前执行对外结果码 |

**拥有者**：`wash_execution_service_*()` 与 `wash_execution_*()`  
**读取方**：应用编排、状态查询、会话状态机

**状态转换**：
1. `IDLE`
2. `RUNNING`
3. `COMPLETED` 或 `ABORTED`

**校验规则**：
- 执行态只解释当前工步与退出过程，不解释整个会话最终结论。
- 退出动作运行态不得由查询或适配层直接改写。

## 8. 等待状态（`wait_condition_t`）

**说明**：表示段内或收尾阶段的超时等待条件。

| 字段 | 类型 | 说明 |
|------|------|------|
| wait_condition_id | 字符串 | 等待标识 |
| execution_id | 字符串 | 所属执行标识 |
| reason_code | 字符串 | 等待原因 |
| armed_at_ms / timeout_at_ms | 时间戳 | 等待开始与超时点 |
| timeout_policy | 枚举 | 超时策略 |
| active | bool | 是否正在等待 |

**拥有者**：`wash_execution_service_*()` 设置，`wait_timeout_service_*()` 只判定是否超时  
**读取方**：主循环超时注入、状态查询、应用编排

**校验规则**：
- `wait_timeout_service_*()` 只能产出超时事实，不直接接管其他运行对象写入。
- 活跃等待只能对应一个明确的执行上下文。

## 9. 程序快照（`program_snapshot_t`）

**说明**：表示会话启动时绑定的程序快照和校验结果。

| 字段 | 类型 | 说明 |
|------|------|------|
| program_snapshot_id | 字符串 | 快照标识 |
| source_program_id | 字符串 | 来源程序标识 |
| source_revision | 整数 | 来源版本 |
| captured_at_ms | 时间戳 | 捕获时间 |
| validation_result | 枚举 | 快照校验结果 |
| snapshot_hash / snapshot_payload_ref | 字符串 | 快照标识信息 |
| frozen_program | `wash_program_v1_t` | 冻结程序内容 |

**拥有者**：`program_snapshot_service_capture()`  
**读取方**：会话状态机、执行服务、状态查询

**校验规则**：
- 会话一旦启动，执行层只能依赖绑定快照，不再回读外部程序仓储。
- 快照只负责程序冻结，不负责写最近结果或全局故障。

## 10. 只读状态视图（`wash_session_status_view_t`）

**说明**：面向查询与展示的只读投影对象，不拥有业务状态，只从现有运行对象拷贝快照。

| 字段 | 类型 | 说明 |
|------|------|------|
| has_active_session | bool | 是否存在活动会话 |
| global_fault_present | bool | 是否存在全局故障 |
| session_id | 字符串 | 当前会话标识 |
| session_state | 枚举 | 会话状态 |
| execution_state | 枚举 | 执行状态 |
| lifecycle_state | 枚举 | 工步生命周期 |
| stage_id | 字符串 | 当前工步标识 |
| wait_condition_id / wait_reason | 字符串 | 当前等待状态 |
| reason_code | 字符串 | 最近原因 |
| global_fault_reason | 字符串 | 最近故障原因 |

**拥有者**：`query_wash_session_status_execute()` 每次调用时临时构造  
**读取来源**：`system_context_t`、`wash_session_t`、`wash_execution_t`、`wait_condition_t`

**校验规则**：
- 状态视图构造过程不得反向写入任何关键运行状态。
- 该对象结束生命周期后不得作为新的业务事实源长期保存。

## 11. 服务依赖切片（现有 `*_service_args_t`）

**说明**：表示应用层从组合根中切出的最小依赖集合，用于把领域服务与总上下文隔离开。

| 切片类型 | 最小依赖 |
|------|------|
| `wash_session_service_args_t` | `wash_session`、`program_snapshot`、`next_session_sequence`、`current_time_ms` |
| `wash_execution_service_args_t` | `wash_execution`、`wash_session`、`wait_condition`、`program_snapshot`、端口、序列号、`current_time_ms` |
| `program_snapshot_service_args_t` | `program_snapshot`、`wash_program`、`program_repository_port`、`current_time_ms` |

**校验规则**：
- 依赖切片中不得出现 `global_fault_*`、最近结果字段、触发队列字段等与该服务职责无关的内容。
- 新增领域服务优先复用这种切片模式，而不是直接透传组合根。

## 关系总览

1. `system_context_t` 是组合根，聚合 `wash_session_t`、`wash_execution_t`、`wait_condition_t`、`program_snapshot_t` 和调度/结果状态。
2. `main_loop_*()` 拥有触发队列与运行时钟，并通过正式入口驱动处理。
3. `process_wash_trigger_execute()` 负责编排跨对象处理，并拥有 `global_fault_*` 的正式写入口。
4. `runtime_event_recorder_*()` 负责最近结果与迁移记录的最终投影落点。
5. 领域服务只消费服务依赖切片，不直接依赖 `system_context_t`。
6. `query_wash_session_status_execute()` 从组合根和运行对象构造只读状态视图，但不回写状态。
