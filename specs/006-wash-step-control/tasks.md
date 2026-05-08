# 任务：洗车工步真实控制模型

**输入**：来自 `/specs/006-wash-step-control/` 的设计文档  
**前置条件**：`plan.md`（必需）、`spec.md`（必需，用于用户故事）、`research.md`、`data-model.md`、`contracts/`

**验证**：每个故事或缺陷都必须在实现前定义明确的测试、复现步骤或人工检查方式。

**组织方式**：任务按用户故事分组，以保证每个故事都能独立实现和独立验证。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（如 `US1`、`US2`、`US3`）
- 描述中必须包含准确文件路径
- 每个实现任务或验证任务都必须附带 `验证：` 说明

## Phase 1：准备阶段（共享基础）

**目的**：准备本特性专用的测试入口、配置样例和构建挂接点。

- [X] T001 在 `tests/fixtures/wash_step_control/program_v1_valid.json`、`tests/fixtures/wash_step_control/program_v1_invalid_unsupported_conditional_controls.json`、`tests/fixtures/wash_step_control/program_v1_invalid_missing_exit_actions.json`、`tests/fixtures/wash_step_control/program_v1_invalid_position_semantics.json` 创建本特性配置样例。验证：样例文件覆盖合法程序、非法条件控制、缺失退出动作和位置语义冲突四类输入。
- [X] T002 在 `tests/CMakeLists.txt` 注册本特性新增的 contract/unit/integration/simulation 测试目标与夹具路径。验证：新增测试源文件都能被 CMake 明确纳入构建。
- [X] T003 [P] 在 `include/shared/timeouts.h` 和 `include/shared/error_codes.h` 增加工步段、收尾、关闭反馈和异常收口所需的超时常量与错误码占位。验证：后续实现不再散落魔法数字和临时错误字符串。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：建立所有用户故事共享的强类型骨架、端口扩展点和构建入口。  
**关键约束**：在本阶段完成前，不得开始任何用户故事开发。

- [X] T004 在 `include/domain/model/wash_program.h`、`include/domain/model/wash_segment.h`、`include/domain/model/segment_motion_plan.h`、`include/domain/model/position_trigger.h`、`include/domain/model/conditional_control.h`、`include/domain/model/exit_action.h`、`include/domain/model/segment_exception_policy.h` 重定义或新增工步段强类型头文件。验证：领域层公开模型能直接表达 `continuous_controls + conditional_controls + exit_actions` 固定结构。
- [X] T005 [P] 在 `src/domain/model/wash_program.c`、`src/domain/model/wash_segment.c`、`src/domain/model/segment_motion_plan.c`、`src/domain/model/position_trigger.c`、`src/domain/model/conditional_control.c`、`src/domain/model/exit_action.c`、`src/domain/model/segment_exception_policy.c` 实现对应的初始化、复制和边界检查逻辑。验证：所有强类型模型都具备可复用的构造与校验入口。
- [X] T006 [P] 在 `include/domain/model/domain_enums.h`、`include/domain/ports/actuator_port.h`、`include/domain/ports/sensor_port.h` 扩展执行机构品类、位置语义、反馈类型和异常结果枚举。验证：顶刷、侧刷、药剂、RO 水、风干、回零和位置快照都能被强类型枚举覆盖。
- [X] T007 在 `include/domain/model/program_snapshot.h`、`src/domain/model/program_snapshot.c`、`include/application/coordinators/system_context.h`、`include/domain/model/wash_execution.h`、`include/domain/model/wash_session.h` 重建工步运行时冻结视图和运行态容器。验证：应用层可以持有“当前工步、生命周期、条件控制激活集、退出开始时间、结果码”等运行态。
- [X] T008 在 `include/domain/model/program_validation.h` 和 `src/domain/model/program_validation.c` 建立新模型统一校验入口。验证：程序加载时能拒绝不支持的 `conditional_controls`、缺失完成条件、缺失退出动作和缺失执行机构映射。
- [X] T009 在 `src/CMakeLists.txt` 和 `tests/CMakeLists.txt` 挂接新增模型、服务、驱动与测试源文件，并移除只服务旧 `wash_stage` 的构建假设。验证：构建系统能编译新文件且不强依赖旧阶段模型文件名。

