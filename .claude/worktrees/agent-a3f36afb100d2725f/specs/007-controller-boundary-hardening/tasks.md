# 任务：主控骨架边界收紧

**输入**：来自 `/specs/007-controller-boundary-hardening/` 的设计文档  
**前置条件**：`plan.md`（必需）、`spec.md`（必需，用于用户故事）、`research.md`、`data-model.md`、`contracts/`

**验证**：每个故事都必须先定义边界型测试或人工检查方式，再进入实现；所有验证以“写入口是否收口、依赖是否收缩、最终落点是否唯一”为核心。

**组织方式**：任务按用户故事分组，以保证每个故事都能独立实现和独立验证。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（如 `US1`、`US2`、`US3`、`US4`）
- 描述中必须包含准确文件路径
- 每个实现任务或验证任务都附带 `验证：`

## 路径约定

- 单项目结构：`src/`、`include/`、`tests/`
- 测试注册入口：`tests/CMakeLists.txt`
- 核心源码注册入口：`src/CMakeLists.txt`

## Phase 1：准备阶段（共享基础）

**目的**：先把构建与公开边界注释准备好，避免后续每个故事各自修改同一类元数据。

- [X] T001 在 `src/CMakeLists.txt` 与 `tests/CMakeLists.txt` 对齐本特性将新增、删除和重命名的源码/测试入口。验证：`cmake -S . -B build` 在每个阶段结束后都不会因为源文件或测试目标缺失而失败。
- [X] T002 [P] 在 `include/application/coordinators/system_context.h`、`include/application/use_cases/process_formal_command.h`、`include/adapters/inbound/cli_command_adapter.h`、`include/platform/linux/main_loop.h` 刷新中文 Doxygen 注释，使其先反映“组合根/正式命令入口/协议适配层/主循环拥有者”边界。验证：头文件注释与 `spec.md`、`plan.md` 的边界定义一致且没有“第二套主路径”残留表述。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：先收拢所有故事共享的结果投影与运行对象拥有权基线。

**关键约束**：在本阶段完成前，不得开始任何用户故事开发。

- [X] T003 在 `include/application/coordinators/runtime_event_recorder.h`、`src/application/coordinators/runtime_event_recorder.c`、`include/application/coordinators/runtime_result_projection.h`、`src/application/coordinators/runtime_result_projection.c` 固化“最近结果只做投影、不是会话最终落点”的共享辅助接口。验证：共享记录/投影接口能表达最近结果与最终会话结果分离，且调用方不再需要重复手写最终结果同步逻辑。
- [X] T004 [P] 在 `include/domain/model/wash_session.h`、`include/domain/model/wash_execution.h`、`include/domain/model/wait_condition.h`、`include/domain/model/program_snapshot.h`、`include/application/coordinators/system_context.h` 统一补齐运行对象拥有者与最终解释权说明。验证：这些公共头文件对“谁负责写、谁只负责读、谁拥有最终解释权”的定义不互相冲突。

**检查点**：共享投影语义与运行对象拥有者定义完成，用户故事可以按边界推进。

---

## Phase 3：用户故事 1 - 总上下文只承载共享运行事实（优先级：P1）MVP

**目标**：把 `system_context_t` 收口为应用层组合根，避免临时状态和局部决策继续回流成共享状态。

**独立验证方式**：检查 `system_context_t` 中只保留共享运行事实；领域服务和查询流程不再把临时状态塞回总上下文。

### 用户故事 1 的验证任务

- [X] T005 [P] [US1] 在 `tests/contract/test_system_context_boundary_contract.c` 编写组合根边界契约测试，并在 `tests/CMakeLists.txt` 注册 `test_contract_system_context_boundary`。验证：测试先失败，直到 `system_context_t` 只保留共享运行事实且共享字段拥有者清晰。
- [X] T006 [P] [US1] 在 `tests/unit/test_service_arg_slices.c` 编写领域服务依赖切片单元测试，并在 `tests/CMakeLists.txt` 注册 `test_unit_service_arg_slices`。验证：测试先失败，直到 `wash_session_service_args_t`、`wash_execution_service_args_t`、`program_snapshot_service_args_t` 不再携带无关共享状态。

