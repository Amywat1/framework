# 数据模型：往复式龙门洗车机控制软件

## 1. 洗车程序（wash_program）

**说明**：定义一次完整洗车流程的可配置工艺。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| program_id | string | 程序唯一标识 |
| program_name | string | 程序名称 |
| enabled | bool | 是否允许操作员选择 |
| applicable_vehicle_types | list<string> | 适用车辆类别列表 |
| stages | list<wash_stage> | 阶段定义列表 |
| default_timeout_ms | integer | 默认阶段超时 |
| revision | integer | 配置版本号 |

**规则**

- `program_id` 全局唯一。
- 禁用程序不得出现在手动可选列表中。
- `stages` 至少包含一个运动/清洗有效阶段。
- 所有关联药剂通道、刷组和动作类型必须可解析且合法。

## 2. 洗车阶段（wash_stage）

**说明**：洗车程序中的单个控制片段。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| stage_id | string | 阶段唯一标识 |
| stage_name | string | 阶段名称 |
| sequence_no | integer | 阶段顺序 |
| gantry_motion_mode | enum | 停止 / 前进 / 后退 / 往返 |
| traverse_count | integer | 往返次数 |
| roof_brush_mode | enum | 禁用 / 跟随 / 固定动作 |
| side_brush_mode | enum | 禁用 / 跟随 / 固定动作 |
| chemical_actions | list<chemical_action> | 本阶段药剂动作 |
| rinse_enabled | bool | 是否启用冲洗 |
| stage_timeout_ms | integer | 阶段超时 |
| skip_on_resource_fault | bool | 资源类故障时是否允许跳过 |

**规则**

- `sequence_no` 在同一程序内唯一且递增。
- `stage_timeout_ms` 必须大于 0。
- `traverse_count` 在 `gantry_motion_mode=往返` 时必须大于 0。
- 安全类故障发生时，阶段配置中的 `skip_on_resource_fault` 不得生效。

## 3. 车辆类别（vehicle_type）

**说明**：用于自动适配判定和程序可选范围约束。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| vehicle_type_id | string | 类别唯一标识 |
| vehicle_type_name | string | 类别名称 |
| max_length_mm | integer | 最大车长 |
| max_width_mm | integer | 最大车宽 |
| max_height_mm | integer | 最大车高 |
| min_length_mm | integer | 最小车长 |
| allowed_programs | list<string> | 允许执行的程序 |

**规则**

- 尺寸范围必须闭合且 `max > min`。
- 系统自动检测出的车辆尺寸必须命中某一类别，才能允许启动。
- `allowed_programs` 只能引用已启用程序。

## 4. 药剂通道（chemical_channel）

**说明**：可独立控制的药剂输出路径。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| channel_id | string | 通道唯一标识 |
| channel_name | string | 通道名称 |
| enabled | bool | 通道是否启用 |
| resource_state | enum | 正常 / 不足 / 故障 / 禁用 |
| low_level_threshold | integer | 低液位阈值 |
| default_dose_ms | integer | 默认投加时长 |
| fault_policy | enum | 降级 / 跳过 / 安全结束 |

**规则**

- 被禁用通道不能出现在可执行阶段动作中。
- `fault_policy` 只适用于工艺/资源类故障。
- 对每次投加动作都要有超时和确认策略。

## 5. 药剂动作（chemical_action）

**说明**：阶段内某一药剂通道的动作定义。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| channel_id | string | 关联药剂通道 |
| start_condition | enum | 阶段开始 / 到位后 / 中段触发 |
| duration_ms | integer | 投加时长 |
| retry_limit | integer | 最大重试次数 |

## 6. 洗车周期（wash_cycle）

**说明**：一次从启动到结束的执行实例。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| cycle_id | string | 周期唯一标识 |
| selected_program_id | string | 操作员手动选择的程序 |
| detected_vehicle_type_id | string | 自动判定的车辆类别 |
| cycle_state | enum | 待机 / 预检 / 运行中 / 降级中 / 安全停机 / 完成 / 异常结束 |
| current_stage_id | string | 当前阶段 |
| start_time | datetime | 启动时间 |
| end_time | datetime | 结束时间 |
| result_code | enum | 成功 / 安全停机 / 资源降级完成 / 人工终止 / 启动失败 |

**状态迁移**

```text
待机 -> 预检 -> 运行中 -> 完成
待机 -> 预检 -> 启动失败
运行中 -> 降级中 -> 完成
运行中 -> 安全停机
运行中 -> 异常结束
安全停机 -> 待机
异常结束 -> 待机
完成 -> 待机
```

## 7. 告警事件（fault_event）

**说明**：运行过程中的异常、联锁和运维事件。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| event_id | string | 事件唯一标识 |
| event_time | datetime | 发生时间 |
| severity | enum | 信息 / 警告 / 安全 |
| fault_class | enum | 安全类 / 工艺类 / 资源类 / 通信类 / 配置类 |
| source | string | 来源模块 |
| related_cycle_id | string | 关联洗车周期 |
| disposition | enum | 停机 / 降级 / 跳过 / 安全结束 / 记录 |
| operator_ack_required | bool | 是否要求人工确认 |

**规则**

- `severity=安全` 时，`disposition` 不得为“记录”。
- 所有触发停机或降级的事件都必须写入日志。

## 8. 安全联锁（safety_interlock）

**说明**：限制自动运行的安全条件集合。

**核心字段**

| 字段 | 类型 | 说明 |
|------|------|------|
| interlock_id | string | 联锁标识 |
| interlock_name | string | 联锁名称 |
| current_state | enum | 就绪 / 触发 / 屏蔽 |
| trip_action | enum | 禁止启动 / 立即停机 |
| reset_rule | enum | 自动恢复 / 人工复位后恢复 |

## 9. 领域关系

```text
vehicle_type 1---* wash_program
wash_program 1---* wash_stage
wash_stage 1---* chemical_action
chemical_action *---1 chemical_channel
wash_cycle *---1 wash_program
wash_cycle *---1 vehicle_type
wash_cycle 1---* fault_event
safety_interlock 1---* fault_event
```
