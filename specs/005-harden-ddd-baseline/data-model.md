# 数据模型：DDD 架构基线加固

本特性不引入新的业务领域对象，而是收敛现有运行时对象之间的职责、入参边界和语义投影。以下模型描述的是实现本次基线加固所需的运行时视图与约束。

## 1. 正式命令受理请求（formal_command_request）

**说明**：表示一条来自正式 `stdin` 的命令，在进入统一应用层正式入口后形成的受理对象。

| 字段 | 类型 | 说明 |
|------|------|------|
| command_name | enum | `start` / `stop` / `feedback` / `fault` / `fault_clear` / `status` |
| source | enum | 固定为 `stdin` |
| requires_queue | bool | 写命令为 `true`，`status` 为 `false` |
| trigger_type | enum | 写命令对应的 `trigger_type_t` |
| primary_code | string | `program_id`、`signal_code`、`fault_code` 等主参数 |
| secondary_reason | string | 故障原因、停止原因或补充信息 |
| received_at_ms | integer | 进入正式入口的时间 |

**验证规则**：
- `status` 必须作为正式受理对象进入统一入口，但 `requires_queue=false`。
- 解析失败的命令不得继续进入队列或领域编排。
- 正式 `stdin` 不得绕过该模型直接触发业务核心。

## 2. 领域决策事实（domain_decision_fact）

**说明**：表示领域服务返回给应用层的最小事实集合，应用层据此完成状态落点和结果投影。

| 字段 | 类型 | 说明 |
|------|------|------|
| fact_kind | enum | `session_transition` / `execution_progress` / `wait_update` / `timeout_resolution` / `request_rejected` / `request_ignored` |
| trigger_type | enum | 触发该事实的事件类型 |
| previous_state | string | 发生前状态摘要 |
| current_state | string | 发生后状态摘要 |
| result_code | string | 领域层给出的结果事实 |
| reason_code | string | 领域层给出的原因码 |
| state_changes | set | 对会话、执行、等待条件等产生的领域状态变化 |

**验证规则**：
- 领域事实只描述领域结论，不直接包含 CLI 响应文本或日志通道。
- 领域事实不得直接写入最近结果或 `global_fault`。
- 应用层必须能够从领域事实唯一决定记录与对外投影。

## 3. 超时决议事实（timeout_resolution_fact）

**说明**：表示 `wait_timeout_service` 返回给应用层的超时处理决议。

| 字段 | 类型 | 说明 |
|------|------|------|
| resolution | enum | `retried` / `continue_session` / `finish_execution` / `abort_session` |
| reason_code | string | 对应超时原因 |
| retry_consumed | bool | 是否消耗了一次重试 |
| requires_execution_followup | bool | 是否需要应用层继续调执行服务 |
| requires_session_followup | bool | 是否需要应用层继续调会话状态机 |

**验证规则**：
- `wait_timeout_service` 只产生决议，不直接接管会话、执行或最近结果写入。
- 应用层必须基于该事实决定后续编排和结果投影。

## 4. 运行时结果投影（runtime_result_projection）

**说明**：表示应用层把领域事实映射为最近结果、状态摘要、日志和 CLI 响应时使用的统一投影模型。

| 字段 | 类型 | 说明 |
|------|------|------|
| latest_result_code | string | 最近处理结果 |
| latest_reason_code | string | 最近处理原因 |
| transition_entity | enum | `request` / `session` / `execution` |
| transition_from | string | 前态摘要 |
| transition_to | string | 后态摘要 |
| log_kind | enum | `transition` / `rejection` / `ignored` / `none` |
| cli_result_code | string | CLI 使用的结果码 |
| cli_accepted | bool | CLI 的 `accepted=true|false` |
| cli_detail | string | CLI 的 detail 字段 |

**验证规则**：
- 最近结果只允许统一主触发编排入口通过该模型写入。
- `status` 只读取状态视图，不创建新的运行时结果投影。
- 同一事件在状态摘要、日志和 CLI 中的表达必须来自同一份投影结论。

## 5. 状态写入边界映射（state_write_boundary_map）

**说明**：表示核心运行时状态与唯一主写入责任方的对应关系。

| 状态类别 | 唯一主写入责任方 | 允许写入内容 |
|------|------|------|
| `wash_session` | `wash_session_state_machine` | 会话创建、运行、完成、终止 |
| `wash_execution` | `wash_execution_service` | 执行开始、完成、终止 |
| `wait_condition` | `wash_execution_service` | 等待条件建立、重置、完成 |
| timeout 决议 | `wait_timeout_service` | 超时裁决事实 |
| `global_fault_*` | 统一主触发编排入口 | 设置、清除全局故障 |
| `last_result_*` | 统一主触发编排入口 | 最近结果与原因更新 |

**验证规则**：
- 任何跨对象状态都必须能追溯到唯一主写入边界。
- `main.c`、`main_loop.c` 和 CLI 适配层不得直接承担业务状态写入职责。
- 领域服务不得借由整块 `system_context` 改写不属于自己的状态类别。

## 6. 只读状态视图（status_query_view）

**说明**：表示 `status` 和内部只读查询共享的状态快照来源。

| 字段 | 类型 | 说明 |
|------|------|------|
| session_id | string | 当前会话标识 |
| session_state | enum | 会话状态 |
| execution_state | enum | 执行状态 |
| stage_id | string | 当前阶段 |
| wait_reason | string | 当前等待原因 |
| global_fault_present | bool | 是否存在全局故障 |
| global_fault_reason | string | 全局故障原因 |
| latest_result_code | string | 最近结果 |
| latest_reason_code | string | 最近原因 |

**验证规则**：
- 该视图只读，不推进状态。
- `status` 必须基于该视图生成响应，但不得借查询动作刷新最近结果。

## 关系与流转

1. `formal_command_request` 进入统一应用层正式入口。
2. 写命令转换为队列事件或应用层编排调用；`status` 直接进入只读查询分支。
3. 领域服务返回 `domain_decision_fact` 或 `timeout_resolution_fact`。
4. 应用层根据事实更新拥有写入权的状态，并生成 `runtime_result_projection`。
5. `status_query_view` 继续作为只读快照为状态查询提供视图，不参与最近结果写入。
