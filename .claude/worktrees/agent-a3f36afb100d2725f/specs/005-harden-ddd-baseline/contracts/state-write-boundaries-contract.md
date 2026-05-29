# 契约：核心状态写入边界

## 目标

定义核心运行时状态的唯一主写入边界，使评审者能够直接判断“哪类状态应由谁写”。

## 写入边界表

| 状态类别 | 唯一主写入责任方 | 允许的写入内容 | 禁止直接写入方 |
|------|------|------|------|
| `wash_session` | `wash_session_state_machine` | 创建、运行、完成、终止 | `main.c`、CLI 适配层、`wait_timeout_service` |
| `wash_execution` | `wash_execution_service` | 阶段开始、完成、终止 | `main.c`、CLI 适配层、会话状态机 |
| `wait_condition` | `wash_execution_service` | arm、reset、satisfy | `main.c`、CLI 适配层、主编排入口 |
| timeout 决议 | `wait_timeout_service` | retry/continue/finish/abort 决议事实 | `main.c`、CLI 适配层 |
| `global_fault_*` | 统一主触发编排入口 | 设置、清除全局故障 | 领域服务、CLI 适配层、主循环平台层 |
| `last_result_*` | 统一主触发编排入口 | 最近结果与原因 | 领域服务、CLI 适配层、只读查询 |

## 边界约束

1. 领域服务不得借由整块 `system_context` 越界改写不属于自己的状态类别。
2. `main.c` 与 `main_loop.c` 只负责系统外壳和运行时推进，不承担业务状态所有权。
3. `status` 查询只读取状态视图，不写最近结果、不写故障状态。
4. 应用层统一主触发编排入口负责跨对象编排和最终对外结果落点。

## 验证准则

- 评审代码时，能够逐类指出会话、执行、等待条件、全局故障和最近结果的唯一主写入位置。
- 不再存在领域服务直接依赖应用层记录器并顺带写跨对象状态的路径。
