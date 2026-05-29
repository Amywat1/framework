# 数据模型：洗车工步真实控制模型

本特性引入的不是“更多阶段字段”，而是一组围绕工步段运行、条件控制、退出收尾和异常决议的强类型领域模型。核心目标是取代当前以 `wash_program_t -> wash_stage_t` 为中心的粗粒度表达。

## 1. 洗车程序（`wash_program_v1_t`）

**说明**：表示一个完整可执行的洗车程序，由多个按顺序推进的工步段组成。

| 字段 | 类型 | 说明 |
|------|------|------|
| program_id | 固定字符串 | 程序标识 |
| program_name | 固定字符串 | 程序名称 |
| revision | 整数 | 配置版本 |
| enabled | bool | 是否允许启用 |
| default_segment_timeout_ms | 整数 | 工步默认段内超时 |
| default_exit_timeout_ms | 整数 | 工步默认收尾超时 |
| segments | 固定数组 `wash_segment_t[]` | 工步段列表 |
| segment_count | 整数 | 工步段数量 |

**关系**：
- 一个 `wash_program_v1_t` 包含多个 `wash_segment_t`
- 工步段顺序由数组顺序和 `sequence_no` 双重约束

**校验规则**：
- `program_id` 必须非空且唯一
- `segment_count` 必须大于 0
- 每个工步段的 `segment_id` 必须唯一
- 程序中必须能覆盖规格要求的核心工步段：顶刷段、侧刷段、RO 水段、风干段

## 2. 工步段（`wash_segment_t`）

**说明**：表示一个可独立推进、可持续判断、带正式退出动作的工艺单元。

| 字段 | 类型 | 说明 |
|------|------|------|
| segment_id | 固定字符串 | 工步段标识 |
| segment_name | 固定字符串 | 工步段名称 |
| sequence_no | 整数 | 执行顺序 |
| segment_kind | 枚举 | 顶刷、侧刷、RO 水、风干、停机路径等工步类型 |
| motion_plan | `segment_motion_plan_t` | 本段移动目标与方向 |
| continuous_controls | 固定数组 `continuous_control_t[]` | 持续控制项 |
| conditional_controls | 固定数组 `conditional_control_t[]` | 条件控制项 |
| completion_condition | `segment_completion_condition_t` | 本段完成条件 |
| exit_actions | 固定数组 `exit_action_t[]` | 正式退出动作 |
| segment_timeout_ms | 整数 | 段内超时 |
| exit_timeout_ms | 整数 | 收尾超时 |
| exception_policy | `segment_exception_policy_t` | 异常策略 |

**状态转换**：
1. `SEGMENT_STATE_PENDING`
2. `SEGMENT_STATE_ENTERING`
3. `SEGMENT_STATE_RUNNING`
4. `SEGMENT_STATE_EXITING`
5. `SEGMENT_STATE_COMPLETED`
6. `SEGMENT_STATE_ABORTED`

**校验规则**：
- 每段必须有且只有一个 `completion_condition`
- 每段必须显式定义 `exit_actions`，即使动作列表为空也要表达“无退出动作”
- 若 `conditional_controls` 非空，则必须同时定义位置语义基础
- 若段内存在顶刷跟随、RO 水或风干动作，则相应执行机构品类必须可用

## 3. 工步移动计划（`segment_motion_plan_t`）

**说明**：定义工步段运行时龙门的运动目标和判定语义。

| 字段 | 类型 | 说明 |
|------|------|------|
| direction | 枚举 | `FORWARD` / `REVERSE` / `STOP` |
| target_reference | 枚举 | 到车头、到车尾、回零、相对距离、绝对位置 |
| target_distance_mm | 整数 | 相对或绝对距离，按引用类型解释 |
| target_tolerance_mm | 整数 | 目标判定容差 |
| requires_position_feedback | bool | 是否依赖位置反馈推进 |

