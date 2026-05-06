# 数据模型：洗车执行模型重构

## 1. 洗车会话（wash_session）

**说明**：表示一次完整洗车任务的生命周期对象。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| session_id | string | 会话唯一标识 |
| session_state | enum | created / running / completed / aborted |
| selected_program_id | string | 启动请求中选择的程序标识 |
| program_snapshot_id | string | 绑定的程序快照标识 |
| current_execution_id | string | 当前有效单次执行标识 |
| progress_stage_id | string | 当前阶段标识 |
| latest_execution_result | enum | 最近一次执行结果 |
| final_session_result | enum | 最终会话结果 |
| abort_reason | enum | 中止原因 |
| started_at | datetime | 会话开始时间 |
| ended_at | datetime | 会话结束时间 |

**规则**

- `session_id` 全局唯一。
- 只有程序数据校验通过后才能创建 `wash_session`。
- 同一时刻最多只能存在一个 `session_state=running` 的会话。
- 会话进入 `completed` 或 `aborted` 后不得被重新打开。

**状态迁移**

```text
created -> running
running -> completed
running -> aborted
```

## 2. 单次执行（wash_execution）

**说明**：表示会话内部一次可独立推进的执行片段，用于承载当前阶段推进、等待与结果。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| execution_id | string | 执行唯一标识 |
| session_id | string | 所属会话标识 |
| execution_state | enum | running / completed / aborted |
| execution_kind | enum | stage_step / stop_path / fault_path / timeout_path |
| stage_id | string | 关联阶段标识 |
| wait_condition_id | string | 当前等待条件标识 |
| started_at | datetime | 执行开始时间 |
| ended_at | datetime | 执行结束时间 |
| execution_result | enum | advanced / waiting / completed / stopped / faulted / timed_out / ignored |
| end_reason | enum | normal / stop / fault / timeout / rejection |

**规则**

- 一个会话在任一时刻只能有一个 `execution_state=running` 的执行。
- 执行结果必须在结束时唯一确定。
- `execution_result=timed_out` 时，必须能够映射到明确的后续会话决策。

**状态迁移**

```text
running -> completed
running -> aborted
```

## 3. 触发事件（wash_trigger_event）

**说明**：表示驱动会话或执行发生变化的离散输入。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| trigger_id | string | 触发唯一标识 |
| trigger_type | enum | start / stop / device_feedback / fault / business / timeout |
| session_id | string | 关联会话标识，启动前可为空 |
| execution_id | string | 关联执行标识，若适用 |
| source | string | 触发来源模块 |
| occurred_at | datetime | 发生时间 |
| correlation_key | string | 用于识别重复或迟到事件 |
| payload_ref | string | 载荷引用或摘要标识 |

**规则**

- `start` 事件在无运行中会话且程序数据校验通过时才可受理。
- `timeout` 事件必须对应某个已登记的等待条件。
- 迟到事件和重复事件必须可识别，并进入忽略路径。

## 4. 等待条件（wait_condition）

**说明**：表示某次执行当前正在等待的外部条件及其边界。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| wait_condition_id | string | 等待条件唯一标识 |
| execution_id | string | 所属执行标识 |
| expected_trigger_type | enum | device_feedback / business / timeout |
| expected_signal | string | 期望信号或反馈类型 |
| armed_at | datetime | 开始等待时间 |
| timeout_at | datetime | 超时截止时间 |
| timeout_policy | enum | continue_session / finish_execution / abort_session |
| is_satisfied | bool | 是否已满足 |

**规则**

- 所有等待条件都必须有 `timeout_at`。
- `timeout_policy` 只能取三类规格允许的后续路径。
- 一旦等待条件满足或超时，必须关闭该等待条件，防止重复消费。

## 5. 程序快照（program_snapshot）

**说明**：表示会话启动时冻结下来的程序配置内容。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| program_snapshot_id | string | 快照唯一标识 |
| source_program_id | string | 源程序标识 |
| source_revision | integer | 源程序修订版本 |
| captured_at | datetime | 快照生成时间 |
| validation_result | enum | valid / invalid |
| snapshot_hash | string | 快照内容摘要 |
| snapshot_payload_ref | string | 快照内容引用 |

**规则**

- 只有 `validation_result=valid` 的快照可绑定到新会话。
- 会话创建后，其 `program_snapshot_id` 不得替换为外部更新版本。
- 新的程序修订只影响后续新会话。

## 6. 状态迁移记录（state_transition_record）

**说明**：表示一次可审计的会话或执行状态变化记录。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| record_id | string | 记录唯一标识 |
| entity_type | enum | session / execution / request |
| entity_id | string | 会话、执行或请求标识 |
| trigger_type | enum | start / stop / device_feedback / fault / business / timeout / reject / ignore |
| previous_state | string | 迁移前状态 |
| current_state | string | 迁移后状态 |
| result_code | string | 本次迁移结果 |
| reason_code | string | 原因代码 |
| recorded_at | datetime | 记录时间 |

**规则**

- 所有最终结果都必须至少生成一条 `state_transition_record`。
- 重复启动拒绝、启动前校验失败、迟到事件忽略和重复事件忽略也必须形成记录。
- 同一实体不得产生相互冲突的最终结果记录。

## 7. 领域关系

```text
wash_session 1---1 program_snapshot
wash_session 1---* wash_execution
wash_session 1---* state_transition_record
wash_execution 1---0..1 wait_condition
wash_execution 1---* state_transition_record
wash_trigger_event *---0..1 wash_session
wash_trigger_event *---0..1 wash_execution
```
