# 契约：触发编排与运行时写入口

## 1. 目的

定义主循环、正式触发入口和结果投影器之间的职责边界，确保关键运行状态只从固定入口改写。

## 2. 契约范围

- `src/platform/linux/main_loop.c`
- `src/application/use_cases/process_wash_trigger.c`
- `src/application/coordinators/runtime_event_recorder.c`
- `src/application/use_cases/process_formal_command.c`
- `src/adapters/inbound/cli_command_adapter.c`
- 验证目标：`test_contract_trigger_runtime_ownership`、`test_integration_formal_entry_ownership`、`test_integration_compat_entrypoint_removal`

## 3. 主循环职责

`main_loop_*()` 只负责：

1. 推进 `current_time_ms`
2. 维护 `pending_triggers[]` 与 `pending_trigger_count`
3. 在等待超时成立时注入超时触发
4. 选择优先级最高的触发并调用 `process_wash_trigger_execute()`
5. 在无触发或触发处理完成后调用 `process_wash_runtime_tick()`

主循环不得直接：

1. 设置或清除 `global_fault_*`
2. 写入 `last_result_code` / `last_reason_code`
3. 决定会话最终结果

## 4. 正式触发入口职责

`process_wash_trigger_execute()` 负责：

1. 路由 `start / stop / fault / timeout`
2. 组装领域服务最小依赖切片
3. 作为 `global_fault_*` 的唯一正式写入口
4. 驱动跨对象编排与最终结果落点

该入口不得把关键状态写入权继续下放给查询路径、适配层或无关领域服务。

## 5. 结果投影职责

`runtime_event_recorder_*()` 负责：

1. 刷新最近结果码与原因码
2. 应用统一结果投影
3. 更新最近迁移记录并通过日志端口输出

该组件是最近结果和迁移记录的唯一正式落点，不负责业务路由。

## 6. 状态写入矩阵

| 状态 | 正式拥有者 | 触发条件 |
|------|------------|----------|
| `pending_triggers[]` | 主循环 | 收到新触发、注入超时、消费已选中触发 |
| `current_time_ms` | 主循环 | 主循环时间推进 |
| `global_fault_*` | `process_wash_trigger_execute()` | 受理正式故障记录或清除 |
| `last_result_code` / `last_reason_code` | `runtime_event_recorder_*()` | 正式入口完成一次结果投影 |
| `last_transition_record` | `runtime_event_recorder_apply_projection()` | 正式入口需要记录迁移/拒绝/忽略 |

## 7. 验收场景

1. **给定** 主循环推进一次超时，**当** 它注入超时触发时，**则** 只能改写触发队列，不能顺手改写最近结果或全局故障。
2. **给定** 系统受理一次 `fault` 触发，**当** 触发处理完成时，**则** `global_fault_*` 只能由正式入口改写，最近结果只能由统一投影器落点。
3. **给定** 某领域服务返回执行事实，**当** 应用层处理该事实时，**则** 由正式入口决定是否投影到最近结果，而不是由领域服务直接写入总上下文。