**检查点**：强类型模型、公共端口和构建入口已经就位，用户故事可以开始实现。

---

## Phase 3：用户故事 1 - 冻结工步段规则与工艺配置（优先级：P1）MVP

**目标**：把程序配置和领域模型收敛到真实工步段语义，明确已支持与未支持边界。  
**独立验证方式**：程序配置能映射为强类型工步段模型；非法字段组合、旧阶段冲突字段和超出 `conditional_controls v1` 范围的配置会被明确拒绝。

### 用户故事 1 的验证任务

- [X] T010 [P] [US1] 在 `tests/contract/test_program_config_adapter_contract.c` 为 JSON -> 强类型工步模型映射编写契约测试。验证：合法配置、缺失退出动作、旧阶段字段冲突、RO/风干条件控制伪支持都能得到固定结果。
- [X] T011 [P] [US1] 在 `tests/unit/test_execution_model_entities.c` 和 `tests/unit/test_program_config_validation.c` 为工步模型实体、位置语义声明和 `conditional_controls v1` 边界编写单元测试。验证：固定字段结构、唯一性规则和未支持能力拒绝规则都先失败后通过。

### 用户故事 1 的实现任务

- [X] T012 [US1] 在 `include/adapters/config/json_program_parser.h` 和 `src/adapters/config/json_program_parser.c` 重写解析器输出类型，使其直接产出 `wash_program_v1_t` 与固定结构的 `wash_segment_t`。验证：运行期不再把字符串一路带入领域层或应用层。
- [X] T013 [US1] 在 `src/adapters/config/json_program_parser.c` 实现 `motion_plan`、`continuous_controls`、`conditional_controls`、`completion_condition`、`exit_actions` 和 `exception_policy` 的严格映射。验证：字段合法组合能成功映射，非法组合会返回明确失败。
- [X] T014 [US1] 在 `src/adapters/config/json_program_parser.c` 把 `conditional_controls v1` 收紧为仅支持药剂窗口控制。验证：RO 水、风干和其他执行机构的通用条件控制配置会被显式拒绝。
- [X] T015 [US1] 在 `include/domain/model/wash_program.h`、`src/domain/model/wash_program.c`、`include/domain/model/wash_segment.h`、`src/domain/model/wash_segment.c` 实现工步程序与工步段的固定结构访问接口。验证：领域模型不再依赖 `wash_stage_t` 可变字段集合解释业务。
- [X] T016 [US1] 在 `include/domain/services/program_snapshot_service.h`、`src/domain/services/program_snapshot_service.c`、`include/domain/model/program_snapshot.h`、`src/domain/model/program_snapshot.c` 更新冻结逻辑，使快照围绕工步段模型而不是旧阶段模型。验证：运行时快照能完整保留段目标、条件控制和退出动作。
- [X] T017 [US1] 在 `include/domain/ports/program_repository_port.h`、`include/adapters/outbound/file_program_repository.h`、`src/adapters/outbound/file_program_repository.c` 移除内建默认程序回退和旧阶段伪成功路径。验证：配置适配失败时仓储返回明确错误，而不是加载伪默认程序。
- [X] T018 [US1] 在 `tests/contract/test_program_config_adapter_contract.c`、`tests/unit/test_program_config_validation.c`、`tests/fixtures/wash_step_control/program_v1_valid.json`、`tests/fixtures/wash_step_control/program_v1_invalid_unsupported_conditional_controls.json`、`tests/fixtures/wash_step_control/program_v1_invalid_missing_exit_actions.json`、`tests/fixtures/wash_step_control/program_v1_invalid_position_semantics.json` 对照规格回归用户故事 1 验收场景。验证：顶刷段整段喷药、侧刷延迟喷药、未支持字段拒绝和 RO/风干能力缺口识别全部可复现。

**检查点**：配置适配层和领域模型已经真实表达工步段，MVP 可单独验证。

---

## Phase 4：用户故事 2 - 主控按工步段持续推进真实工艺（优先级：P1）

