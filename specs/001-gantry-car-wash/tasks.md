# 任务：往复式龙门洗车机控制软件

**输入**：来自 `/specs/001-gantry-car-wash/` 的设计文档  
**前置条件**：`plan.md`（必需）、`spec.md`（必需，用于用户故事）、`research.md`、`data-model.md`、`contracts/`

**验证**：每个故事都必须先具备可执行的验证任务，再开始实现；实现完成后需通过对应单元测试、契约测试、集成/仿真测试或人工检查。

**组织方式**：任务按用户故事分组，确保每个故事都能独立实现、独立验证、独立演示。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（如 `US1`、`US2`、`US3`）
- 描述中必须包含准确文件路径
- 每个任务都带 `验证：` 说明

## 路径约定

- 核心代码：`src/`、`include/`
- 测试代码：`tests/unit/`、`tests/contract/`、`tests/integration/`、`tests/simulation/`、`tests/hil/`
- 配置与运行目录：`configs/`、`runtime/`

---

## Phase 1：准备阶段（共享基础）

**目的**：建立可构建、可测试、可运行的最小工程骨架。

- [X] T001 创建 `CMakeLists.txt`、`src/CMakeLists.txt`、`tests/CMakeLists.txt`，建立 `wash_controller`、`wash_cli`、测试目标的基础构建入口。验证：执行 `cmake -S . -B build` 能成功生成工程文件。
- [X] T002 创建 `src/main/main.c`、`src/main/wash_cli_main.c`、`src/platform/linux/main_loop.c`、`include/platform/linux/main_loop.h`，建立主控守护进程和本地命令行入口骨架。验证：执行 `cmake --build build` 后可生成空骨架二进制。
- [X] T003 [P] 创建 `.clang-format`、`.clang-tidy`、`.editorconfig`，固化 4 空格缩进、禁 Tab、命名和头文件规则。验证：格式规则与 `.specify/memory/constitution.md` 的嵌入式 C 约束一致。
- [X] T004 [P] 创建 `configs/programs/standard_wash.json`、`configs/vehicle_types.json`、`configs/system_limits.json`、`runtime/logs/.gitkeep`、`runtime/state/.gitkeep`，建立最小配置与运行目录。验证：文件路径与 `specs/001-gantry-car-wash/quickstart.md` 的启动路径一致。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：完成所有用户故事共用的基础能力；本阶段完成前不得进入故事实现。

- [X] T005 创建 `include/shared/error_codes.h`、`include/shared/timeouts.h`、`include/shared/result_types.h`，定义统一错误码、超时、最大重试次数和结果类型。验证：所有硬件、通信、存储路径后续都可直接复用具名常量，不引入魔法数字。
- [X] T006 [P] 创建 `include/domain/ports/actuator_port.h`、`include/domain/ports/sensor_port.h`、`include/domain/ports/program_repository_port.h`、`include/domain/ports/event_logger_port.h`、`include/domain/ports/command_port.h`，定义六边形端口接口。验证：头文件仅含声明并具备防重复包含保护。
- [X] T007 [P] 创建 `src/adapters/outbound/file_program_repository.c`、`include/adapters/outbound/file_program_repository.h`、`src/adapters/logging/file_event_logger.c`、`include/adapters/logging/file_event_logger.h`，搭建文件持久化与日志适配器骨架。验证：接口返回显式错误码，且预留重试、降级或停止分支。
- [X] T008 [P] 创建 `src/platform/drivers/simulated_gantry_driver.c`、`src/platform/drivers/simulated_brush_driver.c`、`src/platform/drivers/simulated_chemical_driver.c`、`src/platform/drivers/simulated_sensor_driver.c` 及对应头文件，搭建仿真驱动骨架。验证：各驱动接口都包含超时、重试和状态反馈参数。
- [X] T009 创建 `tests/unit/test_main.c`、`tests/contract/test_main.c`、`tests/integration/test_main.c`、`tests/simulation/test_main.c`，建立 CTest 测试入口与公共夹具。验证：执行 `ctest --test-dir build --output-on-failure` 时能发现空测试集。
- [X] T010 创建 `include/domain/model/domain_enums.h`、`include/domain/events/event_types.h`、`include/application/dto/command_dto.h`，统一周期状态、告警分类、命令类型和事件类型枚举。验证：`spec.md`、`data-model.md`、`contracts/` 中的关键术语均能映射到代码枚举。
- [X] T010A 创建 `src/adapters/inbound/maintenance_command_adapter.c`、`include/adapters/inbound/maintenance_command_adapter.h`，并在 `src/main/wash_cli_main.c` 中建立自动洗车命令与维护命令的共享分发骨架。验证：启动、维护、配置相关命令入口文件已创建，后续故事仅补充分支逻辑，不再重复创建入口骨架。

