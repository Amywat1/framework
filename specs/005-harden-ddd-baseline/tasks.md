# 任务：DDD 架构基线加固

**输入**：来自 `specs/005-harden-ddd-baseline/` 的设计文档  
**前置条件**：`plan.md`、`spec.md`、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证**：每个用户故事都必须先补齐回归验证，再实施代码收敛；所有验证都以“保持正式命令对外语义稳定、保持单进程单主循环模型不变”为前提。

**组织方式**：任务按用户故事分组，保证每个故事都能独立实现、独立验证和独立回归。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（`US1`、`US2`、`US3`、`US4`）
- 每个任务都包含准确文件路径
- 每个任务都附带 `验证：` 说明

## Phase 1：准备阶段（共享基础）

**目的**：先把测试入口、文档约束和构建清单对齐，为后续结构收敛提供共同基线。

- [X] T001 更新 `tests/CMakeLists.txt`，为 `tests/unit/test_wash_execution_service.c` 和本特性新增的契约/回归用例预留并注册测试目标。验证：`ctest -N` 或等效测试清单中能看到新增或调整后的目标。
- [X] T002 [P] 扩展 `tests/test_support.h`，提供驱动 `cli_command_adapter_execute_formal_line()`、读取 `system_context_t` 最近结果/全局故障/状态摘要的通用 helper。验证：测试不需要直穿业务细节即可复现统一正式入口和结果观测路径。
- [X] T003 [P] 校准 `include/adapters/inbound/cli_command_adapter.h`、`include/application/coordinators/system_context.h`、`include/application/use_cases/query_wash_session_status.h` 的中文 Doxygen 注释，固化“统一正式入口”和“`status` 只读不写最近结果”边界。验证：公开头文件中的职责描述与规格一致。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：先建立所有故事共享的编排与投影支点，避免每个故事各自补一套语义映射。

**关键约束**：完成本阶段前，不开始任何用户故事实现任务。

- [X] T004 在 `include/application/coordinators/runtime_result_projection.h` 和 `src/application/coordinators/runtime_result_projection.c` 中定义运行时结果投影结构、投影枚举和投影生成接口，同时更新 `src/CMakeLists.txt` 接入新模块；新公开头文件中的结构、枚举和函数声明必须补齐中文 Doxygen 注释。验证：应用层已有单一投影模型可承载最近结果、状态摘要、日志和 CLI 所需结论，且公开接口注释满足宪章要求。
- [X] T005 重构 `include/application/coordinators/runtime_event_recorder.h` 和 `src/application/coordinators/runtime_event_recorder.c`，让记录器只负责消费投影结果并落地最近结果、迁移记录和日志，不再承担投影决策。验证：记录器调用方不再手写分散的结果参数组合。
- [X] T006 收敛 `include/domain/services/wash_session_state_machine.h`、`include/domain/services/wash_execution_service.h`、`include/domain/services/wait_timeout_service.h` 的公开契约，定义窄化的领域入参/出参结构，去掉领域公开接口对整块 `system_context_t` 的依赖。验证：应用层可以在不透传完整 `system_context_t` 的前提下调用三个核心领域服务。
- [X] T007 在 `include/application/use_cases/process_formal_command.h` 和 `src/application/use_cases/process_formal_command.c` 中定义并实现统一正式命令受理入口，封装 `formal_command_request` 的构建、写命令分流和 `status` 只读分支，同时更新 `src/CMakeLists.txt` 接入新模块。验证：`cli_command_adapter` 与主循环相关入口都通过同一应用层正式入口受理命令，且 `status` 不入 trigger 队列。

**检查点**：到此为止，应用层投影支点和领域服务新契约已经齐备，用户故事可以按优先级推进。

---

## Phase 3：用户故事 1 - 领域层只表达事实与决策（优先级：P1）MVP

**目标**：让领域层只返回领域事实、状态变化和原因信息，不再直接依赖应用层记录器、CLI 语义或整块运行时上下文。

**独立验证方式**：检查三个核心领域服务的头文件与实现，确认它们返回领域事实或超时决议，且不再直接包含 `application/coordinators/runtime_event_recorder.h` 或把 `system_context_t *` 作为主输入。

