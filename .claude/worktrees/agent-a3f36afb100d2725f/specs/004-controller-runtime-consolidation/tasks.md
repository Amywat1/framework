# 任务：控制器运行时收敛

**输入**：来自 `specs/004-controller-runtime-consolidation/` 的设计文档  
**前置条件**：`plan.md`、`spec.md`、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证**：每个用户故事都必须先补齐回归验证，再实施代码收敛；所有验证都以“保持六类命令既有响应语义不变”为前提。

**组织方式**：任务按用户故事分组，保证每个故事都能独立实现、独立验证和独立回归。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（`US1`、`US2`、`US3`、`US4`）
- 每个任务都包含准确文件路径
- 每个任务都附带 `验证：` 说明

## Phase 1：准备阶段（共享基础）

**目的**：先把测试入口和公开契约对齐，为后续收敛提供共同基线。

- [X] T001 盘点并更新 `tests/CMakeLists.txt`，确保本特性新增或拆分后的 contract / integration / simulation 回归测试都会被 `ctest` 发现。验证：相关测试目标可从 `ctest -N` 或等效列表中看到。
- [X] T002 [P] 扩展 `tests/test_support.h`，提供基于 `cli_command_adapter_prepare_line()`、`main_loop_submit_trigger()`、`main_loop_run()`、`cli_command_adapter_finalize_trigger_response()` 的正式路径驱动 helper。验证：测试 helper 能在不调用业务核心直通接口的前提下完成命令驱动。
- [X] T003 [P] 校准 `include/adapters/inbound/cli_command_adapter.h`、`include/platform/linux/main_loop.h`、`include/application/use_cases/query_wash_session_status.h` 的中文 Doxygen 注释，明确正式命令路径与内部只读查询边界。验证：头文件不再把直调写路径描述为正式入口。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：抽出所有故事共享的收敛支点，避免每个故事各自补一套入口逻辑。

**关键约束**：完成本阶段前，不开始任何用户故事实现任务。

- [X] T004 重构 `src/adapters/inbound/cli_command_adapter.c`，把解析、协议错误记录、`status` 响应生成与触发响应收口为可复用辅助逻辑，为移除直调业务路径做准备。验证：适配器内部能独立支撑“解析/响应”而不要求直接执行业务 use case。
- [X] T005 [P] 调整 `src/platform/linux/main_loop.c` 和 `include/platform/linux/main_loop.h`，暴露正式路径所需的最小入队、消费和排空能力，但不改变现有优先级与队列语义。验证：测试可观测 pending trigger 生命周期，且主循环行为不变。
- [X] T006 调整 `src/main/main.c` 与 `tests/test_support.h`，让生产代码和测试代码共用同一套正式命令驱动模式，而不是并存两套命令推进方式。验证：测试支撑不再依赖 `cli_command_adapter_process_line()` 或薄 use case 作为主链路定义。

**检查点**：到此为止，正式路径驱动能力已齐备，用户故事可以开始按优先级推进。

---

## Phase 3：用户故事 1 - 统一正式命令主链路（优先级：P1）MVP

**目标**：让 `start / stop / feedback / fault / fault clear / status` 六类正式命令都走同一条生产链路。

**独立验证方式**：对六类命令执行 contract + integration 回归，确认写命令经由 `命令解析 -> trigger 入队 -> main_loop_run -> 响应生成`，`status` 经由 `命令解析 -> 只读查询 -> 响应生成`，且两者都经过统一正式受理入口并保持既有语义不变。

### 用户故事 1 的验证任务

- [X] T007 [P] [US1] 更新 `tests/contract/test_controller_stdin_protocol.c`，断言六类正式命令都通过正式受理与统一响应逻辑返回结果。验证：移除任何正式 stdin 旁路后，协议测试仍全部通过。
- [X] T008 [P] [US1] 更新 `tests/integration/test_controller_command_paths.c` 与 `tests/contract/test_controller_status_contract.c`，用正式路径 helper 覆盖 `start / stop / feedback / fault / fault clear / status` 主链路。验证：写命令必须入队消费，`status` 不得绕过正式受理入口但可不入队，且六类命令的既有结果语义保持不变。

### 用户故事 1 的实现任务