**目标**：让主控从旧阶段一次性下发模型切换为“进入段 -> 运行段 -> 退出段”的持续编排。  
**独立验证方式**：顶刷、侧刷、RO 水、风干工步都能在运行期持续判断位置、条件控制启停、本段完成和退出完成，而不是仅等待单次反馈。

### 用户故事 2 的验证任务

- [X] T019 [P] [US2] 在 `tests/contract/test_segment_runtime_orchestration_contract.c` 为工步生命周期、退出动作并行触发和收尾完成判定编写契约测试。验证：进入、运行、退出、完成和中止语义与契约保持一致。
- [X] T020 [P] [US2] 在 `tests/integration/test_segment_runtime_orchestration.c` 和 `tests/integration/test_controller_runtime_persistence.c` 为主控编排和状态投影编写集成测试。验证：本段完成与收尾完成被明确区分，退出动作全部完成后才进入下一段。
- [X] T021 [P] [US2] 在 `tests/simulation/test_segment_runtime_control.c` 为顶刷整段喷药、侧刷延迟喷药、到尾停药停刷并回零和并行退出动作编写仿真测试。验证：真实工艺在主循环里持续评估而不是一次性下发。

### 用户故事 2 的实现任务

- [X] T022 [US2] 在 `include/domain/services/wash_execution_service.h` 和 `src/domain/services/wash_execution_service.c` 用工步段生命周期替换旧 `begin_next_stage/handle_feedback` 语义。验证：执行服务以 `ENTERING/RUNNING/EXITING/COMPLETED/ABORTED` 推进工步。
- [X] T023 [P] [US2] 在 `include/domain/services/segment_control_service.h` 和 `src/domain/services/segment_control_service.c` 新增持续控制与条件控制评估服务。验证：顶刷跟随、药剂窗口控制、RO 水运行和风干运行都能在每个周期按强类型规则判断。
- [X] T024 [P] [US2] 在 `include/domain/services/wash_session_state_machine.h` 和 `src/domain/services/wash_session_state_machine.c` 重构会话状态机以承接工步进入、运行、退出和异常中止。验证：会话结果不再依赖旧阶段完成语义。
- [X] T025 [US2] 在 `src/application/use_cases/process_wash_trigger.c`、`src/application/use_cases/start_wash_cycle.c`、`src/application/use_cases/stop_wash_cycle.c` 重建统一工步编排入口。验证：正式触发、停止和继续推进都通过同一工步编排主链路收口。
- [X] T026 [US2] 在 `src/application/use_cases/process_formal_command.c`、`src/application/use_cases/query_wash_session_status.c`、`src/application/coordinators/runtime_result_projection.c` 更新对外状态投影和响应文本。验证：主控对外能表达当前段、生命周期、异常原因和收尾状态。
- [X] T027 [US2] 在 `include/platform/linux/main_loop.h` 和 `src/platform/linux/main_loop.c` 增加运行期持续评估调度点。验证：主循环节拍内会重复评估位置、条件控制启停、本段完成和退出动作完成。
- [X] T028 [US2] 在 `src/application/coordinators/compatibility_trigger_runner.c` 清理仍会主导结果的旧阶段兼容推进路径。验证：运行中不再允许旧 `wash_stage` 路径旁路决定流程结果。
- [X] T029 [US2] 在 `tests/contract/test_segment_runtime_orchestration_contract.c`、`tests/integration/test_segment_runtime_orchestration.c`、`tests/simulation/test_segment_runtime_control.c` 对照规格完成用户故事 2 验收。验证：持续判断、并行退出和顶刷回零正式收尾全部通过。

**检查点**：主控已经按工步段持续推进真实工艺，且可以独立演示。

---

## Phase 5：用户故事 3 - 用统一位置语义和异常收尾完成验收（优先级：P1）

**目标**：统一位置语义、异常处理和收尾策略，并补齐 RO 水/风干执行机构与反馈闭环。  
**独立验证方式**：位置驱动启停点可解释，段内超时与收尾超时可区分，RO 水/风干关闭和收尾中断行为有固定结果。

### 用户故事 3 的验证任务