### 用户故事 1 的验证任务

- [X] T008 [P] [US1] 更新 `tests/unit/test_wash_session_state_machine.c`，断言会话状态机只产出会话迁移事实与原因信息，而不负责最近结果或日志落点。验证：领域层职责越界时单元测试会失败。
- [X] T009 [P] [US1] 新增 `tests/unit/test_wash_execution_service.c` 并补充到 `tests/CMakeLists.txt`，覆盖执行推进、等待条件更新和停止/故障处理返回的领域事实。验证：执行服务的返回值足以驱动应用编排且不依赖应用层记录器。
- [X] T010 [P] [US1] 更新 `tests/unit/test_wait_timeout_service.c`，锁定 `wait_timeout_service` 只返回超时决议事实，不直接写最近结果或接管会话/执行状态。验证：超时服务越界写状态时测试会失败。

### 用户故事 1 的实现任务

- [X] T011 [US1] 重构 `include/domain/services/wash_session_state_machine.h` 和 `src/domain/services/wash_session_state_machine.c`，改为接收窄化的会话输入并返回显式会话迁移事实，而不是直接调用 `runtime_event_recorder`。验证：实现文件中不再直接包含应用层记录器头文件。
- [X] T012 [US1] 重构 `include/domain/services/wash_execution_service.h` 和 `src/domain/services/wash_execution_service.c`，把执行推进、等待条件写入和执行结果封装为领域事实返回值，移除对 `system_context_t` 透传与直接日志记录的依赖。验证：执行服务仅保留 execution/wait 责任，不再直接写 CLI/最近结果。
- [X] T013 [US1] 重构 `include/domain/services/wait_timeout_service.h` 和 `src/domain/services/wait_timeout_service.c`，让超时服务只产出 `timeout_resolution_fact` 风格的决议结果，并补齐领域职责中文注释。验证：超时服务对外只暴露决议，不暴露跨对象写入副作用。

**检查点**：到此为止，领域层已经只表达领域事实与决策，后续编排和投影可由应用层接管。

---

## Phase 4：用户故事 2 - 应用层统一承接主触发编排（优先级：P1）

**目标**：把正式命令和触发事件统一收敛到应用层总入口，`process_wash_trigger` 退化为稳定的路由与编排入口，`status` 也走同一正式入口但保持只读。

**独立验证方式**：沿 `start / stop / feedback / fault / fault clear / status` 六类命令检查正式链路，确认写命令走 `prepare -> submit -> main_loop_run -> finalize`，`status` 走 `prepare -> 只读查询 -> finalize`，且两者都经由统一应用层正式入口。

### 用户故事 2 的验证任务

- [X] T014 [P] [US2] 更新 `tests/contract/test_controller_stdin_protocol.c` 和 `tests/contract/test_controller_status_contract.c`，断言六类正式命令都经由统一正式入口受理，且 `status` 不入队、不刷新最近结果。验证：若出现正式旁路或 `status` 写最近结果，契约测试会失败。
- [X] T015 [P] [US2] 更新 `tests/integration/test_controller_command_paths.c` 和 `tests/integration/test_controller_runtime_persistence.c`，覆盖写命令正式编排路径与 `status` 只读分支。验证：正式主链路保持唯一，且运行时持久状态语义不变。

### 用户故事 2 的实现任务

