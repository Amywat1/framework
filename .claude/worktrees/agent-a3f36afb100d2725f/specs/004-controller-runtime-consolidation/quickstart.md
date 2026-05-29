# 快速开始：控制器运行时收敛

## 1. 目标

本指南用于验证 `004-controller-runtime-consolidation` 的设计目标已经落地：

1. 正式 stdin 命令只走一条正式路径  
2. 薄写 use_case 不再承载另一套主业务入口  
3. 最近结果、状态摘要、日志和响应语义一致  
4. `global_fault`、session、execution、wait 的写入责任边界清晰  
5. `main.c` 仅保留入口和主循环骨架

## 2. 前置条件

- 已完成本特性的实现改动
- CMake 与 C17 编译环境可用
- 仓库根目录下可运行测试

## 3. 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 4. 重点验证测试

优先检查以下测试是否通过：

- `tests/contract/test_controller_stdin_protocol.c`
- `tests/integration/test_controller_command_paths.c`
- `tests/integration/test_session_status_observability.c`
- `tests/simulation/test_controller_global_fault_behavior.c`
- `tests/simulation/test_trigger_priority_competition.c`

## 5. 代码审查清单

### A. 正式路径收敛

检查：
- `main.c` 是否仍通过 `prepare -> submit -> run -> finalize` 协调正式命令
- 生产代码是否还存在 `cli_command_adapter_process_line()` 一类正式旁路
- `status` 的 stdin 路径是否也经过正式受理逻辑

预期：
- 六类正式命令只有一条正式生产路径
- helper 或内部查询接口不再替代正式路径

### B. 薄写入口收缩

检查：
- `start_wash_cycle.c`
- `stop_wash_cycle.c`
- `acknowledge_fault.c`

预期：
- 这些文件如继续保留，也只作为兼容性辅助包装，不再作为正式主业务入口
- `process_wash_trigger_execute()` 仍是唯一业务分发核心

### C. 结果语义一致

手动或测试验证以下场景：

1. `stop` 在空闲时返回 ignored，并刷新最近结果  
2. unmatched / duplicate / late feedback 返回 ignored，并刷新最近结果  
3. `fault clear` 在有/无 `global_fault` 两种场景下都保持当前既有外部语义，但其最近结果、`status` 和日志必须对齐  
4. `status`、CLI 响应和日志对同一事件给出一致结论  

### D. 责任边界

检查：
- `wash_session_state_machine` 是否独占 session 迁移
- `wash_execution_service` 是否独占 execution/wait 推进
- `wait_timeout_service` 是否只负责 timeout 裁决
- `process_wash_trigger_execute()` 是否是唯一 `global_fault` set/clear 方

### E. 主入口变薄

检查 `src/main/main.c` 是否只保留：
- 初始化
- 等待 stdin / timeout
- 时间推进
- 调用正式命令受理
- 排空运行时

预期：
- 命令分支细节和记录细节已下沉，不再堆积在顶层入口