### 用户故事 1 的实现任务

- [X] T007 [US1] 在 `include/application/coordinators/system_context.h` 与 `src/application/use_cases/process_wash_trigger.c` 收紧 `system_context_t` 的共享字段边界，移除或下沉仅供单次处理使用的临时状态。验证：`test_contract_system_context_boundary` 通过，且 `system_context_t` 中不存在无拥有者说明的临时字段。
- [X] T008 [US1] 在 `include/application/use_cases/query_wash_session_status.h` 与 `src/application/use_cases/query_wash_session_status.c` 保持状态视图为纯投影对象，不再为查询引入新的上下文字段或缓存写回。验证：`test_contract_system_context_boundary` 通过，查询路径不会向 `system_context_t` 添加新的查询专用共享状态。
- [X] T009 [US1] 在 `include/domain/services/wash_session_state_machine.h`、`include/domain/services/wash_execution_service.h`、`include/domain/services/program_snapshot_service.h` 以及 `src/application/use_cases/process_wash_trigger.c` 清理对总上下文的隐式耦合，确保调用方只传最小依赖切片。验证：`test_unit_service_arg_slices` 通过，领域服务接口中不新增 `system_context_t *` 参数。

**检查点**：到此为止，`system_context_t` 已成为组合根，且不再被当作领域层万能依赖。

---

## Phase 4：用户故事 2 - 关键运行状态只允许从正式入口改写（优先级：P1）

**目标**：把全局故障、最近结果、触发队列、当前时间和正式命令受理边界收口到固定入口，并移除兼容性第二主路径。

**独立验证方式**：沿 CLI 正式命令、主循环和故障路径检查时，能明确定位每类状态的唯一写入口；兼容性入口不再作为正式主路径存在。

### 用户故事 2 的验证任务

- [X] T010 [P] [US2] 在 `tests/contract/test_trigger_runtime_ownership_contract.c` 编写运行时写入口契约测试，并在 `tests/CMakeLists.txt` 注册 `test_contract_trigger_runtime_ownership`。验证：测试先失败，直到 `global_fault_*`、最近结果、触发队列和时间推进只由约定拥有者改写。
- [X] T011 [P] [US2] 在 `tests/integration/test_formal_entry_ownership.c` 编写正式命令/适配层所有权集成测试，并在 `tests/CMakeLists.txt` 注册 `test_integration_formal_entry_ownership`。验证：测试先失败，直到 CLI 路径只通过 `process_formal_command` + `main_loop_submit_trigger()` 驱动，适配层不再直碰队列。
- [X] T012 [P] [US2] 在 `tests/integration/test_compat_entrypoint_removal.c` 编写兼容性入口退场集成测试，并在 `tests/CMakeLists.txt` 注册 `test_integration_compat_entrypoint_removal`。验证：测试先失败，直到 `start_wash_cycle`、`stop_wash_cycle`、`acknowledge_fault` 不再作为并行正式入口保留。

### 用户故事 2 的实现任务

- [X] T013 [US2] 在 `src/application/use_cases/process_wash_trigger.c` 与 `src/application/coordinators/runtime_event_recorder.c` 固化 `global_fault_*`、`last_result_code`、`last_reason_code`、`last_transition_record` 的正式写入口。验证：`test_contract_trigger_runtime_ownership` 通过，查询/适配/领域服务不再直接改写这些状态。
- [X] T014 [US2] 在 `src/application/use_cases/process_formal_command.c`、`include/application/use_cases/process_formal_command.h`、`src/adapters/inbound/cli_command_adapter.c`、`include/adapters/inbound/cli_command_adapter.h` 收紧“CLI 正式命令入口 + 协议适配层”边界，只允许适配层提交事实或请求。验证：`test_integration_formal_entry_ownership` 通过，适配层代码不再直接读取或写入 `pending_triggers` / `pending_trigger_count`。
- [X] T015 [US2] 在 `src/platform/linux/main_loop.c` 与 `include/platform/linux/main_loop.h` 固化触发队列与时间推进的主循环拥有权，仅暴露正式提交接口。验证：`test_contract_trigger_runtime_ownership` 通过，主循环外部路径只能通过提交函数驱动处理。
- [X] T016 [US2] 在 `src/application/use_cases/start_wash_cycle.c`、`src/application/use_cases/stop_wash_cycle.c`、`src/application/use_cases/acknowledge_fault.c`、对应头文件、`src/application/coordinators/compatibility_trigger_runner.c`、`include/application/coordinators/compatibility_trigger_runner.h` 以及 `src/CMakeLists.txt` 删除兼容性第二主路径。验证：`test_integration_compat_entrypoint_removal` 通过，构建中不再保留并行正式入口实现。