- [X] T009 [US1] 改写 `src/adapters/inbound/cli_command_adapter.c` 与 `include/adapters/inbound/cli_command_adapter.h` 中的 `cli_command_adapter_process_line()`，将其降级为兼容包装或删除其直调业务分支，不再形成第二套正式执行链。验证：适配器中不存在按 trigger type 直调 `start/stop/fault` 业务核心的主逻辑。
- [X] T010 [US1] 收敛 `src/main/main.c` 的 stdin 处理细节，确保生产入口只保留统一正式受理骨架和 EOF/drain 协调：写命令走 `prepare -> submit -> main_loop_run -> finalize`，`status` 走 `prepare -> 只读查询 -> finalize`。验证：`main.c` 中不存在第二套命令执行分支，且运行语义与当前保持一致。

**检查点**：到此为止，正式 stdin 已只有一条可识别的生产执行链。

---

## Phase 4：用户故事 2 - 收敛写入口并减少跳转（优先级：P1）

**目标**：减少纯转发写入口，让 `process_wash_trigger` 继续保持唯一业务分发核心，同时保留独立查询入口。

**独立验证方式**：沿 `start / stop / fault` 三条链路阅读实现和回归测试，确认写路径入口明显减少，`status` 仍通过独立只读查询获得状态视图。

### 用户故事 2 的验证任务

- [X] T011 [P] [US2] 更新 `tests/contract/test_session_command_contract.c` 与 `tests/integration/test_session_exception_paths.c`，使验证逻辑不再依赖薄写 use case 作为行为主入口。验证：测试仍能覆盖 `start / stop / fault` 关键语义，但驱动方式改为正式路径或显式 trigger 驱动。
- [X] T012 [P] [US2] 更新 `tests/integration/test_session_status_observability.c`，确认 `query_wash_session_status_execute()` 仍是只读查询入口而不是写路径旁路。验证：状态读取能力保留，且不承担命令写入职责。

### 用户故事 2 的实现任务

- [X] T013 [US2] 收敛 `src/application/use_cases/start_wash_cycle.c`、`src/application/use_cases/stop_wash_cycle.c`、`src/application/use_cases/acknowledge_fault.c` 及其对应头文件 `include/application/use_cases/start_wash_cycle.h`、`include/application/use_cases/stop_wash_cycle.h`、`include/application/use_cases/acknowledge_fault.h`，将其降级为兼容性辅助包装，不再作为生产代码正式主路径入口。验证：生产主链路不再通过这些薄文件进入业务分发核心。
- [X] T014 [US2] 调整 `tests/test_support.h`，并按代码搜索结果批量更新所有仍通过薄 use case 或 `cli_command_adapter_process_line()` 驱动业务主链的测试文件，替换为正式路径驱动或显式 trigger 驱动；重点覆盖 `test_start_session`、`test_submit_stop`、`test_submit_fault`、`test_process_command` 等 helper 的现有调用点。验证：测试 helper 只驱动或观察正式路径，不直接承担第二套业务链，且仓库内不再存在未审查的旧驱动调用点。
- [X] T015 [US2] 保持 `src/application/use_cases/query_wash_session_status.c` 与 `include/application/use_cases/query_wash_session_status.h` 的独立只读语义，并清理与写路径无关的职责边界描述。验证：查询入口仍可单独复用，但不会替代正式 stdin 的 `status` 处理。

**检查点**：到此为止，写操作入口数量已减少，`process_wash_trigger_execute()` 仍是唯一业务分发核心。

---

## Phase 5：用户故事 3 - 统一结果、状态与响应记录（优先级：P1）

**目标**：把最近结果、状态摘要、日志和 CLI 响应的记录方式收敛到少数一致的语义来源。

**独立验证方式**：覆盖 accepted / rejected / ignored / transition / fault-clear 相关既有场景，确认 `last_result_code`、`last_reason_code`、`status`、日志和 CLI 响应一致。

### 用户故事 3 的验证任务

- [X] T016 [P] [US3] 更新 `tests/contract/test_execution_events_contract.c` 与 `tests/contract/test_controller_status_contract.c`，断言同一事件的最近结果、最近原因和状态摘要保持一致。验证：契约测试能捕获结果码、原因码与状态视图不一致的回归。
- [X] T017 [P] [US3] 更新 `tests/integration/test_session_exception_paths.c` 与 `tests/simulation/test_controller_global_fault_behavior.c`，覆盖 ignored / rejected / cleared 等既有场景的记录一致性。验证：路径收敛后，现有外部语义不变且记录口径统一。

### 用户故事 3 的实现任务