**检查点**：基础完成后，US1/US2/US3 才能并行推进。

---

## Phase 3：用户故事 1 - 自动完成洗车流程（优先级：P1）MVP

**目标**：操作员手动选程序后，系统自动完成预检、龙门往复、刷组协同、药剂投加和周期结束记录。

**独立验证方式**：执行标准程序仿真场景，系统在无人干预下完成一轮洗车，写出结束结果和运行日志。

### 用户故事 1 的验证任务

- [X] T011 [P] [US1] 编写 `tests/unit/test_wash_cycle_state_machine.c`、`tests/unit/test_precheck_rules.c`，覆盖预检通过/失败、阶段推进、周期结束状态迁移。验证：US1 相关状态机测试先失败，再在实现完成后全部通过。
- [X] T012 [P] [US1] 编写 `tests/contract/test_start_wash_cycle_contract.c`、`tests/contract/test_cycle_finished_contract.c`，校验 `operator-control.md` 的启动命令和 `fault-event.md` 的结束事件格式。验证：命令受理结果、拒绝原因和最终结果码与契约一致。
- [X] T013 [US1] 编写 `tests/integration/test_normal_wash_cycle.c`，用仿真驱动复现标准洗车闭环。验证：覆盖 `V-001`，确认预检、车辆适配、往返运动、刷组动作、药剂投加和结束提示完整发生。

### 用户故事 1 的实现任务

- [X] T014 [P] [US1] 实现 `src/domain/model/wash_program.c`、`include/domain/model/wash_program.h`、`src/domain/model/wash_stage.c`、`include/domain/model/wash_stage.h`，承载阶段配置、顺序和超时规则。验证：程序与阶段模型可通过 T011 中的阶段顺序和超时校验测试。
- [X] T015 [P] [US1] 实现 `src/domain/model/vehicle_type.c`、`include/domain/model/vehicle_type.h`、`src/domain/model/chemical_channel.c`、`include/domain/model/chemical_channel.h`、`src/domain/model/chemical_action.c`、`include/domain/model/chemical_action.h`。验证：车辆类别匹配、药剂动作时机与默认投加时长规则可通过单元测试。
- [X] T016 [US1] 实现 `src/domain/model/wash_cycle.c`、`include/domain/model/wash_cycle.h`，管理待机、预检、运行中、完成等周期状态。验证：周期状态迁移与结果码生成可通过 `tests/unit/test_wash_cycle_state_machine.c`。
- [X] T017 [US1] 实现 `src/domain/services/precheck_service.c`、`include/domain/services/precheck_service.h`，完成设备空闲、安全条件、关键资源、车辆到位和尺寸合规检查。验证：预检拒绝路径可返回 `PRECHECK_FAILED` 或 `VEHICLE_NOT_ALLOWED`。
- [X] T018 [US1] 实现 `src/domain/services/gantry_motion_state_machine.c`、`include/domain/services/gantry_motion_state_machine.h`、`src/domain/services/brush_control_state_machine.c`、`include/domain/services/brush_control_state_machine.h`、`src/domain/services/chemical_dosing_state_machine.c`、`include/domain/services/chemical_dosing_state_machine.h`。验证：龙门、顶刷/侧刷、药剂动作能按阶段协调推进并带超时和最大重试次数。
- [X] T019 [US1] 实现 `src/application/coordinators/wash_cycle_coordinator.c`、`include/application/coordinators/wash_cycle_coordinator.h`，编排预检、阶段执行、周期收尾和结果输出。验证：`tests/integration/test_normal_wash_cycle.c` 可通过并生成唯一 `cycle_finished` 结果。
- [X] T020 [US1] 实现 `src/application/use_cases/start_wash_cycle.c`、`include/application/use_cases/start_wash_cycle.h`、`src/adapters/inbound/cli_command_adapter.c`、`include/adapters/inbound/cli_command_adapter.h`，接入手动选程序和启动命令。验证：`tests/contract/test_start_wash_cycle_contract.c` 可验证受理与拒绝语义。
- [X] T021 [US1] 补全 `src/main/wash_cli_main.c`、`src/adapters/outbound/file_program_repository.c`、`src/adapters/logging/file_event_logger.c`，支持从 `configs/` 加载程序、执行一次标准周期并写出日志。验证：按 `quickstart.md` 的 `wash_cli start --program standard_wash` 路径可跑通仿真闭环。

