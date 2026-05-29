# 接口契约：洗车程序配置

## 1. 说明

本契约定义维护人员可编辑的洗车程序配置结构，用于配置导入、导出、校验和持久化。

## 2. 配置顶层对象

| 字段 | 类型 | 说明 |
|------|------|------|
| program_id | string | 程序唯一标识 |
| program_name | string | 程序名称 |
| enabled | bool | 是否允许选择 |
| applicable_vehicle_types | array<string> | 适用车辆类别 |
| default_timeout_ms | integer | 默认超时 |
| stages | array<object> | 阶段配置 |
| revision | integer | 配置版本 |

## 3. 阶段对象

| 字段 | 类型 | 说明 |
|------|------|------|
| stage_id | string | 阶段唯一标识 |
| stage_name | string | 阶段名称 |
| sequence_no | integer | 阶段顺序 |
| gantry_motion_mode | enum | stop / forward / reverse / traverse |
| traverse_count | integer | 往返次数 |
| roof_brush_mode | enum | disabled / follow / fixed |
| side_brush_mode | enum | disabled / follow / fixed |
| rinse_enabled | bool | 是否冲洗 |
| stage_timeout_ms | integer | 阶段超时 |
| skip_on_resource_fault | bool | 资源故障是否允许跳过 |
| chemical_actions | array<object> | 药剂动作列表 |

## 4. 药剂动作对象

| 字段 | 类型 | 说明 |
|------|------|------|
| channel_id | string | 药剂通道标识 |
| start_condition | enum | on_stage_start / on_position_reached / mid_stage |
| duration_ms | integer | 投加时长 |
| retry_limit | integer | 最大重试次数 |

## 5. 校验规则

- `program_id`、`stage_id` 必须唯一。
- `sequence_no` 必须连续递增。
- `stage_timeout_ms` 和 `duration_ms` 必须大于 0。
- 被引用的车辆类别和药剂通道必须存在。
- 资源类故障可跳过，不得覆盖安全类故障停机规则。