- [X] T030 [P] [US3] 在 `tests/contract/test_position_exit_semantics_contract.c` 为位置语义、段完成/收尾完成区分和关闭完成判定编写契约测试。验证：相对位置、绝对位置、车头/车尾和回零完成语义都可单独验证。
- [X] T031 [P] [US3] 在 `tests/integration/test_segment_exception_paths.c` 为位置丢失、跟随失效、段内超时、收尾超时、stop/fault 打断收尾编写集成测试。验证：应用层异常收口路径固定且可观察。
- [X] T032 [P] [US3] 在 `tests/simulation/test_segment_exception_behavior.c` 为 RO 水关闭失败、风干关闭失败、药剂关闭失败和回零失败编写仿真测试。验证：关闭反馈优先、命令 + 超时兜底和异常结果投影都能复现。

### 用户故事 3 的实现任务

- [X] T033 [US3] 在 `include/domain/model/position_trigger.h`、`src/domain/model/position_trigger.c`、`include/domain/model/wait_condition.h`、`src/domain/model/wait_condition.c` 实现统一位置触发器和完成条件判定。验证：每条条件控制规则都必须显式声明位置基准并在规则内保持一致。
- [X] T034 [P] [US3] 在 `include/domain/ports/sensor_port.h`、`src/platform/drivers/simulated_sensor_driver.c`、`include/platform/drivers/simulated_driver_context.h` 扩展统一位置快照、车头/车尾、home、position_valid 和关闭反馈事实。验证：领域层可以从同一份快照评估启停点和完成条件。
- [X] T035 [P] [US3] 在 `include/platform/drivers/simulated_ro_water_driver.h`、`src/platform/drivers/simulated_ro_water_driver.c`、`include/platform/drivers/simulated_dryer_driver.h`、`src/platform/drivers/simulated_dryer_driver.c` 新增 RO 水和风干仿真驱动。验证：RO 水和风干具备独立启停、关闭反馈和失败注入能力。
- [X] T036 [US3] 在 `include/domain/ports/actuator_port.h`、`src/platform/drivers/simulated_brush_driver.c`、`src/platform/drivers/simulated_chemical_driver.c`、`src/platform/drivers/simulated_gantry_driver.c`、`src/platform/drivers/simulated_ro_water_driver.c`、`src/platform/drivers/simulated_dryer_driver.c` 扩展执行机构关闭命令与反馈接口。验证：顶刷、侧刷、药剂、RO 水、风干和回零都能被统一下发与确认。
- [X] T037 [US3] 在 `src/domain/services/fault_policy_service.c`、`src/domain/services/recovery_state_machine.c`、`src/domain/services/wait_timeout_service.c` 实现段内超时、收尾超时、跟随失效、位置丢失和退出失败决议。验证：直接中止与先收尾再停机策略区分明确，收尾阶段 `stop/fault` 会立即整体中断。
- [X] T038 [US3] 在 `src/application/coordinators/runtime_event_recorder.c`、`src/application/coordinators/runtime_result_projection.c`、`src/adapters/logging/file_event_logger.c` 记录位置语义、关闭完成方式和异常收口结果。验证：验收人员可从日志和状态投影追溯每个启停点和异常原因。
- [X] T039 [US3] 在 `tests/contract/test_position_exit_semantics_contract.c`、`tests/integration/test_segment_exception_paths.c`、`tests/simulation/test_segment_exception_behavior.c`、`tests/hil/test_hil_smoke.sh` 对照规格完成用户故事 3 验收。验证：位置语义、异常收尾、RO 水/风干关闭和 stop/fault 抢占路径全部通过。

**检查点**：位置语义、异常策略和执行机构闭环已经统一，可独立完成调试与验收。

---

## Phase 6：收尾与跨故事事项

**目的**：清理旧阶段残留、补正文档并完成全链路回归。