**检查点**：US1 完成后，系统应已具备可独立演示的最小洗车主流程。

---

## Phase 4：用户故事 2 - 配置洗车程序与工艺参数（优先级：P2）

**目标**：维护人员可编辑、保存并复用洗车程序和车辆适配配置。

**独立验证方式**：修改程序阶段或药剂规则后重新执行，系统加载并使用新配置。

### 用户故事 2 的验证任务

- [X] T022 [P] [US2] 编写 `tests/contract/test_program_config_contract.c`、`tests/unit/test_program_config_validation.c`，校验程序配置结构、阶段字段和药剂动作规则。验证：非法配置会被拒绝，合法配置可通过契约校验。
- [X] T023 [US2] 编写 `tests/integration/test_program_update_persistence.c`，验证程序修改后重载生效。验证：覆盖 `V-002`，确认修改后的阶段顺序、往返次数和药剂规则在下一次执行时生效。

### 用户故事 2 的实现任务

- [X] T024 [P] [US2] 实现 `src/adapters/config/json_program_parser.c`、`include/adapters/config/json_program_parser.h`，完成程序配置和车辆类别配置的解析。验证：解析错误可返回显式配置错误码，并通过 `tests/contract/test_program_config_contract.c`。
- [X] T025 [P] [US2] 实现 `src/domain/model/program_validation.c`、`include/domain/model/program_validation.h`，校验阶段顺序、往返次数、药剂动作和车辆适配关系。验证：非法 `stage_timeout_ms`、重复 `stage_id`、引用不存在通道等场景均被拦截。
- [X] T026 [US2] 实现 `src/application/use_cases/update_program_config.c`、`include/application/use_cases/update_program_config.h`，完成配置更新、版本递增和持久化流程。验证：更新流程通过 `tests/integration/test_program_update_persistence.c`。
- [X] T027 [US2] 补全 `src/adapters/inbound/maintenance_command_adapter.c` 的配置导入、导出和修改命令分支，并实现 `src/application/use_cases/update_program_config.c` 与适配器联动。验证：维护命令只在授权配置路径下生效，且配置修改后可保存并重新加载。
- [X] T028 [US2] 更新 `configs/programs/standard_wash.json`、`configs/programs/heavy_wash.json`、`configs/vehicle_types.json`，提供至少两套可执行示例程序和车辆映射。验证：操作员可在下一次启动前明确选择或确认对应程序。

**检查点**：US2 完成后，程序工艺参数应可独立调整并在后续周期中生效。

---

## Phase 5：用户故事 3 - 安全告警与人工处置（优先级：P3）

**目标**：系统在异常时执行安全动作、记录告警，并支持人工确认、维护模式和恢复到安全待机。

**独立验证方式**：注入急停、药剂不足、刷组异常、位置异常和断电恢复场景，确认系统执行停机、降级、告警和恢复流程。

### 用户故事 3 的验证任务

