# 快速开始：DDD 架构基线加固

## 1. 目标

本指引用于验证 `005-harden-ddd-baseline` 的设计目标已经落地：

1. 领域层不再反向依赖应用层记录职责或 `system_context` 万能透传。
2. 统一主触发编排入口承担正式事件编排，`status` 纳入统一正式入口但保持只读。
3. `global_fault`、最近结果以及会话/执行/等待条件/超时决议的主写入边界清晰。
4. 最近结果、状态摘要、日志和 CLI 响应的语义映射统一。
5. 单进程、单主循环、`select + timeout + stdin` 和 trigger 队列模型不变。

## 2. 前置条件

- 已完成本特性的实现改动
- CMake 与 C17 编译环境可用
- 仓库根目录下可执行相关测试

## 3. 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 4. 重点回归测试

优先检查以下测试：

- `tests/contract/test_controller_stdin_protocol.c`
- `tests/contract/test_controller_status_contract.c`
- `tests/integration/test_controller_command_paths.c`
- `tests/integration/test_session_status_observability.c`
- `tests/simulation/test_controller_global_fault_behavior.c`
- `tests/unit/test_wait_timeout_service.c`
- 与领域服务签名调整直接相关的单元测试

## 5. 代码评审检查清单

### A. 依赖方向

检查：
- `src/domain/services/*.c` 和对应头文件中是否仍直接包含应用层记录器或 `application/coordinators/system_context.h`
- 领域服务是否只接收自己真正需要的模型、端口与时间数据

预期：
- 领域层不再反向依赖应用层
- 领域返回事实或决议，不直接拼装 CLI/日志/最近结果

### B. 统一正式入口

检查：
- `cli_command_adapter_execute_formal_line()` 是否仍是正式命令统一入口
- `status` 是否也经由统一正式入口，但保持只读且不入队
- 写命令是否继续采用 `prepare -> submit -> run -> finalize`

预期：
- 正式 `stdin` 不再存在业务旁路
- 内部只读查询可以保留，但不替代正式入口

### C. 结果投影统一

检查：
- 最近结果是否只由统一主触发编排入口写入
- `status` 查询是否不刷新最近结果
- 日志、状态摘要和 CLI 是否共享同一事件结论

预期：
- 接受、拒绝、忽略、状态迁移、超时决议、故障清除的对外语义一致

### D. 状态写入边界

检查：
- `wash_session_state_machine` 是否只负责 session 写入
- `wash_execution_service` 是否只负责 execution/wait 写入
- `wait_timeout_service` 是否只产出 timeout 决议
- `global_fault` 是否只由统一主触发编排入口设置/清除

预期：
- 核心状态均能直接追溯到唯一主写入边界

### E. 顶层入口变薄

检查 `src/main/main.c` 是否只保留：
- 初始化
- 等待 `stdin` / timeout
- 时间推进
- 调用正式受理与排空逻辑

预期：
- 顶层入口不再堆积业务细节、记录细节或跨对象状态写入细节
## 6. 2026-05-07 WSL 验证记录

本次在 WSL `Ubuntu-24.04` 中完成实际构建与回归，仓库路径为 `/mnt/c/Users/HUAWEI/Desktop/Code-M8`。

执行命令：

```bash
cmake -S . -B build-wsl
cmake --build build-wsl -j2
ctest --test-dir build-wsl --output-on-failure
```

验证结果：

- `cmake -S . -B build-wsl` 配置成功
- `cmake --build build-wsl -j2` 编译成功，无编译错误
- `ctest --test-dir build-wsl --output-on-failure` 执行 22/22 项测试，100% 通过

本次回归覆盖了正式 `stdin` 命令路径、`status` 只读路径、`global fault` 行为、领域服务边界以及主触发编排流程，对外行为未出现回归。