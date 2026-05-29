# 接口契约：执行状态与结果事件

## 1. 说明

本契约定义新的执行模型向日志、HMI 或其他外部观察者输出的状态变更与结果事件。

## 2. 通用事件格式

| 字段 | 类型 | 说明 |
|------|------|------|
| event_id | string | 事件唯一标识 |
| event_time | datetime | 事件时间 |
| event_type | enum | session_state_changed / execution_state_changed / request_rejected / session_finished |
| session_id | string | 关联会话标识 |
| execution_id | string | 关联执行标识，若适用 |
| payload | object | 事件载荷 |

## 3. session_state_changed

| 字段 | 类型 | 说明 |
|------|------|------|
| previous_state | enum | created / running / completed / aborted |
| current_state | enum | created / running / completed / aborted |
| trigger_type | enum | start / stop / fault / timeout / business |
| result_code | string | 本次迁移结果 |

## 4. execution_state_changed

| 字段 | 类型 | 说明 |
|------|------|------|
| previous_state | enum | running / completed / aborted |
| current_state | enum | running / completed / aborted |
| stage_id | string | 当前阶段标识 |
| wait_condition_id | string | 当前等待条件标识 |
| retry_count | integer | 当前等待已发生的重试次数 |
| result_code | enum | advanced / waiting / completed / stopped / faulted / timed_out / ignored |

## 5. request_rejected

| 字段 | 类型 | 说明 |
|------|------|------|
| request_type | enum | start_session / stop_session / submit_feedback |
| reject_reason | enum | running_session_exists / program_unavailable / program_invalid / invalid_state / unmatched_feedback / duplicate_event / late_event |
| correlation_key | string | 请求关联键 |

## 6. session_finished

| 字段 | 类型 | 说明 |
|------|------|------|
| final_session_result | enum | completed / stopped / aborted / faulted / timed_out |
| latest_execution_result | enum | completed / stopped / faulted / timed_out |
| abort_reason | string | 终止原因，若适用 |
| started_at | datetime | 开始时间 |
| ended_at | datetime | 结束时间 |

## 7. 契约约束

- 每个已结束会话只能输出一条最终 `session_finished` 事件。
- `request_rejected` 必须用于重复启动、无效启动、迟到事件和重复事件。
- 当竞争事件发生时，只能输出与最高优先级结果一致的一组状态变更与终态事件。
- 若等待超时后进入重试路径，必须输出可区分的执行状态变化或记录，以便观察当前重试次数。