- [X] T016 [US2] 重构 `include/application/use_cases/process_formal_command.h`、`src/application/use_cases/process_formal_command.c`、`include/application/use_cases/process_wash_trigger.h` 和 `src/application/use_cases/process_wash_trigger.c`，把统一正式命令入口与主触发编排入口收敛为稳定的应用层骨架，细节决策改为消费领域事实和投影辅助。验证：正式入口与主触发入口职责分离清晰，且主入口中不再散落重复的结果拼装和细节日志写入。
- [X] T017 [US2] 重构 `include/adapters/inbound/cli_command_adapter.h` 和 `src/adapters/inbound/cli_command_adapter.c`，让正式命令都进入 `process_formal_command` 统一应用层正式入口，并让 `status` 共享解析路径但走只读查询分支。验证：正式 `stdin` 不再存在业务旁路。
- [X] T018 [US2] 收敛 `include/application/use_cases/start_wash_cycle.h`、`include/application/use_cases/stop_wash_cycle.h`、`include/application/use_cases/acknowledge_fault.h` 与 `src/application/use_cases/start_wash_cycle.c`、`src/application/use_cases/stop_wash_cycle.c`、`src/application/use_cases/acknowledge_fault.c`，把三者降级为兼容性包装，不再代表生产正式主路径。验证：代码搜索中不再把这些薄 use case 视为正式写入口。
- [X] T019 [US2] 调整 `include/application/coordinators/compatibility_trigger_runner.h`、`src/application/coordinators/compatibility_trigger_runner.c`、`include/platform/linux/main_loop.h`、`src/platform/linux/main_loop.c`、`src/main/main.c`，让兼容调用和主循环都只协助 `process_formal_command` 与统一主触发编排入口，而不形成第二套业务编排路径。验证：顶层运行链路可直接指出唯一正式受理与唯一主触发编排入口。

**检查点**：到此为止，正式命令与触发事件已经统一由应用层承接，`process_wash_trigger` 不再继续膨胀为细节堆积点。

---

## Phase 5：用户故事 3 - 核心运行时状态拥有清晰主写入边界（优先级：P1）

**目标**：让会话、执行、等待条件、全局故障和最近结果五类核心状态都能直接定位到唯一主写入边界。

**独立验证方式**：逐类追踪 `wash_session`、`wash_execution`、`wait_condition`、`global_fault_*`、`last_result_*` 的写入点，确认领域层不再借助整块 `system_context_t` 越界改写无关状态，`status` 查询保持只读。

### 用户故事 3 的验证任务

- [X] T020 [P] [US3] 更新 `tests/integration/test_session_status_observability.c` 和 `tests/simulation/test_controller_global_fault_behavior.c`，断言 `global_fault_*` 与 `last_result_*` 只能由统一主触发编排入口改写。验证：跨层直接写故障或最近结果会被回归捕获。
- [X] T021 [P] [US3] 更新 `tests/integration/test_event_driven_session_progression.c` 和 `tests/unit/test_wash_session_state_machine.c`，锁定 session/execution/wait 三类状态各自的唯一写入责任方。验证：状态类别一旦越界写入，测试会出现失败或不一致。

### 用户故事 3 的实现任务

- [X] T022 [US3] 重构 `src/application/use_cases/process_wash_trigger.c`，把 `global_fault_*` 和 `last_result_*` 的最终写入时机与调用位置收口到统一主触发编排入口，并经由 `runtime_result_projection` / `runtime_event_recorder` 完成落地。验证：只有应用层主入口会触发这两类状态的最终写入。
- [X] T023 [US3] 调整 `src/application/use_cases/query_wash_session_status.c`、`include/application/use_cases/query_wash_session_status.h`、`include/application/coordinators/system_context.h`，明确 `status` 只读视图来源并禁止查询路径刷新最近结果或故障状态。验证：`status` 命令只读取状态快照，不带来副作用。
- [X] T024 [US3] 收敛 `src/domain/services/wash_session_state_machine.c`、`src/domain/services/wash_execution_service.c`、`src/domain/services/wait_timeout_service.c` 的内部写入边界，实现“session 只归状态机、execution/wait 只归执行服务、timeout 只归超时决议”。验证：三个领域服务各自只写所属状态类别。
- [X] T025 [US3] 校准 `src/main/main.c`、`src/platform/linux/main_loop.c`、`include/platform/linux/main_loop.h` 的职责边界与防回归注释，确保平台层和顶层入口只负责系统外壳与运行推进。验证：`main.c` 和 `main_loop.c` 中不承担业务状态所有权。

**检查点**：到此为止，五类核心运行时状态都已能直接追溯到唯一主写入边界。

---

## Phase 6：用户故事 4 - 对外运行时语义保持统一且稳定（优先级：P2）

**目标**：让接受、拒绝、忽略、状态迁移、超时决议和故障清除等典型事件在最近结果、状态摘要、日志和 CLI 响应中的表达保持一致，同时保持现有对外行为稳定。

