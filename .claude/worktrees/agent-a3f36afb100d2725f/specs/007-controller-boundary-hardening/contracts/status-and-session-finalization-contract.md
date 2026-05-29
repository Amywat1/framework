# 契约：只读查询与会话结束落点

## 1. 目的

定义 `status` 查询的只读语义，以及会话结束后最终状态只能落到唯一、可解释位置的规则。

## 2. 契约范围

- `include/application/use_cases/query_wash_session_status.h`
- `src/application/use_cases/query_wash_session_status.c`
- `wash_session_t`、`wash_execution_t`、`wait_condition_t`
- 最近结果与迁移记录投影
- 验证目标：`test_contract_session_finalization`、`test_contract_status_query_readonly`、`test_integration_unique_session_result_sink`、`test_integration_status_query_no_side_effect`

## 3. 状态查询契约

`query_wash_session_status_execute()` 必须：

1. 只从 `system_context_t` 和现有运行对象构造 `wash_session_status_view_t`
2. 返回当前视图，而不是推进业务状态
3. 不得刷新 `last_result_code`、`last_reason_code`、`last_transition_record` 或 `global_fault_*`

## 4. 会话结束落点契约

1. 会话最终结果只允许落在 `wash_session.final_session_result`
2. 当前执行结果只允许落在 `wash_execution.execution_result`
3. 最近结果只表示“最近一次正式处理对外结论”，不能替代会话终态
4. 状态视图只是读取这些对象的快照，不新增新的最终解释源

## 5. 一致性规则

| 场景 | 必须成立的关系 |
|------|----------------|
| 正常完成 | `wash_session.final_session_result`、最近结果、最近迁移记录语义一致 |
| 异常中止 | `wash_session.abort_reason`、`wash_execution.reason_code` 与最近结果可相互解释 |
| 状态查询 | 查询前后的最近结果、故障状态和会话终态完全一致 |

## 6. 验收场景

1. **给定** 系统已存在最近结果与全局故障状态，**当** 连续执行多次 `status` 查询，**则** 查询前后的关键运行状态必须完全一致。
2. **给定** 一次会话正常完成，**当** 检查 `wash_session`、最近结果和迁移记录，**则** 三者对最终结论的解释必须一致，且最终会话结果只落在 `wash_session.final_session_result`。
3. **给定** 一次会话因故障中止，**当** 检查状态视图，**则** 视图只能读取并呈现现有对象事实，不能通过查询动作生成新的业务结果。