**校验规则**：
- `STOP` 方向不得同时声明前进/后退目标
- 使用相对距离时必须说明相对基准
- 使用回零目标时必须提供回零完成判定

## 4. 持续控制项（`continuous_control_t`）

**说明**：在工步段运行期间持续生效，直到本段退出阶段开始或规则显式关闭。

| 字段 | 类型 | 说明 |
|------|------|------|
| control_object | 枚举 | 顶刷、侧刷、RO 水、风干、药剂通道、龙门跟随等 |
| control_mode | 枚举 | 启用、关闭、跟随、固定、持续喷洒、持续吹干等 |
| required_feedback | 枚举 | 是否要求运行反馈、跟随反馈或关闭反馈 |
| fail_policy | 枚举 | 反馈缺失后的失败策略 |

**校验规则**：
- 同一工步段内同一控制对象不得存在互相冲突的持续模式
- 跟随模式必须声明失效时的异常策略

## 5. 条件控制项（`conditional_control_t`）

**说明**：在工步段运行期间按位置或业务条件触发开始和停止的控制规则。

| 字段 | 类型 | 说明 |
|------|------|------|
| control_id | 固定字符串 | 条件控制标识 |
| control_object | 枚举 | 目标控制对象 |
| start_condition | `position_trigger_t` | 开始条件 |
| stop_condition | `position_trigger_t` | 停止条件 |
| action_on_start | 枚举 | 开始时执行的动作 |
| action_on_stop | 枚举 | 停止时执行的动作 |
| active_during_exit | bool | 退出阶段是否允许继续保持激活 |

**校验规则**：
- 每条条件控制必须同时定义开始条件和停止条件
- `active_during_exit=false` 时，退出阶段开始必须强制关闭
- 同一对象的条件窗口不得形成不可解释的重叠冲突

## 6. 位置触发器（`position_trigger_t`）

**说明**：统一表达条件控制、完成条件和退出动作使用的位置语义。

| 字段 | 类型 | 说明 |
|------|------|------|
| semantics_type | 枚举 | 相对位置、绝对位置、到车头、到车尾、回零完成 |
| compare_mode | 枚举 | 大于等于、小于等于、进入区间、离开区间 |
| value_mm | 整数 | 需要时的距离值 |
| tolerance_mm | 整数 | 判定容差 |

**校验规则**：
- 到车头/到车尾语义不再混用文本信号名
- 进入区间必须同时有上下边界
- 所有位置触发器都必须能映射到同一份运行时位置快照

## 7. 本段完成条件（`segment_completion_condition_t`）

**说明**：定义何时认定“本段运行目标已满足”，但不代表退出收尾已完成。

| 字段 | 类型 | 说明 |
|------|------|------|
| completion_type | 枚举 | 到头、到尾、到相对距离、到绝对位置、等待特定反馈 |
| trigger | `position_trigger_t` 或反馈条件 | 完成判定规则 |
| requires_all_controls_started | bool | 是否要求段内必需控制已成功启用 |

**校验规则**：
- 本段完成条件不得复用为收尾完成条件
- 若依赖反馈，则必须定义超时与失败结果

## 8. 退出动作（`exit_action_t`）

**说明**：工步段完成后必须执行的正式收尾动作。

| 字段 | 类型 | 说明 |
|------|------|------|
| action_id | 固定字符串 | 退出动作标识 |
| control_object | 枚举 | 目标对象 |
| exit_command | 枚举 | 停止、关闭、回零、解除跟随等 |
| completion_feedback | 枚举 | 完成该动作所需反馈 |
| timeout_ms | 整数 | 动作超时 |
| fail_policy | 枚举 | 失败后立即中止或先安全收尾 |

**校验规则**：
- 顶刷回零必须作为正式退出动作出现
- RO 水关闭和风干关闭必须具备可观察完成条件
- 同一对象退出动作顺序必须可解释，例如先停药再停刷或反之

## 9. 工步异常策略（`segment_exception_policy_t`）

