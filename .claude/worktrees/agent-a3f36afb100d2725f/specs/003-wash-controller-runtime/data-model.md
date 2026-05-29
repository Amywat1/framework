# 数据模型：洗车主控运行骨架

## 1. 主控运行实例（controller_runtime）

**说明**：表示一次主控进程生命周期内长期存在的运行载体，是对现有 `system_context` 在主控层语义上的抽象。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| runtime_id | string | 主控实例标识，可由进程启动时生成 |
| current_time_ms | integer | 当前运行时刻 |
| has_active_session | bool | 当前是否存在活动洗车任务 |
| active_session_id | string | 当前活动会话标识，无任务时为空 |
| global_fault_present | bool | 是否存在未清除的全局故障 |
| global_fault_code | string | 最近全局故障代码 |
| global_fault_reason | string | 最近全局故障原因 |
| last_result_code | string | 最近一次命令/迁移结果 |
| last_reason_code | string | 最近一次关键原因 |

**规则**

- `controller_runtime` 在主控进程运行期内持续存在。
- 主控进程不得因单次任务完成或暂时无输入而销毁该对象。
- 全局故障状态与任务级状态独立存在，清除方式只能是 `fault clear`。

## 2. 控制器命令（controller_command）

**说明**：表示通过正式输入通道进入主控的一条原子命令。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| command_id | string | 命令标识 |
| command_type | enum | start / stop / feedback / fault_report / fault_clear / status |
| raw_line | string | 原始输入文本 |
| argument_1 | string | 主参数，如 `program_id`、`signal_code`、`fault_code` |
| argument_2 | string | 次参数，如 `reason` |
| received_at_ms | integer | 接收时间 |
| parse_result | enum | valid / invalid |

**规则**

- 每条命令只代表一次处理意图。
- 命令解析失败时不得推进业务状态，但必须形成单行错误响应。
- `fault clear` 是独立命令语义，不得与 `fault <fault_code> <reason>` 混淆。

## 3. 触发请求（runtime_trigger_request）

**说明**：表示正式输入被归一化后交给应用/领域层处理的请求，可映射为已有 `wash_trigger_event` 或查询动作。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| request_type | enum | start / stop / feedback / fault / timeout / status_query |
| correlation_key | string | 用于去重或跟踪 |
| payload_code | string | `program_id`、`signal_code`、`fault_code` 等 |
| payload_reason | string | 故障原因或补充说明 |
| source | string | 来源，固定为 `stdin` 或 `main-loop-timeout` |
| requested_at_ms | integer | 请求进入主循环的时间 |

**规则**

- `status_query` 只读，不得写状态。
- `timeout` 由等待条件派生，不能由外部命令直接构造。
- 非法命令不得进入 `runtime_trigger_request`。

## 4. 全局故障状态（global_fault_state）

**说明**：表示不依附于活动任务的主控级故障记录。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| present | bool | 是否存在全局故障 |
| fault_code | string | 故障代码 |
| reason | string | 故障原因 |
| raised_at_ms | integer | 记录时间 |
| cleared_at_ms | integer | 清除时间，未清除时为空 |
| clear_result_code | string | 最近清除结果 |

**规则**

- 只有在无活动任务时收到 `fault` 才会写入 `global_fault_state`。
- 全局故障存在时，新的 `start` 必须拒绝。
- `fault clear` 是唯一允许清除 `present=true` 的正式命令。

## 5. 启动预检结果（start_precheck_result）

**说明**：表示某次 `start` 受理前对资源和传感器条件的检查结果。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| allowed | bool | 是否允许继续启动 |
| result_code | string | 预检结果代码 |
| reason_code | string | 失败或告警原因 |
| checked_at_ms | integer | 检查时间 |
| sensor_summary | string | 传感器检查摘要 |

**规则**

- 只有 `allowed=true` 时才允许创建任务。
- 预检失败不会终止主控进程，只拒绝当前启动命令。
- 预检结果必须可进入最近原因或日志记录。

## 6. 命令响应（command_response）

**说明**：表示主控对一条正式输入命令返回的单行输出结果。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| command_id | string | 关联命令标识 |
| accepted | bool | 是否被受理 |
| result_code | string | 处理结果 |
| summary | string | 单行摘要文本 |
| emitted_at_ms | integer | 输出时间 |

**规则**

- 每条命令必须且只能生成一条 `command_response`。
- 响应必须是单行文本。
- 对被忽略或拒绝的命令也必须返回明确结果。

## 7. 状态快照（runtime_status_view）

**说明**：表示 `status` 命令和测试查询看到的主控对外状态视图。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| has_active_session | bool | 是否存在活动任务 |
| session_state | enum | none / created / running / completed / aborted |
| execution_state | enum | none / running / completed / aborted |
| stage_id | string | 当前阶段标识 |
| wait_condition_id | string | 当前等待条件标识 |
| wait_reason | string | 当前等待或最近等待原因 |
| latest_reason_code | string | 最近一次关键原因 |
| global_fault_present | bool | 是否存在全局故障 |
| global_fault_reason | string | 最近全局故障原因 |

**规则**

- `runtime_status_view` 不推进业务状态。
- 当无活动任务但存在全局故障时，视图必须同时展示“无任务”和“全局故障存在”。
- 任务结束后，如果尚未开始下一单，状态视图仍必须可判断。

## 8. 关键关系

```text
controller_runtime 1---0..1 global_fault_state
controller_runtime 1---* controller_command
controller_command 1---1 command_response
controller_command 0..1---1 runtime_trigger_request
controller_runtime 1---0..1 wash_session
controller_runtime 1---1 runtime_status_view
controller_command 0..1---1 start_precheck_result
```
