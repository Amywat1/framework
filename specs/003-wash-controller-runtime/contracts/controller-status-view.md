# 接口契约：主控状态查询视图

## 1. 说明

本契约定义 `status` 命令和内部状态查询用例需要暴露的最小状态视图。

## 2. 状态视图字段

| 字段 | 类型 | 说明 |
|------|------|------|
| has_active_session | bool | 是否存在活动任务 |
| session_id | string | 当前会话标识，无任务时可为空 |
| session_state | enum | none / created / running / completed / aborted |
| execution_state | enum | none / running / completed / aborted |
| stage_id | string | 当前阶段标识 |
| wait_condition_id | string | 当前等待条件标识，若适用 |
| latest_reason_code | string | 最近一次关键原因 |
| global_fault_present | bool | 是否存在全局故障 |
| global_fault_reason | string | 最近全局故障原因 |

## 3. 输出语义

- 当存在活动任务时，状态视图必须可判断当前会话和执行推进位置。
- 当任务处于等待状态时，状态视图必须可暴露当前等待条件或至少可定位等待原因。
- 当无活动任务但存在全局故障时，状态视图必须同时体现：
  - 无活动任务
  - 全局故障存在
  - 最近全局故障原因

## 4. 单行摘要要求

`status` 的 `stdout` 单行摘要至少应能映射出以下信息：

```text
session=<...> state=<...> execution=<...> stage=<...> wait=<...> global_fault=<...> reason=<...>
```

允许字段命名调整，但不得丢失关键信息。

## 5. 契约约束

- `status` 查询必须是只读操作。
- 不得因为无活动任务而返回不可判断的空白结果。
- 最近原因字段必须覆盖最近一次关键迁移、拒绝或故障相关结果。