- [X] T018 [US3] 在 `src/application/use_cases/process_wash_trigger.c` 中抽出统一的结果记录辅助逻辑，收口 `last_result_code`、`last_reason_code`、`last_transition_record` 和 rejection / ignored / transition 日志写法。验证：典型路径不再分散手写最近结果与日志。
- [X] T019 [US3] 重构 `src/adapters/inbound/cli_command_adapter.c`，复用统一记录结果来生成协议错误、命令拒绝和最终 CLI 响应，而不是重复维护一套语义映射。验证：CLI 响应与运行时记录共享同一语义来源。
- [X] T020 [US3] 调整 `src/application/use_cases/query_wash_session_status.c`、`src/domain/services/wash_session_state_machine.c`、`src/domain/services/wash_execution_service.c`，使状态视图、迁移记录和最近结果字段在同一事件上保持对齐。验证：`status`、日志和最近结果对同一事件不再给出冲突结论。

**检查点**：到此为止，结果记录辅助逻辑已经统一，状态摘要、CLI 响应和日志语义一致。

---

## Phase 6：用户故事 4 - 明确状态写入责任并轻量化主入口（优先级：P2）

**目标**：固定 `session / execution / wait_condition / global_fault` 的写入边界，并把 `main.c` 收敛为主循环骨架。

**独立验证方式**：检查典型状态修改点与主循环回归，确认 `wash_session_state_machine`、`wash_execution_service`、`wait_timeout_service`、`process_wash_trigger` 各司其职，`main.c` 不再承载命令处理细节。

### 用户故事 4 的验证任务

- [X] T021 [P] [US4] 更新 `tests/unit/test_wait_timeout_service.c` 与 `tests/integration/test_session_status_observability.c`，锁定 `wait_condition`、`execution` 与 `global_fault` 的责任边界。验证：错误层级写状态时会触发回归失败。
- [X] T022 [P] [US4] 更新 `tests/integration/test_controller_runtime_persistence.c` 与 `tests/simulation/test_controller_continuous_runtime.c`，确认主入口变薄后仍保持 `select + timeout + stdin` 运行模型和排空行为。验证：运行时骨架未发生模型回归。

### 用户故事 4 的实现任务

- [X] T023 [US4] 调整 `src/application/use_cases/process_wash_trigger.c`，将其职责限制为路由、高层策略和 `global_fault` set/clear，并把 session/execution/wait 的内部推进交回 `src/domain/services/wash_session_state_machine.c`、`src/domain/services/wash_execution_service.c`、`src/domain/services/wait_timeout_service.c`。验证：状态写入责任与 `state-write-boundaries.md` 一致。
- [X] T024 [US4] 重构 `src/main/main.c` 与 `src/platform/linux/main_loop.c`，把 stdin 等待、时间推进、runtime drain 等细节下沉为辅助函数或运行时协作点，保留主入口和主循环骨架。验证：`main.c` 只剩初始化、等待、推进与协调职责。
- [X] T025 [US4] 更新 `include/application/coordinators/system_context.h`、`include/platform/linux/main_loop.h`、`include/adapters/inbound/cli_command_adapter.h` 的注释与约束描述，固化状态写入责任边界和主入口职责。验证：公开接口文档与实际责任边界一致。

**检查点**：到此为止，状态写入责任已固定，`main.c` 只保留主循环骨架。

---

## Phase 7：收尾与跨故事事项

**目的**：做最终回归、文档对齐和收口，确保本轮只交付“收敛”而非额外语义变更。

- [X] T026 [P] 运行并核对 `specs/004-controller-runtime-consolidation/quickstart.md` 对应的人工/自动化验证流程，必要时仅更新 `specs/004-controller-runtime-consolidation/quickstart.md` 中与实现证据不一致的检查描述。验证：quickstart 与实际实现、测试证据一致。
- [X] T027 收口本次触及的测试与代码文件，包括 `tests/CMakeLists.txt`、`tests/test_support.h`、`src/main/main.c`、`src/adapters/inbound/cli_command_adapter.c`、`src/application/use_cases/process_wash_trigger.c`，删除仅因收敛遗留的旁路 helper 或重复语义代码。验证：不存在第二套主路径、重复记录逻辑或与规格无关的残留改动。

---

## 依赖与执行顺序

### 阶段依赖

