# 契约：状态写入责任边界

## 1. 目的

本契约定义运行时关键状态的唯一写入责任方，防止主循环、适配器和业务核心之间继续出现越界状态修改。

## 2. 责任映射

| 状态 | 允许写入方 | 不允许写入方 |
|------|------------|--------------|
| `wash_session` | `wash_session_state_machine` | `main.c`、CLI 适配器、`wait_timeout_service` |
| `wash_execution` | `wash_execution_service` | `main.c`、CLI 适配器、`wash_session_state_machine` |
| `wait_condition` 推进 | `wash_execution_service` | `main.c`、CLI 适配器 |
| `wait_condition` timeout 裁决 | `wait_timeout_service` | `main.c`、CLI 适配器、`process_wash_trigger` |
| `global_fault_present/code/reason` | `process_wash_trigger_execute()` | `main_loop.c`、`wait_timeout_service`、CLI 适配器 |
| 最近结果与最近记录 | 统一记录辅助逻辑 | 分散命令分支 |

## 3. 高层策略约束

`process_wash_trigger_execute()` 可以：
- 决定路由到哪个服务
- 应用高层策略
- 唯一 set/clear `global_fault`
- 调用统一结果记录逻辑

`process_wash_trigger_execute()` 不可以：
- 越权手写 session 迁移细节
- 越权手写 execution / wait 推进细节
- 复制 timeout 裁决逻辑

## 4. 主入口与主循环约束

- `main.c` 只保留入口和骨架，不直接改业务状态。
- `main_loop.c` 只负责入队、选取和消费，不直接承担业务状态写入。

## 5. 验收信号

- 典型链路 `start` / `stop` / `fault` 中，各类状态都能追溯到唯一责任边界。
- 代码评审时不再出现主循环层、CLI 层或 helper 层直接写 `global_fault`、`wash_session` 或 `wash_execution` 的主路径逻辑。