- [X] T040 [P] 在 `include/domain/model/wash_stage.h`、`src/domain/model/wash_stage.c`、`src/domain/services/brush_control_state_machine.c`、`src/domain/services/chemical_dosing_state_machine.c` 清理仅服务旧阶段模型且不再需要的代码。验证：正式流程中不再保留与新工步段模型冲突的有效旧语义。
- [X] T041 在 `include/adapters/config/json_program_parser.h`、`include/domain/model/wash_program.h`、`include/domain/model/wash_segment.h`、`include/domain/model/segment_motion_plan.h`、`include/domain/model/position_trigger.h`、`include/domain/model/conditional_control.h`、`include/domain/model/exit_action.h`、`include/domain/model/segment_exception_policy.h`、`include/domain/model/program_snapshot.h`、`include/domain/model/wash_execution.h`、`include/domain/model/wash_session.h`、`include/domain/model/domain_enums.h`、`include/application/coordinators/system_context.h`、`include/domain/services/program_snapshot_service.h`、`include/domain/services/wash_execution_service.h`、`include/domain/services/wash_session_state_machine.h`、`include/domain/services/fault_policy_service.h`、`include/domain/services/recovery_state_machine.h`、`include/domain/services/wait_timeout_service.h`、`include/domain/services/segment_control_service.h`、`include/domain/ports/actuator_port.h`、`include/domain/ports/sensor_port.h`、`include/domain/ports/program_repository_port.h`、`include/adapters/outbound/file_program_repository.h`、`include/platform/linux/main_loop.h`、`include/platform/drivers/simulated_driver_context.h`、`include/platform/drivers/simulated_ro_water_driver.h`、`include/platform/drivers/simulated_dryer_driver.h` 补齐或更新中文 Doxygen 注释。验证：所有新增或重定义的公开接口、结构体、枚举、状态机接口、运行时协调接口和硬件相关 API 都满足宪章要求的中文 Doxygen 覆盖。
- [X] T042 [P] 在 `specs/006-wash-step-control/quickstart.md` 和 `tests/hil/README.md` 更新最终验证步骤、HIL smoke 预期和异常注入说明。验证：文档能指导完整执行顶刷、侧刷、RO 水、风干和异常收尾验收。
- [X] T043 在 `tests/CMakeLists.txt`、`tests/contract/test_main.c`、`tests/integration/test_main.c`、`tests/simulation/test_main.c` 汇总新增测试入口并清理废弃目标。验证：测试入口与新增用例一一对应，不保留无效目标。
- [X] T044 在 `specs/006-wash-step-control/quickstart.md`、`tests/contract/test_program_config_adapter_contract.c`、`tests/contract/test_segment_runtime_orchestration_contract.c`、`tests/contract/test_position_exit_semantics_contract.c`、`tests/integration/test_segment_runtime_orchestration.c`、`tests/integration/test_segment_exception_paths.c`、`tests/simulation/test_segment_runtime_control.c`、`tests/simulation/test_segment_exception_behavior.c` 执行并记录最终回归所需的验证矩阵。验证：US1/US2/US3 的独立验证方式和总体验证顺序都能直接追溯到规格。

---

## 依赖与执行顺序

### 阶段依赖

- **说明**：`US1`、`US2`、`US3` 在规格中都属于业务优先级 `P1`；此处的 Phase 顺序仅表达技术依赖，不表示业务价值降级。

- **准备阶段（Phase 1）**：无依赖，可立即开始
- **基础阶段（Phase 2）**：依赖准备阶段完成，并阻塞所有用户故事
- **用户故事 1（Phase 3）**：依赖基础阶段完成，是后续工步编排的模型与配置前提
- **用户故事 2（Phase 4）**：依赖用户故事 1 完成，因为主链路必须消费新的强类型工步模型
- **用户故事 3（Phase 5）**：依赖用户故事 2 完成，因为位置语义和异常收口需要挂接新的运行时编排
- **收尾阶段（Phase 6）**：依赖所有目标用户故事完成

### 用户故事依赖

- **用户故事 1（P1）**：基础阶段完成后即可开始，不依赖其他故事
- **用户故事 2（P1）**：依赖 US1 的强类型模型、配置适配和程序快照
- **用户故事 3（P1）**：依赖 US2 的生命周期编排和统一状态投影

### 每个用户故事内部顺序

- 先定义验证，再开始实现
- 配置/契约先于运行时编排
- 领域模型先于应用编排
- 应用编排先于平台驱动和仿真闭环
- C 代码验收前必须完成空指针、边界、超时和错误返回检查
- 当前故事完成并验证后再进入下一故事

### 可并行机会