**说明**：定义异常发生时，本段如何中止或先收尾后停机。

| 字段 | 类型 | 说明 |
|------|------|------|
| on_position_lost | 枚举 | 位置信号丢失时策略 |
| on_follow_lost | 枚举 | 跟随失效时策略 |
| on_exit_failure | 枚举 | 退出动作失败时策略 |
| on_segment_timeout | 枚举 | 段内超时策略 |
| on_exit_timeout | 枚举 | 收尾超时策略 |
| retry_limit | 整数 | 允许的有限重试次数 |

**校验规则**：
- 段内超时和收尾超时必须分开配置
- 直接中止与先收尾再停机必须二选一，不得含糊

## 10. 执行机构品类（`actuator_category_t`）

**说明**：统一表达平台层可控制的对象种类，为领域层提供强类型控制目标。

| 枚举值 | 说明 |
|------|------|
| `ACTUATOR_GANTRY` | 龙门运动 |
| `ACTUATOR_ROOF_BRUSH` | 顶刷 |
| `ACTUATOR_SIDE_BRUSH` | 侧刷 |
| `ACTUATOR_CHEMICAL` | 药剂通道 |
| `ACTUATOR_RO_WATER` | RO 水冲洗 |
| `ACTUATOR_DRYER` | 风干机构 |

**校验规则**：
- 目标流程中出现的每个控制对象都必须能映射到一个执行机构品类
- 若现有端口缺少 `ACTUATOR_RO_WATER` 或 `ACTUATOR_DRYER`，本期必须补齐

## 11. 位置快照（`position_snapshot_t`）

**说明**：平台层提供给领域层的统一位置事实。

| 字段 | 类型 | 说明 |
|------|------|------|
| position_valid | bool | 位置是否有效 |
| gantry_absolute_mm | 整数 | 龙门绝对位置 |
| distance_to_vehicle_head_mm | 整数 | 到车头相对距离 |
| distance_to_vehicle_tail_mm | 整数 | 到车尾相对距离 |
| home_reached | bool | 是否回零完成 |
| head_reached | bool | 是否到达车头 |
| tail_reached | bool | 是否到达车尾 |

**校验规则**：
- `position_valid=false` 时不得继续正常推进位置驱动工步
- 同一运行循环中所有条件判断必须基于同一份快照

## 12. 工步运行时视图（`segment_runtime_state_t`）

**说明**：应用层编排当前工步推进时使用的运行时状态视图。

| 字段 | 类型 | 说明 |
|------|------|------|
| current_segment_index | 整数 | 当前工步索引 |
| lifecycle_state | 枚举 | 进入、运行、退出、完成、异常 |
| active_conditional_controls | 位图或固定数组 | 当前已激活的条件控制 |
| entered_at_ms | 整数 | 进入时间 |
| exit_started_at_ms | 整数 | 收尾开始时间 |
| segment_result | 枚举 | 本段当前结果 |
| segment_reason | 枚举/字符串码 | 本段原因 |

**状态转换**：
1. 进入段成功后进入 `RUNNING`
2. 满足本段完成条件后进入 `EXITING`
3. 退出动作全部完成后进入 `COMPLETED`
4. 任何不可恢复异常可直接进入 `ABORTED`

## 关系总览

1. `wash_program_v1_t` 包含多个 `wash_segment_t`
2. 每个 `wash_segment_t` 包含一个 `segment_motion_plan_t`
3. 每个 `wash_segment_t` 可包含多个 `continuous_control_t`
4. 每个 `wash_segment_t` 可包含多个 `conditional_control_t`
5. 每个 `wash_segment_t` 包含一个 `segment_completion_condition_t`
6. 每个 `wash_segment_t` 可包含多个 `exit_action_t`
7. `conditional_control_t`、`segment_completion_condition_t` 和 `exit_action_t` 共享 `position_snapshot_t` 作为判定事实
8. `segment_runtime_state_t` 是应用层围绕上述领域模型做的运行时编排视图