- Phase 1 无依赖，可立即开始。
- Phase 2 依赖 Phase 1 完成，并阻塞所有用户故事。
- Phase 3（US1）依赖 Phase 2 完成，是 MVP 与后续故事的主入口基线。
- Phase 4（US2）依赖 US1 完成，因为写入口收缩必须建立在唯一正式命令路径之上。
- Phase 5（US3）依赖 US1、US2 完成，因为统一记录语义必须基于收敛后的正式路径和写入口。
- Phase 6（US4）依赖 US1、US2、US3 完成，因为责任边界和主入口变薄需要建立在已收口的路径与记录模型上。
- Phase 7 依赖所有目标用户故事完成。

### 用户故事依赖

- **US1**：基础阶段完成后即可开始，不依赖其他故事。
- **US2**：依赖 US1 的正式路径基线，但完成后可独立验证“写入口减少且查询入口保留”。
- **US3**：依赖 US1、US2 的结构收敛结果，但完成后可独立验证“记录语义一致”。
- **US4**：依赖前 3 个故事的收敛结果，但完成后可独立验证“责任边界清晰且主入口变薄”。

### 每个用户故事内部顺序

- 先补验证任务，再做实现任务。
- 先收敛正式入口，再收缩薄写入口。
- 先统一记录语义，再固化状态写入责任。
- 每个故事完成后先回归其独立验证标准，再进入下一个故事。

---

## 并行机会

- Phase 1 中的 T002、T003 可并行。
- Phase 2 中的 T005 可在 T004 开始后并行推进，但 T006 需等待正式路径驱动模式稳定。
- US1 中的 T007、T008 可并行。
- US2 中的 T011、T012 可并行。
- US3 中的 T016、T017 可并行。
- US4 中的 T021、T022 可并行。
- 最终收尾中的 T026 可在主要代码收敛完成后，与局部代码清理并行准备。

## 并行示例：用户故事 1

```text
Task: "更新 tests/contract/test_controller_stdin_protocol.c，断言六类正式命令都通过正式受理与统一响应逻辑返回结果"
Task: "更新 tests/integration/test_controller_command_paths.c 与 tests/contract/test_controller_status_contract.c，用正式路径 helper 覆盖六类命令主链路"
```

## 并行示例：用户故事 2

```text
Task: "更新 tests/contract/test_session_command_contract.c 与 tests/integration/test_session_exception_paths.c，去除对薄写 use case 的行为主入口依赖"
Task: "更新 tests/integration/test_session_status_observability.c，确认 query_wash_session_status_execute() 仍是只读查询入口"
```

## 并行示例：用户故事 3

```text
Task: "更新 tests/contract/test_execution_events_contract.c 与 tests/contract/test_controller_status_contract.c，断言最近结果与状态摘要一致"
Task: "更新 tests/integration/test_session_exception_paths.c 与 tests/simulation/test_controller_global_fault_behavior.c，覆盖 ignored / rejected / cleared 记录一致性"
```

## 并行示例：用户故事 4

```text
Task: "更新 tests/unit/test_wait_timeout_service.c 与 tests/integration/test_session_status_observability.c，锁定状态写入责任边界"
Task: "更新 tests/integration/test_controller_runtime_persistence.c 与 tests/simulation/test_controller_continuous_runtime.c，确认主入口变薄后运行模型不变"
```

---

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1：准备阶段。
2. 完成 Phase 2：基础阶段。
3. 完成 Phase 3：用户故事 1。
4. 先独立回归六类正式命令的单路径行为，再决定是否进入后续故事。

### 增量交付

1. 先交付 US1，消除正式路径双链路。
2. 再交付 US2，减少薄写入口和跨文件跳转。
3. 再交付 US3，统一结果记录与响应语义来源。
4. 最后交付 US4，固定责任边界并压薄主入口。

### 多人并行策略

1. 团队先共同完成 Phase 1 和 Phase 2。
2. US1 完成后：
   开发者 A：推进 US2 的薄入口收缩。
   开发者 B：推进 US3 的结果记录统一。
3. US2、US3 落定后，再集中推进 US4 的责任边界与 `main.c` 变薄。

---

## 备注

- 所有任务默认以“不改变六类命令既有响应语义”为硬约束。
- 薄写 use case 本轮按“保留文件但降级为兼容性辅助包装”处理，不作为生产代码正式主路径入口。
- `query_wash_session_status_execute()` 保留为独立只读查询入口，但不能成为正式 stdin 的 `status` 旁路。