**独立验证方式**：对 accepted / rejected / ignored / transition / timeout / fault-clear 事件逐项比对最近结果、状态摘要、日志和 CLI 响应，确认四者共享同一结论来源；再对正式命令路径做回归，确认外部行为不变。

### 用户故事 4 的验证任务

- [X] T026 [P] [US4] 更新 `tests/contract/test_execution_events_contract.c`、`tests/contract/test_controller_status_contract.c`、`tests/integration/test_controller_command_paths.c`，校验典型事件在最近结果、状态摘要和 CLI 响应中的结论与原因一致。验证：若三处表达不一致，契约或集成测试会失败。
- [X] T027 [P] [US4] 更新 `tests/integration/test_session_exception_paths.c` 和 `tests/simulation/test_controller_global_fault_behavior.c`，覆盖故障清除、空闲 stop、重复 feedback、timeout 等语义一致性回归。验证：典型边界事件的对外语义与既有行为保持稳定。

### 用户故事 4 的实现任务

- [X] T028 [US4] 扩展 `include/application/coordinators/runtime_result_projection.h`、`src/application/coordinators/runtime_result_projection.c`、`include/application/coordinators/runtime_event_recorder.h`、`src/application/coordinators/runtime_event_recorder.c`，补齐 accepted / rejected / ignored / transition / timeout / fault-clear 的统一投影规则，供 CLI、状态摘要和日志共同复用。验证：六类典型事件都能从同一份投影结论生成一致对外表达。
- [X] T029 [US4] 重构 `src/adapters/inbound/cli_command_adapter.c` 和 `src/application/use_cases/process_wash_trigger.c`，让 CLI finalize 响应复用统一投影结果，而不是重复维护独立语义映射。验证：CLI 响应与运行时记录表达一致。
- [X] T030 [US4] 调整 `src/application/use_cases/query_wash_session_status.c`、`src/adapters/logging/file_event_logger.c`、`include/domain/ports/event_logger_port.h`，让 `status` 状态视图复用统一投影中的共享语义字段与格式约束，但不新增最近结果写入，也不新增事件日志副作用。验证：`status` 返回的状态视图与统一语义契约一致，且查询仍保持只读无副作用。

**检查点**：到此为止，运行时结果表达已经统一，正式命令的外部可见语义保持稳定。

---

## Phase 7：收尾与跨故事事项

**目的**：做最终回归、文档对齐和残留清理，确保本轮只交付“基线加固”而非额外语义变更。

- [X] T031 [P] 运行并核对 `specs/005-harden-ddd-baseline/quickstart.md` 中定义的构建与重点回归流程，必要时仅更新 `specs/005-harden-ddd-baseline/quickstart.md` 的验证描述。验证：quickstart 与最终实现、测试证据一致。
- [X] T032 清理 `src/application/use_cases/process_wash_trigger.c`、`src/domain/services/wash_session_state_machine.c`、`src/domain/services/wash_execution_service.c`、`src/domain/services/wait_timeout_service.c`、`src/adapters/inbound/cli_command_adapter.c` 中因本次收敛遗留的重复 helper、无效 include 和旁路语义。验证：仓库内不存在与本特性目标冲突的重复职责代码。
- [X] T033 执行 `tests/contract/test_controller_stdin_protocol.c`、`tests/contract/test_controller_status_contract.c`、`tests/integration/test_controller_command_paths.c`、`tests/integration/test_session_status_observability.c`、`tests/simulation/test_controller_global_fault_behavior.c`、`tests/unit/test_wait_timeout_service.c` 与 `tests/unit/test_wash_execution_service.c` 的最终回归，并在 `specs/005-harden-ddd-baseline/quickstart.md` 对应步骤记录验收结论。验证：现有正式命令路径和典型运行流程回归通过，外部行为稳定。

---

## 依赖与执行顺序

### 阶段依赖

- Phase 1 无依赖，可立即开始。
- Phase 2 依赖 Phase 1 完成，并阻塞所有用户故事。
- Phase 3（US1）依赖 Phase 2 完成，是后续所有故事的领域契约基线。
- Phase 4（US2）依赖 US1 完成，因为统一应用层编排必须建立在领域事实化返回之上。
- Phase 5（US3）依赖 US1、US2 完成，因为状态写入边界需要建立在新领域契约和统一编排入口之上。
- Phase 6（US4）依赖 US2、US3 完成，因为统一对外语义必须建立在写入边界与投影收口之后。
- Phase 7 依赖所有目标用户故事完成。