**检查点**：到此为止，关键运行状态已收口到正式入口，CLI 是唯一正式命令链路，兼容性入口已退场。

---

## Phase 5：用户故事 3 - 业务服务与主控总容器解耦（优先级：P1）

**目标**：让领域服务和用例服务只依赖最小输入，且把会话最终结果和执行结果的解释边界彻底分开。

**独立验证方式**：检查领域服务头文件和主用例编排时，不再出现直接传递 `system_context_t` 给领域层；会话结束后只有 `wash_session.final_session_result` 拥有最终解释权。

### 用户故事 3 的验证任务

- [X] T017 [P] [US3] 在 `tests/contract/test_session_finalization_contract.c` 编写会话最终落点契约测试，并在 `tests/CMakeLists.txt` 注册 `test_contract_session_finalization`。验证：测试先失败，直到 `wash_session.final_session_result` 成为唯一最终会话结论落点。
- [X] T018 [P] [US3] 在 `tests/unit/test_runtime_object_ownership.c` 编写运行对象拥有权单元测试，并在 `tests/CMakeLists.txt` 注册 `test_unit_runtime_object_ownership`。验证：测试先失败，直到会话、执行、等待、程序快照和状态视图的写读边界不重叠。
- [X] T019 [P] [US3] 在 `tests/integration/test_unique_session_result_sink.c` 编写唯一最终结果落点集成测试，并在 `tests/CMakeLists.txt` 注册 `test_integration_unique_session_result_sink`。验证：测试先失败，直到正常/异常结束都只留下单一会话最终落点和一致投影。

### 用户故事 3 的实现任务

- [X] T020 [US3] 在 `include/domain/model/wash_session.h`、`src/domain/model/wash_session.c`、`include/domain/model/wash_execution.h`、`src/domain/model/wash_execution.c` 明确区分会话最终结果与当前执行结果的拥有者和写入时机。验证：`test_contract_session_finalization` 与 `test_integration_unique_session_result_sink` 通过。
- [X] T021 [US3] 在 `include/domain/model/wait_condition.h`、`src/domain/model/wait_condition.c`、`include/domain/model/program_snapshot.h`、`src/domain/model/program_snapshot.c` 强化等待状态和程序快照的只读/只写边界。验证：`test_unit_runtime_object_ownership` 通过，非拥有者路径不再直接改写等待和快照对象。
- [X] T022 [US3] 在 `include/domain/services/wash_execution_service.h`、`src/domain/services/wash_execution_service.c`、`include/domain/services/wash_session_state_machine.h`、`src/domain/services/wash_session_state_machine.c`、`include/domain/services/program_snapshot_service.h`、`src/domain/services/program_snapshot_service.c`、`include/domain/services/wait_timeout_service.h`、`src/domain/services/wait_timeout_service.c` 收缩服务依赖为最小切片。验证：`test_unit_service_arg_slices` 与 `test_unit_runtime_object_ownership` 通过，领域服务不再需要组合根才能工作。
- [X] T023 [US3] 在 `src/application/use_cases/process_wash_trigger.c` 与 `src/application/use_cases/query_wash_session_status.c` 按拥有者重新落地最终结果投影和状态视图构造。验证：`test_contract_session_finalization`、`test_integration_unique_session_result_sink` 与 `test_unit_runtime_object_ownership` 同时通过。

**检查点**：到此为止，领域服务依赖已收缩，会话最终结果与运行中结果的解释边界已清晰。

