# 接口契约：操作与维护命令

## 1. 说明

本契约定义主控软件对外暴露的逻辑入站命令，不绑定具体传输协议。无论最终由本地 HMI、CLI、串口面板还是 IPC 适配器承载，都必须遵循相同语义。

## 2. 通用命令格式

| 字段 | 类型 | 说明 |
|------|------|------|
| command_id | string | 命令唯一标识 |
| command_type | enum | 命令类型 |
| issued_at | datetime | 发起时间 |
| operator_id | string | 操作员或维护人员标识 |
| payload | object | 命令载荷 |

## 3. 命令集合

### 3.1 start_wash_cycle

**用途**：启动一次洗车周期。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| program_id | string | 是 | 操作员手动选择的洗车程序 |
| vehicle_present_confirm | bool | 是 | 车辆已进入工位确认 |

**前置条件**

- 系统处于待机状态。
- 自动预检通过。
- 车辆尺寸与程序适配判定通过。

**返回结果**

| 字段 | 类型 | 说明 |
|------|------|------|
| accepted | bool | 是否受理 |
| cycle_id | string | 受理后的周期标识 |
| reject_reason | string | 拒绝原因 |

### 3.2 stop_wash_cycle

**用途**：人工终止当前周期。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| cycle_id | string | 是 | 目标周期 |
| reason | string | 否 | 人工终止原因 |

**行为**

- 进入安全停机或人工终止路径。
- 记录事件日志。

### 3.3 acknowledge_fault

**用途**：操作员/维护人员确认告警。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| event_id | string | 是 | 告警事件标识 |
| note | string | 否 | 备注 |

### 3.4 enter_maintenance_mode

**用途**：进入维护模式。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| authorization_token | string | 是 | 授权凭证 |

**前置条件**

- 系统不在自动运行中。

### 3.5 jog_actuator

**用途**：维护模式下点动执行机构。

**payload**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| actuator_type | enum | 是 | gantry / roof_brush / side_brush / chemical_channel |
| actuator_id | string | 否 | 对多实例部件有效 |
| action | enum | 是 | forward / reverse / start / stop / pulse |
| duration_ms | integer | 否 | 点动持续时间 |

**限制**

- 只能在维护模式中使用。
- 每次点动都必须带超时。

## 4. 错误码语义

| 错误码 | 含义 |
|--------|------|
| INVALID_STATE | 当前状态不允许执行命令 |
| PRECHECK_FAILED | 启动前预检失败 |
| VEHICLE_NOT_ALLOWED | 车辆不满足程序适配条件 |
| SAFETY_INTERLOCK_ACTIVE | 安全联锁触发 |
| RESOURCE_UNAVAILABLE | 关键资源不可用 |
| AUTH_REQUIRED | 需要更高权限 |
