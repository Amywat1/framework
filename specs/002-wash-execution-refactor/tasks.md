# 任务：洗车执行模型重构

**输入**：来自 `specs/002-wash-execution-refactor/` 的设计文档  
**前置条件**：`plan.md`、`spec.md`、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证**：每个用户故事都必须能够通过对应的 contract / integration / simulation / unit 测试或 quickstart 场景独立验证。  
**组织方式**：任务按用户故事分组，以保证每个故事都能独立实现、独立测试、独立交付。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无未完成前置依赖冲突）
- **[Story]**：仅用于用户故事阶段任务，格式为 `[US1]`、`[US2]`、`[US3]`
- 每个任务描述都包含明确文件路径

## Phase 1：准备阶段（共享基础）

**目的**：为本次重构补齐构建入口、测试入口和文档指针，保证后续任务可以按故事增量推进。

- [X] T001 对齐执行模型新增源码与测试目标的构建入口，在 `src/CMakeLists.txt` 和 `tests/CMakeLists.txt`
- [ ] T002 [P] 更新 contract / integration / simulation / unit 测试总入口以纳入新测试文件，在 `tests/contract/test_main.c`、`tests/integration/test_main.c`、`tests/simulation/test_main.c`、`tests/unit/test_main.c`
- [X] T003 [P] 补充执行模型需要的共享结果码、超时常量和公共类型声明，在 `include/shared/result_types.h`、`include/shared/timeouts.h`、`include/domain/model/domain_enums.h`

---

## Phase 2：基础阶段（阻塞前置）

**目的**：建立所有用户故事共享的领域对象、端口扩展和最小日志/快照能力。  
**关键约束**：本阶段完成前，不开始任何用户故事实现。

- [X] T004 在 `include/domain/model/wash_session.h` 和 `src/domain/model/wash_session.c` 创建洗车会话领域对象
- [X] T005 [P] 在 `include/domain/model/wash_execution.h` 和 `src/domain/model/wash_execution.c` 创建单次执行领域对象
- [X] T006 [P] 在 `include/domain/model/wait_condition.h`、`src/domain/model/wait_condition.c`、`include/domain/model/wash_trigger_event.h`、`src/domain/model/wash_trigger_event.c` 创建等待条件与触发事件领域对象，并显式包含最大重试次数、当前重试计数以及“其他业务事件”预留类型位
- [X] T007 [P] 在 `include/domain/model/program_snapshot.h`、`src/domain/model/program_snapshot.c`、`include/domain/model/state_transition_record.h`、`src/domain/model/state_transition_record.c` 创建程序快照与状态迁移记录领域对象
- [X] T008 扩展程序仓储端口以支持启动前校验和快照读取，在 `include/domain/ports/program_repository_port.h`、`include/adapters/outbound/file_program_repository.h`
- [X] T009 [P] 扩展事件日志端口以支持状态迁移、拒绝和忽略记录，在 `include/domain/ports/event_logger_port.h`、`include/adapters/logging/file_event_logger.h`
- [X] T010 为基础领域对象补充单元测试覆盖，在 `tests/unit/test_execution_model_entities.c`

**检查点**：基础领域对象、共享端口和公共结果码就绪，用户故事可按优先级独立推进。

---

## Phase 3：用户故事 1 - 按阶段完成一次洗车任务（优先级：P1）MVP

**目标**：让系统能够创建洗车会话，并通过离散事件/等待超时按阶段推进，而不是一次性串行跑完整个流程。  
**独立验证方式**：执行 `quickstart.md` 场景 A，或运行对应 contract / integration / simulation / unit 测试，验证一次有效启动可以创建会话、建立执行、等待反馈并形成单一完成结果。

### 用户故事 1 的验证任务

- [X] T011 [P] [US1] 为启动命令和反馈命令语义编写契约测试，在 `tests/contract/test_session_command_contract.c`
- [X] T012 [P] [US1] 为事件驱动推进主路径编写集成测试，在 `tests/integration/test_event_driven_session_progression.c`
- [X] T013 [P] [US1] 为正常推进闭环编写仿真测试，在 `tests/simulation/test_event_driven_progression.c`
- [X] T014 [P] [US1] 为会话与执行状态迁移编写单元测试，在 `tests/unit/test_wash_session_state_machine.c`
- [X] T014A [P] [US1] 为统一状态观测入口编写集成测试，在 `tests/integration/test_session_status_observability.c`