---

## Phase 6：用户故事 4 - 运行期边界由回归验证持续守住（优先级：P2）

**目标**：补齐边界型回归测试，确保只读查询、故障入口、正式命令入口和最终状态落点不会再次回退。

**独立验证方式**：执行契约、集成与仿真回归时，可以直接证明查询无副作用、故障只能从正式入口进入、兼容入口已退场且会话终态唯一。

### 用户故事 4 的验证任务

- [X] T024 [P] [US4] 在 `tests/contract/test_status_query_readonly_contract.c` 编写只读查询契约测试，并在 `tests/CMakeLists.txt` 注册 `test_contract_status_query_readonly`。验证：测试先失败，直到多次 `status` 查询都不会改写最近结果、故障状态、触发队列或会话终态。
- [X] T025 [P] [US4] 在 `tests/integration/test_status_query_no_side_effect.c` 编写 `status` 无副作用集成测试，并在 `tests/CMakeLists.txt` 注册 `test_integration_status_query_no_side_effect`。验证：测试先失败，直到 CLI `status` 路径保持只读，同时正式命令路径仍正常工作。
- [X] T026 [P] [US4] 在 `tests/simulation/test_controller_global_fault_behavior.c` 与 `tests/integration/test_session_status_observability.c` 扩展边界回归场景，并在 `tests/CMakeLists.txt` 保持对应目标可运行。验证：新增仿真/集成场景先失败，直到故障入口、只读查询和最终状态落点全部按规格收口。

### 用户故事 4 的实现任务

- [X] T027 [US4] 在 `src/application/use_cases/query_wash_session_status.c`、`src/application/use_cases/process_formal_command.c`、`src/main/main.c` 修正任何仍会让 `status`、响应组装或主循环输出副作用写回关键状态的路径。验证：`test_contract_status_query_readonly` 与 `test_integration_status_query_no_side_effect` 通过。
- [X] T028 [US4] 在 `specs/007-controller-boundary-hardening/quickstart.md` 更新最终回归矩阵与命令示例，并同步 `tests/CMakeLists.txt` 中的最终测试命名。验证：`quickstart.md` 中列出的 `ctest -R` 过滤表达式都能命中实际测试目标。

**检查点**：到此为止，边界型回归验证可长期守住这轮结构约束。

---

## Phase 7：收尾与跨故事事项

**目的**：完成跨故事清理、文档同步和最终全量验证。

- [X] T029 [P] 在 `include/application/coordinators/system_context.h`、`include/application/use_cases/process_formal_command.h`、`include/adapters/inbound/cli_command_adapter.h`、`include/platform/linux/main_loop.h` 复查中文 Doxygen、头文件保护和边界注释一致性。验证：所有对外公开边界头文件都保留中文 Doxygen，且不再出现兼容性第二主路径的陈旧表述。
- [X] T030 在 `specs/007-controller-boundary-hardening/contracts/system-context-boundary-contract.md`、`specs/007-controller-boundary-hardening/contracts/trigger-runtime-orchestration-contract.md`、`specs/007-controller-boundary-hardening/contracts/status-and-session-finalization-contract.md` 回写实现后的最终边界与测试名称。验证：契约文档只引用仍然存在的正式入口、测试名和运行对象边界。
- [X] T031 依据 `specs/007-controller-boundary-hardening/quickstart.md` 执行完整 `ctest --test-dir build --output-on-failure` 回归并记录问题修正。验证：完整测试集通过，且 `spec.md` 中的 SC-001 至 SC-008 都有对应验证证据。

---

## 依赖与执行顺序

### 阶段依赖

- **准备阶段（Phase 1）**：无依赖，可立即开始
- **基础阶段（Phase 2）**：依赖准备阶段完成，并阻塞所有用户故事
- **用户故事阶段（Phase 3+）**：全部依赖基础阶段完成
- **收尾阶段（Phase 7）**：依赖所有目标用户故事完成

### 用户故事依赖

