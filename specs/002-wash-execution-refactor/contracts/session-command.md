# 接口契约：会话控制命令

## 1. 说明

本契约定义新的洗车执行模型对外暴露的逻辑入站命令，聚焦“创建会话、停止会话、推进执行”的命令语义，不绑定具体 CLI、HMI、IPC 或其他传输方式。

## 2. 通用命令格式

| 字段 | 类型 | 说明 |
|------|------|------|
| command_id | string | 命令唯一标识 |
| command_type | enum | start_session / stop_session / submit_feedback / query_status |
| issued_at | datetime | 发起时间 |
| operator_id | string | 发起人标识 |
| payload | object | 命令载荷 |

## 3. start_session

**用途**：请求创建一次新的洗车会话。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| program_id | string | 是 | 目标程序标识 |
| vehicle_present_confirm | bool | 是 | 车辆已进入工位确认 |
| request_note | string | 否 | 备注 |

**受理前置条件**

- 当前不存在运行中的洗车会话
- 程序数据可用
- 程序数据校验通过

**返回结果**

| 字段 | 类型 | 说明 |
|------|------|------|
| accepted | bool | 是否受理 |
| session_id | string | 受理后的会话标识 |
| reject_reason | enum | running_session_exists / program_unavailable / program_invalid / invalid_state |

**契约约束**

- 若 `accepted=false`，不得创建新会话。
- 重复启动请求必须返回 `running_session_exists`。

## 4. stop_session

**用途**：请求停止当前运行中的洗车会话。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| session_id | string | 是 | 目标会话标识 |
| reason | string | 否 | 停止原因 |

**契约约束**

- 若目标会话不是运行中状态，必须返回显式拒绝原因。
- `stop_session` 只产生停止触发，不直接跳过领域层状态迁移规则。

## 5. submit_feedback

**用途**：向当前执行提交设备反馈或业务反馈。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| session_id | string | 是 | 目标会话标识 |
| execution_id | string | 否 | 目标执行标识 |
| feedback_type | enum | device_feedback / business |
| feedback_code | string | 是 | 反馈标识 |
| correlation_key | string | 否 | 去重与迟到识别键 |

**契约约束**

- 反馈只在与当前等待条件匹配时才可推进执行。
- 不匹配、重复或迟到反馈必须被忽略并产生记录。

## 6. query_status

**用途**：读取当前会话状态、当前执行状态和最近一次状态迁移原因。

**返回结果**

| 字段 | 类型 | 说明 |
|------|------|------|
| session_id | string | 当前会话标识，无活动会话时为空 |
| session_state | enum | none / created / running / completed / aborted |
| execution_state | enum | none / running / completed / aborted |
| stage_id | string | 当前阶段标识 |
| latest_reason | string | 最近一次迁移原因 |

**契约约束**

- `query_status` 不得推进业务状态。
- 输出必须足以支撑维护人员在 1 分钟内定位当前状态与最近原因。