### 用户故事 1 的实现任务

- [X] T015 [P] [US1] 在 `include/domain/services/wash_session_state_machine.h` 和 `src/domain/services/wash_session_state_machine.c` 实现会话状态迁移规则
- [X] T016 [P] [US1] 在 `include/domain/services/wash_execution_service.h` 和 `src/domain/services/wash_execution_service.c` 实现单次执行推进与等待装配规则
- [X] T017 [US1] 在 `include/application/use_cases/process_wash_trigger.h` 和 `src/application/use_cases/process_wash_trigger.c` 实现统一触发处理用例，仅处理启动、停止、设备反馈、故障和超时五类正式触发源
- [X] T018 [US1] 在 `include/application/coordinators/wash_cycle_coordinator.h` 和 `src/application/coordinators/wash_cycle_coordinator.c` 重构为会话/执行编排入口
- [X] T019 [US1] 在 `include/application/use_cases/start_wash_cycle.h`、`src/application/use_cases/start_wash_cycle.c`、`include/adapters/inbound/cli_command_adapter.h`、`src/adapters/inbound/cli_command_adapter.c` 接入新启动路径
- [X] T020 [US1] 在 `include/platform/linux/main_loop.h` 和 `src/platform/linux/main_loop.c` 改造 Linux 主循环，使其只发现事件/超时并调用统一推进入口
- [X] T021 [US1] 在 `include/adapters/logging/file_event_logger.h` 和 `src/adapters/logging/file_event_logger.c` 写出正常推进所需的状态迁移记录
- [X] T021A [US1] 在 `include/application/use_cases/query_wash_session_status.h`、`src/application/use_cases/query_wash_session_status.c`、`include/adapters/inbound/cli_command_adapter.h`、`src/adapters/inbound/cli_command_adapter.c` 实现统一状态查询入口并输出最近一次状态迁移原因

**检查点**：到此为止，用户故事 1 应可独立运行和验证，形成本次重构的 MVP。

---

## Phase 4：用户故事 2 - 在停止、故障和超时下进入明确处理路径（优先级：P1）

**目标**：让运行中的会话在停止、故障、超时、迟到/重复事件和竞争输入场景下收敛到唯一且可记录的结果。  
**独立验证方式**：运行 `quickstart.md` 场景 D / E，或执行对应 contract / integration / simulation / unit 测试，验证系统按“故障 > 停止 > 超时 > 设备反馈”收敛并记录结果。

### 用户故事 2 的验证任务

- [X] T022 [P] [US2] 为状态结果事件、拒绝事件和最终完成事件编写契约测试，在 `tests/contract/test_execution_events_contract.c`
- [X] T023 [P] [US2] 为停止/故障/超时/重试耗尽路径编写集成测试，在 `tests/integration/test_session_exception_paths.c`
- [X] T024 [P] [US2] 为竞争事件优先级与迟到/重复事件处理编写仿真测试，在 `tests/simulation/test_trigger_priority_competition.c`
- [X] T025 [P] [US2] 为优先级服务、等待重试与超时决策编写单元测试，在 `tests/unit/test_trigger_priority_service.c`、`tests/unit/test_wait_timeout_service.c`

### 用户故事 2 的实现任务

