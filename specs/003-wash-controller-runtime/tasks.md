# 任务：洗车主控运行骨架

## Phase 1：准备阶段

- [X] T001 更新 `tests/CMakeLists.txt`，注册主控运行骨架新增的 contract / integration / simulation 测试文件。
- [X] T002 [P] 扩展 `tests/test_support.h`，提供常驻主控、命令注入、状态查询和超时推进的共享测试 helper。
- [X] T003 [P] 对齐 `src/CMakeLists.txt`、`src/main/main.c`、`src/main/wash_cli_main.c` 的运行入口职责，明确 `wash_controller` 是正式主控入口，`wash_cli` 仅用于兼容或调试。

## Phase 2：基础阶段

- [X] T004 在 `include/application/coordinators/system_context.h` 增加主控级全局故障、最近结果和最近原因字段，并同步初始化 `src/main/main.c`、`src/main/wash_cli_main.c`、`tests/test_support.h`。
- [X] T005 [P] 扩展 `include/application/dto/command_dto.h` 和 `include/application/use_cases/query_wash_session_status.h`，补齐 `program_id`、`signal_code`、`fault_code`、`reason`、`wait_condition`、`global_fault` 等正式协议字段。
- [X] T006 [P] 为本期涉及的公开头文件补齐中文 Doxygen 注释。
- [X] T007 [P] 审查并复用 `wait_timeout_service` / `trigger_priority_service`，在主循环与测试中显式接入。

## Phase 3：用户故事 1 - 常驻主控连续接单

### 验证任务

- [X] T008 [P] [US1] 新增 `tests/integration/test_controller_runtime_persistence.c`，验证空闲等待、单次结束后继续接单和同进程内连续处理多单。
- [X] T009 [P] [US1] 新增 `tests/simulation/test_controller_continuous_runtime.c`，验证无输入时主循环持续等待且不会因“无事可做”退出。

### 实现任务

- [X] T010 [US1] 重构 `include/platform/linux/main_loop.h` 和 `src/platform/linux/main_loop.c`，提供常驻主控所需的持续 tick / 待处理触发推进接口，并显式接入 `wait_timeout_service` / `trigger_priority_service`。
- [X] T011 [US1] 修改 `src/main/main.c`，让 `wash_controller` 启动后长期保留 `system_context`、循环处理事件和超时，并在任务结束后继续等待下一单。
- [X] T012 [US1] 调整 `src/application/use_cases/process_wash_trigger.c` 和 `src/application/use_cases/start_wash_cycle.c`，确保新任务开始与结束只重置任务级状态，不重建主控级上下文。

## Phase 4：用户故事 2 - 通过正式输入与预检驱动任务

### 验证任务

- [X] T013 [P] [US2] 新增 `tests/contract/test_controller_stdin_protocol.c`，验证 `start / stop / feedback / fault / fault clear / status` 命令格式和单行响应契约。
- [X] T014 [P] [US2] 新增 `tests/integration/test_controller_command_paths.c`，验证 `stdin` 正常启动、预检失败、空闲 `stop`/`feedback` 忽略，以及运行中 `feedback` / `fault` 路径。
- [X] T015 [P] [US2] 新增 `tests/simulation/test_controller_global_fault_behavior.c`，验证空闲 `fault`、`start` 阻断和 `fault clear` 清除的最小全局故障职责。

### 实现任务

- [X] T016 [US2] 修改 `include/adapters/inbound/cli_command_adapter.h` 和 `src/adapters/inbound/cli_command_adapter.c`，实现 `stdin` 行命令解析、单行 `stdout` 响应和错误命令持续运行。
- [X] T017 [US2] 修改 `include/application/use_cases/start_wash_cycle.h`、`src/application/use_cases/start_wash_cycle.c`、`include/domain/services/precheck_service.h`、`src/domain/services/precheck_service.c`，把预检接入 `start` 受理门槛并记录失败原因。
- [X] T018 [US2] 扩展 `src/application/use_cases/process_wash_trigger.c`、`include/application/use_cases/acknowledge_fault.h`、`src/application/use_cases/acknowledge_fault.c`、`include/application/use_cases/stop_wash_cycle.h`、`src/application/use_cases/stop_wash_cycle.c`，实现空闲 `fault` 记录、`fault clear` 清除、空闲 `stop`/`feedback` 忽略和运行中触发路径。
- [X] T019 [US2] 调整 `src/main/main.c` 和 `src/main/wash_cli_main.c`，把正式 `stdin` 通道只接入 `wash_controller` 常驻主控，`wash_cli` 保持兼容或调试用途。

## Phase 5：用户故事 3 - 持续可观测并覆盖关键边界

### 验证任务

- [X] T020 [P] [US3] 新增 `tests/contract/test_controller_status_contract.c`，验证 `status` 单行摘要字段、最近原因和全局故障可见性契约。
- [X] T021 [P] [US3] 更新 `tests/integration/test_session_status_observability.c`，验证运行中、空闲、有全局故障和任务结束后的状态查询结果。
- [X] T022 [P] [US3] 更新 `tests/integration/test_session_exception_paths.c` 和 `tests/simulation/test_trigger_priority_competition.c`，验证关键迁移记录、真实触发类型和迟到/重复事件隔离。

### 实现任务

- [X] T023 [US3] 扩展 `include/application/use_cases/query_wash_session_status.h` 和 `src/application/use_cases/query_wash_session_status.c`，返回等待条件、最近原因、全局故障存在性和全局故障原因。
- [X] T024 [US3] 调整 `include/domain/ports/event_logger_port.h`、`include/adapters/logging/file_event_logger.h`、`src/adapters/logging/file_event_logger.c`，承接启动拒绝、预检失败、空闲 `fault`、`fault clear`、超时、完成和中止等关键记录。
- [X] T025 [US3] 修正 `src/application/use_cases/process_wash_trigger.c`、`src/platform/linux/main_loop.c` 和 `include/application/coordinators/system_context.h` 的最近原因、真实触发类型和连续任务隔离逻辑。

## Phase 6：收尾与跨故事事项

- [ ] T026 [P] 如新增测试文件需要统一入口，更新 `tests/contract/test_main.c`、`tests/integration/test_main.c`、`tests/simulation/test_main.c`、`tests/unit/test_main.c`，保持测试目录入口一致。
- [ ] T027 运行并收口 `quickstart.md` 等效流程，最终核对 `src/main/main.c`、`src/adapters/inbound/cli_command_adapter.c`、`src/platform/linux/main_loop.c` 的行为满足场景 A-F。

## 说明

- 当前实现已覆盖核心代码和自动化测试文件，但本机缺少 `cmake/gcc/clang`，因此 `T026`、`T027` 对应的真实构建与 quickstart 回归尚未本地验证。