- [X] T029 [P] [US3] 编写 `tests/unit/test_fault_policy_service.c`、`tests/unit/test_recovery_state_machine.c`，覆盖安全类故障立即停机、工艺/资源类故障降级/跳过/安全结束，以及恢复回到待机。验证：故障分级和恢复状态机先失败，完成实现后全部通过。
- [X] T030 [P] [US3] 编写 `tests/contract/test_fault_event_contract.c`、`tests/contract/test_maintenance_command_contract.c`，校验告警事件输出和维护/点动命令契约。验证：事件消息、严重级别、处置动作和维护命令限制与契约一致。
- [X] T031 [US3] 编写 `tests/integration/test_fault_handling_scenarios.c`、`tests/simulation/test_power_loss_recovery.c`，复现急停、药剂不足、刷组异常、运动异常和断电恢复场景。验证：覆盖 `V-003`，确认系统回到安全待机且禁止断点续洗。

### 用户故事 3 的实现任务

- [X] T032 [P] [US3] 实现 `src/domain/model/fault_event.c`、`include/domain/model/fault_event.h`、`src/domain/model/safety_interlock.c`、`include/domain/model/safety_interlock.h`，建模告警事件和安全联锁。验证：所有停机、降级和禁止启动事件都能生成中文事件描述并写日志。
- [X] T033 [US3] 实现 `src/domain/services/fault_policy_service.c`、`include/domain/services/fault_policy_service.h`、`src/domain/services/recovery_state_machine.c`、`include/domain/services/recovery_state_machine.h`。验证：安全类故障立即停机，工艺/资源类故障按阶段策略处理，恢复后重新预检才能重启。
- [X] T034 [US3] 实现 `src/application/use_cases/stop_wash_cycle.c`、`include/application/use_cases/stop_wash_cycle.h`、`src/application/use_cases/acknowledge_fault.c`、`include/application/use_cases/acknowledge_fault.h`。验证：人工终止和告警确认路径可通过 `tests/contract/test_maintenance_command_contract.c`。
- [X] T035 [US3] 实现 `src/application/use_cases/enter_maintenance_mode.c`、`include/application/use_cases/enter_maintenance_mode.h`、`src/application/use_cases/jog_actuator.c`、`include/application/use_cases/jog_actuator.h`、`src/application/coordinators/maintenance_coordinator.c`、`include/application/coordinators/maintenance_coordinator.h`。验证：维护模式下可点动龙门、刷组和药剂通道，且每次点动都受超时约束。
- [X] T036 [US3] 补全 `src/adapters/inbound/maintenance_command_adapter.c` 的告警确认、维护模式和点动命令分支，以及 `src/adapters/logging/file_event_logger.c`、`src/platform/linux/main_loop.c`、`src/platform/drivers/simulated_sensor_driver.c` 的故障监测与恢复联动。验证：`tests/integration/test_fault_handling_scenarios.c` 与 `tests/simulation/test_power_loss_recovery.c` 可通过。

**检查点**：US3 完成后，系统应已具备可演示的安全联锁、故障处置、维护和恢复闭环。

---

## Phase 6：收尾与跨故事事项

**目的**：完成跨故事验证、性能预算检查和交付文档收口。

- [X] T037 [P] 创建 `tests/hil/test_hil_smoke.sh`、`tests/hil/README.md`，固化真实硬件或 HIL 冒烟验证步骤。验证：HIL 用例至少覆盖正常洗车、急停和药剂不足三个场景。
- [X] T038 [P] 编写 `tests/simulation/test_basic_control_loop.c`，验证主控制循环、状态推进和安全停机流程能够稳定运行且无死锁、无超时卡死。验证：正常洗车、急停和恢复场景均可完整跑通并输出可复核日志。
- [X] T039 更新 `specs/001-gantry-car-wash/quickstart.md`、`runtime/state/.gitkeep`，补齐真实构建、运行、故障注入和恢复命令。验证：新成员按文档可独立跑通最小闭环。
- [X] T040 在 `specs/001-gantry-car-wash/checklists/implementation-evidence.md` 记录 `ctest --test-dir build --output-on-failure`、仿真测试、HIL 测试、C 规范审查和宪章对齐证据。验证：US1、US2、US3 的验证结果和关键风险都有明确记录。

---