- [X] T026 [P] [US2] 在 `include/domain/services/trigger_priority_service.h` 和 `src/domain/services/trigger_priority_service.c` 实现竞争事件优先级收敛规则
- [X] T027 [P] [US2] 在 `include/domain/services/wait_timeout_service.h` 和 `src/domain/services/wait_timeout_service.c` 实现等待超时判定、重试重建与重试耗尽后的后续路径决策
- [X] T028 [US2] 在 `include/application/use_cases/process_wash_trigger.h` 和 `src/application/use_cases/process_wash_trigger.c` 扩展停止、故障、超时、迟到和重复事件处理
- [X] T029 [US2] 在 `include/application/use_cases/stop_wash_cycle.h`、`src/application/use_cases/stop_wash_cycle.c`、`include/application/use_cases/acknowledge_fault.h`、`src/application/use_cases/acknowledge_fault.c` 改造为触发事件入口
- [X] T030 [US2] 在 `include/platform/linux/main_loop.h` 和 `src/platform/linux/main_loop.c` 加入等待超时发现与单一结果收敛逻辑
- [X] T031 [US2] 在 `include/adapters/logging/file_event_logger.h` 和 `src/adapters/logging/file_event_logger.c` 记录停止、故障、超时、忽略和拒绝路径结果

**检查点**：到此为止，用户故事 2 应可独立验证，异常与竞争输入路径明确且可观测。

---

## Phase 5：用户故事 3 - 运行中的任务使用稳定程序数据（优先级：P2）

**目标**：让会话在启动前完成程序数据校验，并在运行期间绑定不可变快照，不受外部程序更新直接影响。  
**独立验证方式**：运行 `quickstart.md` 场景 B / C，或执行对应 contract / integration / simulation / unit 测试，验证无效启动被拒绝、重复启动被拒绝、运行中配置变更只影响后续新会话。

### 用户故事 3 的验证任务

- [X] T032 [P] [US3] 为程序快照校验、启动前拒绝和运行中重复启动拒绝语义编写契约测试，在 `tests/contract/test_program_snapshot_contract.c`
- [X] T033 [P] [US3] 为程序快照一致性编写集成测试，在 `tests/integration/test_program_snapshot_consistency.c`
- [X] T034 [P] [US3] 为无效启动拒绝、运行中重复启动拒绝和运行中程序更新行为编写仿真测试，在 `tests/simulation/test_program_snapshot_runtime_behavior.c`
- [X] T035 [P] [US3] 为程序快照服务编写单元测试，在 `tests/unit/test_program_snapshot_service.c`

### 用户故事 3 的实现任务

- [X] T036 [P] [US3] 在 `include/domain/services/program_snapshot_service.h` 和 `src/domain/services/program_snapshot_service.c` 实现程序快照读取、校验和绑定规则
- [X] T037 [US3] 在 `include/adapters/outbound/file_program_repository.h` 和 `src/adapters/outbound/file_program_repository.c` 实现版本化快照输出与校验结果返回
- [X] T038 [US3] 在 `include/application/use_cases/start_wash_cycle.h`、`src/application/use_cases/start_wash_cycle.c`、`include/application/coordinators/wash_cycle_coordinator.h`、`src/application/coordinators/wash_cycle_coordinator.c` 强制执行启动前校验和快照绑定
- [X] T039 [US3] 在 `include/application/use_cases/update_program_config.h`、`src/application/use_cases/update_program_config.c`、`src/adapters/config/json_program_parser.c` 保证运行中会话不受外部配置修改直接影响

**检查点**：到此为止，用户故事 3 应可独立验证，程序数据快照与拒绝路径规则闭合。

---

## Phase 6：收尾与跨故事事项

**目的**：统一测试入口、更新文档、清理本次重构引入的多余串行路径残留，确保进入实现前后的交付可回归。

- [X] T040 [P] 更新执行模型 quickstart 场景与文档引用，补充等待重试与统一状态查询说明，在 `specs/002-wash-execution-refactor/quickstart.md`、`specs/002-wash-execution-refactor/contracts/session-command.md`、`specs/002-wash-execution-refactor/contracts/execution-events.md`、`specs/002-wash-execution-refactor/contracts/program-snapshot.md`
- [ ] T041 在 `tests/contract/test_main.c`、`tests/integration/test_main.c`、`tests/simulation/test_main.c`、`tests/unit/test_main.c` 完成全部新增测试的总入口注册与回归校验
- [X] T042 清理或替换串行主路径残留逻辑，避免新旧推进模型并存，在 `src/application/coordinators/wash_cycle_coordinator.c`、`src/application/use_cases/start_wash_cycle.c`、`src/platform/linux/main_loop.c`

