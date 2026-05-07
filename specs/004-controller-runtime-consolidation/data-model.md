# 数据模型：控制器运行时收敛

本特性不引入新的领域业务对象，而是收敛现有运行时对象之间的职责、入口和可观测语义。以下模型描述的是实现本次收敛所需的运行时视图和约束。

## 1. 正式命令受理请求（formal_command_request）

**说明**：表示一条来自正式 stdin 的命令，在解析完成后进入统一正式路径前的受理对象。

| 字段 | 类型 | 说明 |
|------|------|------|
| command_name | enum | `start` / `stop` / `feedback` / `fault` / `fault_clear` / `status` |
| trigger_id | string | 仅对会入队的命令有效，用于跟踪 pending trigger 生命周期 |
| payload_code | string | `program_id`、`signal_code`、`fault_code` 等主参数 |
| payload_reason | string | `fault` 原因或 stop 原因等补充信息 |
| from_stdin | bool | 固定为 `true`，表示这是正式入口而非内部直调 |
| requires_queue | bool | 标记该命令是只读查询还是需要进入 trigger 队列 |

**约束**：
- `status` 也属于正式受理对象，但 `requires_queue=false`。
- 解析失败的命令不得创建 `formal_command_request` 后续入队动作。
- 正式 stdin 不得绕过该受理模型直接落到业务核心。

## 2. 队列化触发事件（queued_runtime_trigger）

**说明**：表示统一正式路径中进入 `main_loop_submit_trigger()` 队列的 trigger 事件。

| 字段 | 类型 | 说明 |
|------|------|------|
| trigger_type | enum | 现有 `wash_trigger_event.trigger_type` |
| trigger_id | string | 唯一标识本次待消费事件 |
| source | string | `stdin` 或 `main-loop-timeout` |
| program_id | string | 仅 `start` 相关 |
| signal_code | string | `feedback`、`fault` 或 `stop` 的主语义码 |
| correlation_key | string | 去重或补充原因字段 |
| requested_at_ms | integer | 入队时间 |

**约束**：
- 所有正式写命令最终都必须转换为该模型后再进入主循环消费。
- timeout 事件只允许由主循环派生，不允许外部命令直接构造。
- `cli_command_adapter_process_line()` 式直调不再视为合法正式 trigger 生成路径。

## 3. 统一结果记录（recorded_runtime_result）

**说明**：表示最近一次被系统处理的事件结论，以及与日志和响应共享的最小语义集合。

| 字段 | 类型 | 说明 |
|------|------|------|
| result_code | string | `accepted` / `rejected` / `ignored` / `error` / `status` 等 |
| reason_code | string | 对应原因码或事件摘要原因 |
| transition_entity | enum | request / session / execution 等 |
| transition_result | string | 迁移或处理结论 |
| log_channel | enum | rejection / ignored / transition |
| response_acceptance | bool | 最终 CLI 响应中的 `accepted=true|false` |

**约束**：
- 接受、拒绝、忽略三类事件都必须刷新最近结果。
- 具体命令场景沿用既有结果定义，本模型不在本轮新增任何命令特例语义。
- CLI 响应、日志和状态查询引用的最近结果语义必须来自同一个记录模型。

## 4. 状态写入责任映射（state_write_ownership）

**说明**：表示哪些模块拥有特定状态的唯一写入权。

| 状态类别 | 唯一写入责任方 | 禁止越界写入方 |
|------|------|------|
| `wash_session` | `wash_session_state_machine` | `main.c`、CLI 适配器、`wait_timeout_service` |
| `wash_execution` | `wash_execution_service` | `main.c`、CLI 适配器、`wash_session_state_machine` |
| `wait_condition` | `wash_execution_service`（推进）/ `wait_timeout_service`（裁决） | `main.c`、CLI 适配器 |
| `global_fault_*` | `process_wash_trigger_execute()` | `main_loop`、timeout service、CLI 适配器 |
| `last_result_*` 与最近记录 | 统一结果记录辅助逻辑 | 分散的各条命令分支 |

**约束**：
- `process_wash_trigger_execute()` 只拥有高层策略级状态写入权，不得接管 session/execution 的内部迁移细节。
- `main.c` 和 `main_loop.c` 只负责协调，不直接承担业务状态写入。

## 5. 内部只读状态视图（internal_status_view）

**说明**：表示 `query_wash_session_status_execute()` 返回的内部只读快照，以及 `status` 命令响应映射的来源。

| 字段 | 类型 | 说明 |
|------|------|------|
| has_active_session | bool | 是否存在活动任务 |
| session_state | enum | 当前 session 状态 |
| execution_state | enum | 当前 execution 状态 |
| stage_id | string | 当前阶段 |
| wait_condition_id | string | 当前等待条件标识 |
| wait_reason | string | 当前等待原因 |
| global_fault_present | bool | 是否存在全局故障 |
| global_fault_reason | string | 最近全局故障原因 |
| latest_result_code | string | 最近处理事件结论 |
| latest_reason_code | string | 最近处理事件原因 |

**约束**：
- 该视图是只读的，不得推进状态。
- 正式 stdin 的 `status` 命令必须基于该视图生成响应，但不能直接把它变成绕过正式路径的旁路。

## 6. 主入口骨架职责（main_runtime_shell）

**说明**：表示 `main.c` 在本次收敛后允许保留的职责集合。

| 职责 | 是否保留 | 说明 |
|------|------|------|
| 初始化端口与上下文 | 是 | 仍属于进程入口职责 |
| 等待 stdin / timeout | 是 | 保留现有 select 模型 |
| 推进当前时间 | 是 | 仍属于主循环骨架 |
| 调用正式命令受理 | 是 | 只负责协调调用，不嵌入业务分支细节 |
| 直接执行业务 use_case | 否 | 必须下沉到正式路径 |
| 手写结果语义 | 否 | 必须由统一结果记录逻辑负责 |

**约束**：
- `main.c` 不再承担“另一套主业务链”。
- 任何新加入的顶层逻辑都必须先证明无法下沉到已有运行时模块。