## 依赖与执行顺序

### 阶段依赖

- **Phase 1**：无依赖，可立即开始
- **Phase 2**：依赖 Phase 1 完成，并阻塞全部用户故事
- **Phase 3（US1）**：依赖 Phase 2 完成，是 MVP 最小交付
- **Phase 4（US2）**：依赖 Phase 2 完成；可在 US1 完成后独立推进
- **Phase 5（US3）**：依赖 Phase 2 完成；建议在 US1 主流程跑通后推进
- **Phase 6**：依赖 US1、US2、US3 完成

### 用户故事依赖

- **US1**：基础阶段完成后即可开始，不依赖其他故事
- **US2**：依赖基础配置与存储骨架，复用基础阶段建立的 CLI/维护命令入口骨架和 US1 的程序模型
- **US3**：依赖 US1 的周期执行主流程，复用基础阶段建立的命令入口骨架，以及 US1/US2 的日志和配置能力

### 每个用户故事内部顺序

- 先完成验证任务，再进入实现任务
- 领域模型先于领域服务
- 领域服务先于应用编排
- 应用编排先于入站/出站适配器补全
- 故事完成前必须通过嵌入式 C 规范检查、超时/重试检查和中文日志检查

### 可并行机会

- Phase 1 中 T003、T004 可并行
- Phase 2 中 T006、T007、T008 可并行
- US1 中 T011、T012 可并行；T014、T015 可并行
- US2 中 T022 与 T024/T025 可部分并行；T024、T025 可并行
- US3 中 T029、T030 可并行；T032 可与 T031 的准备工作并行
- Phase 6 中 T037、T038 可并行

---

## 并行示例：用户故事 1

```bash
# 并行准备 US1 验证任务
Task: "T011 编写 tests/unit/test_wash_cycle_state_machine.c 与 tests/unit/test_precheck_rules.c"
Task: "T012 编写 tests/contract/test_start_wash_cycle_contract.c 与 tests/contract/test_cycle_finished_contract.c"

# 并行实现 US1 基础模型
Task: "T014 实现 src/domain/model/wash_program.c 与 src/domain/model/wash_stage.c"
Task: "T015 实现 src/domain/model/vehicle_type.c、src/domain/model/chemical_channel.c 与 src/domain/model/chemical_action.c"
```

## 并行示例：用户故事 2

```bash
# 并行完成配置解析与校验
Task: "T024 实现 src/adapters/config/json_program_parser.c"
Task: "T025 实现 src/domain/model/program_validation.c"
```

## 并行示例：用户故事 3

```bash
# 并行准备故障与维护验证
Task: "T029 编写 tests/unit/test_fault_policy_service.c 与 tests/unit/test_recovery_state_machine.c"
Task: "T030 编写 tests/contract/test_fault_event_contract.c 与 tests/contract/test_maintenance_command_contract.c"
```

---

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1：准备阶段
2. 完成 Phase 2：基础阶段
3. 完成 Phase 3：US1 自动洗车主流程
4. 运行 US1 的单元、契约、集成测试并按 `quickstart.md` 演示
5. 在 US1 验证通过后再进入 US2/US3

### 增量交付

1. 先交付 US1，形成可运行的洗车主闭环
2. 再交付 US2，补齐程序配置和维护参数能力
3. 最后交付 US3，补齐安全告警、维护模式和恢复闭环
4. 每完成一个故事，都更新验证证据并保持已有能力可回归

### 多人并行策略

1. 全员先完成 Phase 1 和 Phase 2
2. US1 完成并稳定后：
   - 开发者 A：推进 US2 配置与持久化
   - 开发者 B：推进 US3 安全联锁与维护流程
3. 收尾阶段共同完成性能、HIL 和证据归档

---

## 备注

- 所有公开头文件、端口接口、状态机和硬件相关接口都必须补中文 Doxygen 注释
- 任何等待、到位确认、停机确认、药剂反馈都必须绑定超时和最大重试次数
- 不得引入支付、云平台、OTA、吹干联动或多工位协同任务
- 每个任务只清理由本任务引入的未使用代码或文件
