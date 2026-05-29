# 接口契约：告警与事件输出

## 1. 说明

本契约定义主控软件对外输出的周期结果、告警和状态变更事件。用于 HMI 展示、日志持久化和后续诊断接口扩展。

## 2. 通用事件格式

| 字段 | 类型 | 说明 |
|------|------|------|
| event_id | string | 事件唯一标识 |
| event_time | datetime | 事件时间 |
| event_type | enum | cycle_state_changed / fault_raised / fault_cleared / cycle_finished |
| severity | enum | info / warning / safety |
| cycle_id | string | 关联洗车周期 |
| payload | object | 事件内容 |

## 3. fault_raised 载荷

| 字段 | 类型 | 说明 |
|------|------|------|
| fault_class | enum | safety / process / resource / communication / configuration |
| source | string | 来源模块 |
| code | string | 领域错误码 |
| message | string | 中文说明 |
| disposition | enum | stop / degrade / skip / safe_finish |
| operator_ack_required | bool | 是否要求人工确认 |

## 4. cycle_state_changed 载荷

| 字段 | 类型 | 说明 |
|------|------|------|
| previous_state | enum | 上一状态 |
| current_state | enum | 当前状态 |
| stage_id | string | 当前阶段 |

## 5. cycle_finished 载荷

| 字段 | 类型 | 说明 |
|------|------|------|
| result_code | enum | success / safe_stop / degraded_complete / manual_abort / start_failed |
| start_time | datetime | 开始时间 |
| end_time | datetime | 结束时间 |
| primary_fault_code | string | 主要异常代码（若有） |

## 6. 契约约束

- 所有 `message` 必须为中文。
- `severity=safety` 的事件必须在危险动作撤销前后都留下记录。
- `cycle_finished` 事件必须在周期结束时产生，且只能有一条最终结果记录。
