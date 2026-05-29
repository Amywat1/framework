# 契约：正式命令路径收敛

## 1. 目的

本契约定义控制器正式命令从 stdin 进入到生成响应时必须遵守的唯一正式路径，以及测试 helper 的允许边界。

## 2. 正式路径

对于 `start`、`stop`、`feedback`、`fault`、`fault clear`、`status` 六类正式命令：

1. 命令首先由 `cli_command_adapter_prepare_line()` 或等价正式受理逻辑解析。  
2. 写命令必须转换为 trigger 并进入 `main_loop_submit_trigger()`。  
3. 主循环通过 `main_loop_run()` 消费 pending trigger，并由 `process_wash_trigger_execute()` 完成业务分发。  
4. 响应必须由 `cli_command_adapter_finalize_trigger_response()` 或等价统一响应逻辑生成。  

## 3. 允许的例外

- `status` 可以复用内部只读查询接口获取状态视图。
- 但 `status` 作为正式 stdin 命令时，仍必须被正式受理逻辑接住，不能成为绕过正式路径的 stdin 例外。

## 4. 禁止事项

- 禁止正式 stdin 直接调用 `cli_command_adapter_process_line()` 这类直调业务路径。
- 禁止新增第二套“解析后立即执行业务核心”的旁路。
- 禁止测试代码依赖旁路来证明主流程成立。

## 5. 测试 helper 边界

测试 helper 允许：
- 驱动正式路径的命令输入或 trigger 入队动作
- 推进主循环时间和排空逻辑
- 读取内部只读状态视图

测试 helper 不允许：
- 直接调用业务处理核心形成正式路径外的第二套执行业务链
- 通过专用 helper 绕过正式受理、入队或统一响应逻辑

## 6. 验收信号

- 正式 stdin 六类命令都能映射到同一正式路径。
- 现有测试若需要 helper，只能证明正式路径行为，而不是替代正式路径。
- 没有新的生产代码入口直接调用薄写 use_case 来替代主循环消费。
