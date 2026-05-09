# 契约：组合根与总上下文边界

## 1. 目的

定义 `system_context_t` 的正式定位：它是应用层组合根，不是领域层可随意依赖和改写的公共状态池。

## 2. 契约范围

- `include/application/coordinators/system_context.h`
- `src/application/coordinators/system_context_private.h`
- 应用层正式入口和协调器
- 领域服务参数切片（现有 `*_service_args_t`）
- 验证目标：`test_contract_system_context_boundary`、`test_unit_service_arg_slices`

## 3. 共享状态准入规则

| 类别 | 允许进入 `system_context_t` | 不允许进入 `system_context_t` |
|------|-----------------------------|-------------------------------|
| 运行对象 | 会话、执行、等待、程序快照、运行快照 | 仅用于单次调用的局部缓存 |
| 结果投影 | 最近结果、最近迁移记录 | 临时格式化字符串、展示拼装中间值 |
| 调度事实 | 触发队列、当前时间、序列号 | 局部循环变量、单次超时判断中间值 |
| 端口 | 传感器、执行器、日志、程序仓储端口 | 与领域服务职责无关的便利指针 |

## 4. 领域依赖规则

1. 领域服务不得以 `system_context_t *` 作为正式输入参数。
2. 领域服务只能接收完成职责所需的切片对象。
3. 新增服务时，若发现需要直接传入 `system_context_t` 才能工作，默认视为边界设计失败，必须先拆分最小输入。
4. `include/application/coordinators/system_context.h` 不得公开关键运行状态字段；完整布局只能保留在私有头中。

## 5. 写入规则

| 状态类别 | 合法写入者 | 非法写入者示例 |
|------|------------|----------------|
| 全局故障 | `process_wash_trigger_execute()` | 查询路径、适配层、普通领域服务 |
| 最近结果/迁移记录 | `runtime_event_recorder_*()` | CLI 查询、领域状态机、主循环 |
| 触发队列 | `main_loop_submit_trigger()`、主循环内部队列维护函数 | 领域服务、状态查询 |
| 当前时间 | `main_loop_advance_time()` | 应用用例、领域服务 |

## 6. 验收场景

1. **给定** 某领域服务只需要 `wash_execution`、`wait_condition` 和端口，**当** 设计其输入时，**则** 必须使用最小依赖切片，而不是传入整个 `system_context_t`。
2. **给定** 某新需求引入局部临时状态，**当** 决定其数据落点时，**则** 该状态不得默认进入 `system_context_t`，除非能说明共享必要性和最终解释责任。
3. **给定** 某路径尝试直接写入最近结果或全局故障，**当** 评审其边界时，**则** 必须证明其属于正式写入口，否则视为违规。