- Phase 1 中的 `T002` 与 `T003` 可并行
- Phase 2 中的 `T005` 与 `T006` 可并行
- US1 中的 `T010` 与 `T011` 可并行，`T013` 与 `T014` 可并行
- US2 中的 `T019`、`T020`、`T021` 可并行，`T023` 与 `T024` 可并行
- US3 中的 `T030`、`T031`、`T032` 可并行，`T034` 与 `T035` 可并行
- 收尾阶段中的 `T040`、`T042`、`T043` 可并行

---

## 并行示例：用户故事 1

```bash
# 一起执行用户故事 1 的验证任务
Task: "在 tests/contract/test_program_config_adapter_contract.c 为 JSON -> 强类型工步模型映射编写契约测试"
Task: "在 tests/unit/test_execution_model_entities.c 和 tests/unit/test_program_config_validation.c 为工步模型实体和 v1 边界编写单元测试"

# 一起执行用户故事 1 的解析实现拆分任务
Task: "在 src/adapters/config/json_program_parser.c 实现 motion_plan、continuous_controls、conditional_controls、completion_condition、exit_actions 和 exception_policy 的严格映射"
Task: "在 src/adapters/config/json_program_parser.c 把 conditional_controls v1 收紧为仅支持药剂窗口控制"
```

## 并行示例：用户故事 2

```bash
# 一起执行用户故事 2 的验证任务
Task: "在 tests/contract/test_segment_runtime_orchestration_contract.c 为工步生命周期编写契约测试"
Task: "在 tests/integration/test_segment_runtime_orchestration.c 为主控编排编写集成测试"
Task: "在 tests/simulation/test_segment_runtime_control.c 为顶刷/侧刷/退出动作编写仿真测试"

# 一起执行用户故事 2 的服务拆分任务
Task: "在 src/domain/services/segment_control_service.c 新增持续控制与条件控制评估服务"
Task: "在 src/domain/services/wash_session_state_machine.c 重构会话状态机以承接工步生命周期"
```

## 并行示例：用户故事 3

```bash
# 一起执行用户故事 3 的验证任务
Task: "在 tests/contract/test_position_exit_semantics_contract.c 为位置语义和退出判定编写契约测试"
Task: "在 tests/integration/test_segment_exception_paths.c 为异常收口路径编写集成测试"
Task: "在 tests/simulation/test_segment_exception_behavior.c 为关闭失败和超时兜底编写仿真测试"

# 一起执行用户故事 3 的平台闭环任务
Task: "在 src/platform/drivers/simulated_sensor_driver.c 扩展统一位置快照和关闭反馈事实"
Task: "在 src/platform/drivers/simulated_ro_water_driver.c 和 src/platform/drivers/simulated_dryer_driver.c 新增 RO 水和风干仿真驱动"
```

---

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1：准备阶段
2. 完成 Phase 2：基础阶段
3. 完成 Phase 3：用户故事 1
4. 运行 US1 的 contract/unit 验证
5. 以“配置已真实表达工步段且未支持能力被明确拒绝”作为 MVP 交付标准

### 增量交付

1. 先完成准备阶段和基础阶段
2. 交付用户故事 1，锁定强类型模型和配置边界
3. 交付用户故事 2，切换主控到工步生命周期编排
4. 交付用户故事 3，完成位置语义、异常收口和 RO/风干闭环
5. 最后执行收尾阶段清理旧阶段残留并完成全链路回归

### 多人并行策略

1. 团队先共同完成 Phase 1 和 Phase 2
2. US1 期间：
   - 开发者 A：`json_program_parser` 与程序仓储
   - 开发者 B：工步模型与程序校验
3. US2 期间：
   - 开发者 A：执行服务与会话状态机
   - 开发者 B：应用编排与主循环
4. US3 期间：
   - 开发者 A：位置/异常语义
   - 开发者 B：RO 水/风干驱动与反馈闭环

---

## 备注

- `[P]` 表示不同文件、无依赖冲突，可并行
- `[US1]`、`[US2]`、`[US3]` 标签用于把任务映射到规格中的具体用户故事
- 每个用户故事都带有独立验证任务和独立验收检查点
- 所有任务都直接对应 `spec.md`、`plan.md`、`data-model.md`、`contracts/` 和 `quickstart.md` 中的明确要求