- **用户故事 1（P1）**：基础阶段完成后即可开始，是 MVP 的第一交付目标
- **用户故事 2（P1）**：依赖 US1 完成后的组合根边界基线，因为正式写入口要建立在组合根不再混放临时状态之上
- **用户故事 3（P1）**：依赖 US1 和 US2 完成，因为最小依赖切片与唯一最终落点要建立在正式入口和写入口已收口之后
- **用户故事 4（P2）**：依赖 US1、US2、US3 完成，因为它负责把前面三类边界约束固化成回归测试

### 每个用户故事内部顺序

- 先定义验证，再开始实现
- 先改头文件/契约，再改实现
- 先收口写入口，再删兼容路径
- 先完成故事独立验证，再进入下一个故事
- 当前故事完成后必须同步 `tests/CMakeLists.txt`
- 涉及公开接口的故事，在完成前必须补齐中文 Doxygen 检查

### 可并行机会

- `T002` 与 `T001` 可并行
- `T003` 与 `T004` 可并行
- 每个故事中的 `[P]` 测试任务可并行
- `US2` 中 `T010`、`T011`、`T012` 可并行准备
- `US3` 中 `T017`、`T018`、`T019` 可并行准备
- `US4` 中 `T024`、`T025`、`T026` 可并行准备

---

## 并行示例：用户故事 1

```bash
# 并行准备 US1 的边界测试
Task: "在 tests/contract/test_system_context_boundary_contract.c 编写组合根边界契约测试"
Task: "在 tests/unit/test_service_arg_slices.c 编写领域服务依赖切片单元测试"
```

## 并行示例：用户故事 2

```bash
# 并行准备 US2 的正式入口与兼容入口退场验证
Task: "在 tests/contract/test_trigger_runtime_ownership_contract.c 编写运行时写入口契约测试"
Task: "在 tests/integration/test_formal_entry_ownership.c 编写正式命令/适配层所有权集成测试"
Task: "在 tests/integration/test_compat_entrypoint_removal.c 编写兼容性入口退场集成测试"
```

## 并行示例：用户故事 3

```bash
# 并行准备 US3 的拥有者与唯一结果落点测试
Task: "在 tests/contract/test_session_finalization_contract.c 编写会话最终落点契约测试"
Task: "在 tests/unit/test_runtime_object_ownership.c 编写运行对象拥有权单元测试"
Task: "在 tests/integration/test_unique_session_result_sink.c 编写唯一最终结果落点集成测试"
```

## 并行示例：用户故事 4

```bash
# 并行准备 US4 的边界回归测试
Task: "在 tests/contract/test_status_query_readonly_contract.c 编写只读查询契约测试"
Task: "在 tests/integration/test_status_query_no_side_effect.c 编写 status 无副作用集成测试"
Task: "扩展 tests/simulation/test_controller_global_fault_behavior.c 与 tests/integration/test_session_status_observability.c 的边界回归场景"
```

---

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1：准备阶段
2. 完成 Phase 2：基础阶段
3. 完成 Phase 3：用户故事 1
4. 运行 `test_contract_system_context_boundary` 与 `test_unit_service_arg_slices`
5. 若通过，则主控组合根边界可先独立验收

### 增量交付

1. 先完成组合根边界（US1）
2. 再完成正式写入口和正式命令入口收口（US2）
3. 再完成领域服务解耦与唯一最终落点（US3）
4. 最后用边界型回归把结构约束锁死（US4）

### 多人并行策略

1. 团队先共同完成 Phase 1 和 Phase 2
2. 基础阶段完成后：
   - 开发者 A：US1 组合根边界
   - 开发者 B：US2 正式入口与兼容入口退场
   - 开发者 C：US3 运行对象拥有权与唯一最终落点
3. US4 由前面故事合并后统一补边界回归

---

## 备注

- `[P]` 表示不同文件、无依赖冲突，可并行
- `[US1]` 到 `[US4]` 直接映射 `spec.md` 中的四个用户故事
- 本特性明确要求边界型回归验证，因此测试任务是必需项，不是可选项
- 兼容性入口删除属于本特性的正式范围，不得在实现时跳过
- 每完成一个故事后都应运行对应的最小 `ctest -R` 过滤集合