### 用户故事依赖

- **US1**：基础阶段完成后即可开始，不依赖其他故事。
- **US2**：依赖 US1 的领域事实与窄化接口基线。
- **US3**：依赖 US1、US2 的结构收敛结果。
- **US4**：依赖 US2、US3 的统一编排和清晰写入边界。

### 每个用户故事内部顺序

- 先补验证任务，再做实现任务。
- 先改领域返回事实，再收敛应用编排入口。
- 先固定写入边界，再统一对外语义投影。
- 每个故事完成后先按其独立验证方式回归，再进入下一个故事。

---

## 并行机会

- Phase 1 中的 T002、T003 可并行。
- Phase 3 中的 T008、T009、T010 可并行。
- Phase 4 中的 T014、T015 可并行。
- Phase 5 中的 T020、T021 可并行。
- Phase 6 中的 T026、T027 可并行。
- Phase 7 中的 T031 可在主要代码完成后与残留清理准备并行推进。

## 并行示例：用户故事 1

```text
Task: "更新 tests/unit/test_wash_session_state_machine.c，断言会话状态机只产出会话迁移事实与原因信息"
Task: "新增 tests/unit/test_wash_execution_service.c，覆盖执行推进、等待条件更新和停止/故障处理返回的领域事实"
Task: "更新 tests/unit/test_wait_timeout_service.c，锁定 wait_timeout_service 只返回超时决议事实"
```

## 并行示例：用户故事 2

```text
Task: "更新 tests/contract/test_controller_stdin_protocol.c 和 tests/contract/test_controller_status_contract.c，断言六类正式命令都经由统一正式入口"
Task: "更新 tests/integration/test_controller_command_paths.c 和 tests/integration/test_controller_runtime_persistence.c，覆盖写命令正式编排路径与 status 只读分支"
```

## 并行示例：用户故事 3

```text
Task: "更新 tests/integration/test_session_status_observability.c 和 tests/simulation/test_controller_global_fault_behavior.c，断言 global_fault_* 与 last_result_* 只能由统一主触发编排入口改写"
Task: "更新 tests/integration/test_event_driven_session_progression.c 和 tests/unit/test_wash_session_state_machine.c，锁定 session/execution/wait 三类状态的唯一写入责任方"
```

## 并行示例：用户故事 4

```text
Task: "更新 tests/contract/test_execution_events_contract.c、tests/contract/test_controller_status_contract.c、tests/integration/test_controller_command_paths.c，校验典型事件表达一致"
Task: "更新 tests/integration/test_session_exception_paths.c 和 tests/simulation/test_controller_global_fault_behavior.c，覆盖 fault-clear、idle stop、duplicate feedback、timeout 的语义一致性回归"
```

---

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1：准备阶段。
2. 完成 Phase 2：基础阶段。
3. 完成 Phase 3：用户故事 1。
4. 先独立回归领域层去反向依赖和事实化返回，再决定是否进入后续故事。

### 增量交付

1. 先交付 US1，纠正领域层依赖方向。
2. 再交付 US2，收敛统一主触发编排入口。
3. 再交付 US3，固定五类核心状态的写入边界。
4. 最后交付 US4，统一对外运行时语义并做回归收口。

### 多人并行策略

1. 团队先共同完成 Phase 1 和 Phase 2。
2. US1 完成后：
   开发者 A：推进 US2 的统一入口与兼容包装收敛。
   开发者 B：推进 US3 的状态写入边界固化。
3. US2、US3 落定后，再集中推进 US4 的统一投影与语义回归。

---

## 备注

- 所有任务默认以“不改变正式命令对外语义”为硬约束。
- `status` 必须进入统一正式入口，但始终保持只读、不可入队、不可刷新最近结果。
- `global_fault_*` 与 `last_result_*` 只允许统一主触发编排入口写入。
- 如需新增辅助文件，限定在 `application/coordinators` 及对应头文件目录内，不做目录级大搬迁。