---

## 依赖与执行顺序

### 阶段依赖

- **Phase 1：准备阶段**：无依赖，可立即开始
- **Phase 2：基础阶段**：依赖 Phase 1 完成，并阻塞所有用户故事
- **Phase 3：用户故事 1**：依赖 Phase 2 完成，是 MVP 与最先交付路径
- **Phase 4：用户故事 2**：依赖 Phase 2 完成，并依赖 US1 中统一触发处理入口已经存在
- **Phase 5：用户故事 3**：依赖 Phase 2 完成，并依赖 US1 中启动路径已经切换到会话模型
- **Phase 6：收尾阶段**：依赖所有目标用户故事完成

### 用户故事依赖

- **US1**：无业务前置故事依赖，是最小可交付增量
- **US2**：依赖 US1 已建立会话/执行推进主路径
- **US3**：依赖 US1 已建立启动与会话创建主路径

### 每个用户故事内部顺序

- 先写 contract / integration / simulation / unit 验证任务
- 再实现领域服务与状态机
- 再实现应用用例与协调器
- 最后接入平台主循环、适配器和日志

## 并行执行示例

### 用户故事 1

```bash
# 可并行的验证任务
T011 tests/contract/test_session_command_contract.c
T012 tests/integration/test_event_driven_session_progression.c
T013 tests/simulation/test_event_driven_progression.c
T014 tests/unit/test_wash_session_state_machine.c

# 可并行的领域实现任务
T015 include/domain/services/wash_session_state_machine.h + src/domain/services/wash_session_state_machine.c
T016 include/domain/services/wash_execution_service.h + src/domain/services/wash_execution_service.c
```

### 用户故事 2

```bash
# 可并行的验证任务
T022 tests/contract/test_execution_events_contract.c
T023 tests/integration/test_session_exception_paths.c
T024 tests/simulation/test_trigger_priority_competition.c
T025 tests/unit/test_trigger_priority_service.c

# 可并行的领域实现任务
T026 include/domain/services/trigger_priority_service.h + src/domain/services/trigger_priority_service.c
T027 include/domain/services/wait_timeout_service.h + src/domain/services/wait_timeout_service.c
```

### 用户故事 3

```bash
# 可并行的验证任务
T032 tests/contract/test_program_snapshot_contract.c
T033 tests/integration/test_program_snapshot_consistency.c
T034 tests/simulation/test_program_snapshot_runtime_behavior.c
T035 tests/unit/test_program_snapshot_service.c

# 可并行的实现任务
T036 include/domain/services/program_snapshot_service.h + src/domain/services/program_snapshot_service.c
T037 include/adapters/outbound/file_program_repository.h + src/adapters/outbound/file_program_repository.c
```

## 实施策略

### 先做 MVP

1. 完成 Phase 1 和 Phase 2
2. 完成 Phase 3（US1）
3. 验证 `quickstart.md` 场景 A 与 US1 对应测试
4. 以此作为本次重构的最小可交付闭环

### 增量交付

1. 先交付 US1，确保事件驱动推进模型成立
2. 再交付 US2，补齐停止/故障/超时/竞争路径
3. 最后交付 US3，闭合程序快照一致性与启动拒绝规则
4. 最后统一做 Phase 6 收尾和回归

### 多人并行策略

1. 团队共同完成 Phase 1 和 Phase 2
2. Phase 2 完成后先由开发者 A 完成 US1，打通统一触发处理入口与启动路径
3. US1 检查点完成后再并行推进：
   - 开发者 B：US2
   - 开发者 C：US3
4. 每个故事完成后独立回归，再合并进入 Phase 6

## 备注

- 所有 `[P]` 任务均要求文件集互不冲突或依赖明确
- 所有用户故事阶段任务都附带 `[US#]` 标签
- 本任务清单默认包含测试任务，因为规格、契约和 quickstart 已明确给出可验证场景
- 进入 `/speckit-implement` 前，应先确保 `tests/*/test_main.c` 能纳入新增测试目标
