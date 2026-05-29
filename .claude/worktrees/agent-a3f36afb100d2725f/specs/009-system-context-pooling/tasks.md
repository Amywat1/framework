# 任务：system_context 组合根生命周期收口与静态池化

**输入**：来自 `/specs/009-system-context-pooling/` 的设计文档  
**前置条件**：`plan.md`（必需）、`spec.md`（必需）、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证**：每个用户故事必须先建立可失败的验证，再推进实现；最终以 `quickstart.md` 中的聚焦 CTest 场景和完整回归作为收尾依据。

**组织方式**：任务按用户故事分组，确保每个故事都能独立实现、独立验证，并保持 `system_context_t` 只作为正式生命周期管理的组合根资源。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（如 `US1`、`US2`、`US3`、`US4`）
- 描述中必须包含准确文件路径
- 每个任务都附带 `验证：` 说明

## 路径约定

- 生产代码：`include/`、`src/`
- 测试代码：`tests/`
- 特性文档：`specs/009-system-context-pooling/`

## Phase 1：准备阶段（共享基础）

**目的**：先把构建入口和测试挂载点准备好，避免后续在改 API 的同时再追构建脚手架。

- [X] T001 更新 `tests/CMakeLists.txt`，为 `tests/contract/test_system_context_lifecycle_contract.c`、`tests/unit/test_system_context_pool.c`、`tests/integration/test_system_context_error_paths.c` 预留编译入口。验证：`tests/CMakeLists.txt` 中能看到新测试源文件或待接入占位，后续新增测试无需再改阶段结构。
- [X] T002 更新 `src/CMakeLists.txt`，确保 `src/application/coordinators/system_context_private.h` 仍保持私有边界，同时允许 `src/application/coordinators/system_context.c`、`src/platform/linux/controller_scheduler_linux.c` 和 `src/platform/linux/main_loop.c` 的生命周期改造顺利编译。验证：`src/CMakeLists.txt` 不把私有头暴露到公开 include 路径。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：建立所有用户故事共用的正式生命周期边界和内部静态池骨架。

**关键约束**：本阶段完成前，不得开始任何用户故事实现。

- [X] T003 重构 `include/application/coordinators/system_context.h`，去掉 `system_context_create()`、`system_context_initialize()`、`system_context_destroy()` 的公开语义，改为不透明类型下的正式 `acquire/reset/release` 与装配接口，并补齐中文 Doxygen。验证：`rg -n "system_context_(create|initialize|destroy)" include/application/coordinators/system_context.h` 无结果，公开头不再暴露调用方自带存储说明。
- [X] T004 重构 `src/application/coordinators/system_context_private.h`，删除 `owns_storage`，新增静态池槽位、池状态、容量常量、空闲链表哨兵值和 `in_use` 合法性校验所需的私有声明。验证：私有头包含 `system_context_pool_slot_t` / `system_context_pool_state_t` 等池化定义，且不再出现 `owns_storage`。
- [X] T005 在 `src/application/coordinators/system_context.c` 实现固定容量静态对象池初始化、空闲链表头弹出/头插回收、运行态清理和句柄合法性检查的基础骨架。验证：实现中存在静态池状态、空闲链表管理逻辑和 `in_use` 校验入口，且不再依赖 `malloc/free`。

**检查点**：正式生命周期边界与私有静态池骨架已就位，用户故事可围绕同一实现继续推进。

---

## Phase 3：用户故事 1 - 正式实例只能从统一入口获取（优先级：P1）MVP

**目标**：让主程序、调度器、适配层和测试辅助都只能通过正式生命周期入口拿到 `system_context_t *`。

**独立验证方式**：主程序、调度器、适配层和测试辅助不再使用调用方自带 `system_context_t` 存储；旧公开入口不再能作为正式路径使用。

### 用户故事 1 的验证任务

- [X] T006 [P] [US1] 更新 `tests/contract/test_system_context_boundary_contract.c` 和 `tests/integration/test_formal_entry_ownership.c`，先固定“公开边界不透明、正式实例只能 acquire”的验收场景。验证：测试先因旧入口或外部构造语义存在而失败。
- [X] T007 [P] [US1] 更新 `tests/integration/test_compat_entrypoint_removal.c` 和 `tests/contract/test_controller_scheduler_contract.c`，锁定旧 `create/initialize/destroy` 使用路径必须消失。验证：测试能明确区分新旧入口，并在旧入口仍残留时失败。

### 用户故事 1 的实现任务

- [X] T008 [US1] 重构 `tests/test_support.h`，提供统一的 acquire / release 测试辅助入口，移除调用方自带 `system_context_t` 存储初始化方式。验证：测试辅助不再调用 `system_context_initialize()`，且能为后续测试统一提供正式句柄。
- [X] T009 [US1] 修改 `src/main/main.c`，将主程序路径改为“申请句柄 -> 装配 -> 运行 -> 释放”的正式生命周期。验证：`src/main/main.c` 不再调用旧 `create/destroy` 接口，主程序持有关系与规格一致。
- [X] T010 [P] [US1] 修改 `src/platform/linux/main_loop.c` 和 `include/platform/linux/main_loop.h`，让主循环只消费正式 `system_context_t *` 句柄，并为对外公开接口补齐中文 Doxygen 注释。验证：主循环边界测试只依赖正式句柄，不再要求外部构造结构体，且公开声明具备中文 Doxygen 说明。
- [X] T011 [P] [US1] 修改 `src/platform/linux/controller_scheduler_linux.c`、`src/platform/linux/controller_scheduler_linux_internal.h`、`include/platform/linux/controller_scheduler_linux.h` 和 `include/platform/controller_scheduler.h`，收紧调度器对 `system_context` 的拥有者边界，并为公开接口补齐中文 Doxygen 注释。验证：调度器相关接口不再承担创建/销毁策略，只消费正式句柄，且公开声明具备中文 Doxygen 说明。
- [X] T012 [P] [US1] 修改 `src/adapters/inbound/cli_command_adapter.c`、`include/adapters/inbound/cli_command_adapter.h`、`src/adapters/outbound/file_program_repository.c`、`include/adapters/outbound/file_program_repository.h`、`src/adapters/logging/file_event_logger.c` 和 `include/adapters/logging/file_event_logger.h`，移除适配层对外部自带存储语义的依赖，并为公开接口补齐中文 Doxygen 注释。验证：适配层装配与查询路径只依赖正式句柄，不新增旁路入口，且公开声明具备中文 Doxygen 说明。
- [X] T013 [US1] 更新 `tests/contract/test_main_loop_boundary_contract.c`、`tests/integration/test_formal_command_scheduler_dispatch.c` 和 `tests/integration/test_scheduler_command_fd_nonblocking.c`，让路径级回归全部匹配新入口合同。验证：这些测试通过正式 acquire 入口装配和运行，不再依赖局部 `system_context_t` 存储。

**检查点**：主程序和外围集成入口已经统一迁移到正式生命周期，US1 可独立交付为 MVP。

---

## Phase 4：用户故事 2 - 重置与释放必须明确分离（优先级：P1）

**目标**：把“清空运行态重新开始”和“结束生命周期归还资源”拆成两个正式动作，并稳定保留 reset 后端口绑定。

**独立验证方式**：同一实例在 reset 后仍归当前持有者占用且可继续运行，release 后资源回池且旧句柄失效。

### 用户故事 2 的验证任务

- [X] T014 [P] [US2] 创建 `tests/contract/test_system_context_lifecycle_contract.c`，先锁定 acquire / reset / release 的正式契约、端口保留和失效句柄行为。验证：新契约测试在语义未拆分前失败。
- [X] T015 [P] [US2] 更新 `tests/integration/test_controller_runtime_persistence.c` 和 `tests/simulation/test_basic_control_loop.c`，先覆盖“reset 继续运行、release 结束占用”的独立验收路径。验证：测试能区分 reset 与 release 的不同后果。

### 用户故事 2 的实现任务

- [X] T016 [US2] 修改 `src/application/coordinators/system_context.c` 和 `include/application/coordinators/system_context.h`，把运行态重置与资源释放语义正式拆分，保证 reset 清空运行态但保留端口绑定，release 清空运行态并归还池资源。验证：实现中 reset 不触碰空闲链表，release 必须执行回收逻辑。
- [X] T017 [P] [US2] 审计并确认 `src/main/main.c`、`src/platform/linux/main_loop.c` 和 `src/platform/linux/controller_scheduler_linux.c` 的生命周期分工：当前无内部“重跑即重置”旁路，`main_loop` 不持有释放职责，`controller_scheduler_linux_destroy()` 只解绑调度器资源，`src/main/main.c` 仅在最终拥有者退出时调用 release。验证：主路径不存在调度器或主循环越级 release 的情况，正式释放仍只落在最终拥有者退出路径。
- [X] T018 [US2] 更新 `tests/contract/test_program_snapshot_contract.c`、`tests/contract/test_execution_events_contract.c`、`tests/integration/test_session_status_observability.c` 和 `tests/integration/test_status_query_no_side_effect.c`，将状态观察回归改成验证 reset 后干净重启、release 后句柄失效。验证：相关状态型测试不再把 reset 当成 destroy。

**检查点**：reset 与 release 语义已经被正式拆分，并由契约与集成路径稳定覆盖。

---

## Phase 5：用户故事 3 - 池耗尽和非法使用有稳定语义（优先级：P1）

**目标**：为池耗尽、重复释放、非法句柄和可检测的释放后误用建立统一、显式且可恢复的失败语义。

**独立验证方式**：池耗尽、重复 release、非法句柄、无效 reset 和可检测的释放后误用均返回显式错误，并且不污染其他实例或池状态。

### 用户故事 3 的验证任务

- [X] T019 [P] [US3] 创建 `tests/unit/test_system_context_pool.c`，先验证固定容量、空闲链表头弹出/头插回收、`free_count` 一致性、`in_use` 仅做合法性校验，且测试不依赖具体池容量数值。验证：白盒单测在池状态机未落地前失败，且断言只覆盖“占满后失败、释放后可再次获取”等正式语义。
- [X] T020 [P] [US3] 创建 `tests/integration/test_system_context_error_paths.c`，先覆盖池耗尽、重复 release、非法句柄、无效 reset 和可检测释放后误用的正式错误语义。验证：异常路径测试在显式错误尚未实现前失败。
- [X] T021 [P] [US3] 更新 `tests/integration/test_scheduler_failure_paths.c` 和 `tests/contract/test_event_source_dispatch_contract.c`，让外围路径先暴露资源不可用和非法句柄传播缺口。验证：调度器/事件源路径在未传播生命周期错误时失败。

### 用户故事 3 的实现任务

- [X] T022 [US3] 修改 `src/application/coordinators/system_context.c`，为池耗尽、重复 release、非法句柄、无效 reset 和可检测的释放后误用返回显式错误，并保持池状态可恢复。验证：实现中异常返回不会修改其他槽位状态，且释放后可再次 acquire。
- [X] T023 [P] [US3] 修改 `src/platform/linux/main_loop.c`、`src/platform/linux/controller_scheduler_linux.c`、`src/main/main.c`、`src/adapters/inbound/cli_command_adapter.c`、`include/platform/linux/main_loop.h` 和 `include/platform/linux/controller_scheduler_linux.h`，贯通生命周期失败结果到外围调用方。验证：外围路径能够区分资源不可用与非法句柄，不再吞掉生命周期错误。
- [X] T024 [US3] 更新 `tests/contract/test_trigger_runtime_ownership_contract.c`、`tests/integration/test_segment_exception_paths.c`、`tests/integration/test_session_exception_paths.c` 和 `tests/simulation/test_segment_exception_behavior.c`，让异常回归全部对齐正式失败语义。验证：异常回归不再依赖旧对象语义自保。

**检查点**：异常路径和池耗尽语义已经正式化，US3 可单独验证为稳定边界能力。

---

## Phase 6：用户故事 4 - 白盒验证只验证边界，不恢复旧模型（优先级：P2）

**目标**：把剩余测试全部迁到正式生命周期入口，同时保留受控白盒观察能力，不再恢复外部可构造结构体的旧模型。

**独立验证方式**：测试仍可验证池状态和边界语义，但不再存在依赖 `system_context_t local_context;` 的正式测试路径。

### 用户故事 4 的验证任务

- [X] T025 [P] [US4] 更新 `tests/unit/test_precheck_rules.c`、`tests/unit/test_program_config_validation.c`、`tests/unit/test_program_snapshot_service.c`、`tests/unit/test_recovery_state_machine.c`、`tests/unit/test_wash_session_state_machine.c`，把单元测试统一迁到正式 acquire / release 辅助入口。验证：这些单元测试通过 `test_setup_system_context()` / `test_release_system_context()` 获取并释放正式句柄，局部变量仅承载句柄值，不依赖公开结构体布局。
- [X] T026 [P] [US4] 更新 `tests/contract/test_position_exit_semantics_contract.c`、`tests/contract/test_segment_runtime_orchestration_contract.c`、`tests/contract/test_session_finalization_contract.c`、`tests/contract/test_status_query_readonly_contract.c`、`tests/contract/test_scheduler_exit_contract.c` 和 `tests/contract/test_scheduler_observability_contract.c`，让契约测试全部改为正式句柄和边界断言。验证：契约测试统一走正式句柄辅助入口，并在结束时显式释放句柄，不再把局部变量声明误当作外部自带对象存储。
- [X] T027 [P] [US4] 更新 `tests/integration/test_segment_runtime_orchestration.c` 和 `tests/integration/test_unique_session_result_sink.c`，让剩余集成路径改成正式句柄贯穿。验证：集成路径统一通过正式句柄辅助入口获取/释放上下文，不存在外部构造真实 `system_context` 对象的路径。
- [X] T028 [P] [US4] 更新 `tests/simulation/test_controller_global_fault_behavior.c`、`tests/simulation/test_fixed_period_scheduler_tick.c`、`tests/simulation/test_program_snapshot_runtime_behavior.c`、`tests/simulation/test_scheduler_backlog_behavior.c`、`tests/simulation/test_scheduler_event_priority.c` 和 `tests/simulation/test_segment_runtime_control.c`，把仿真回归迁移到正式生命周期。验证：仿真测试统一走测试辅助的 acquire / release 入口，调度器销毁后仍由测试显式释放正式句柄。

### 用户故事 4 的实现任务

- [X] T029 [US4] 调整 `tests/unit/test_system_context_pool.c`、`tests/test_support.h` 和 `src/application/coordinators/system_context_private.h`，保留受控白盒池状态观察能力，但不重新公开真实 `system_context_t` 结构布局。验证：白盒断言只能观察池/边界语义，公开头仍保持不透明。

**检查点**：测试体系已经完全对齐正式生命周期，白盒能力不再把旧存储模型拉回公开边界。

---

## Phase 7：收尾与跨故事事项

**目的**：清除残留旧语义，并按 quickstart 完成最终回归。

- [X] T030 [P] 按 `specs/009-system-context-pooling/quickstart.md` 更新并执行聚焦回归命令，确认 `tests/CMakeLists.txt` 中的生命周期相关测试都已接入并可单独运行。验证：聚焦 `ctest -R` 场景覆盖边界契约、池语义、正式入口拥有权和调度器路径。
- [X] T031 审计 `include/application/coordinators/system_context.h`、`include/platform/linux/main_loop.h`、`include/platform/linux/controller_scheduler_linux.h`、`include/platform/controller_scheduler.h`、`include/adapters/inbound/cli_command_adapter.h`、`include/adapters/outbound/file_program_repository.h`、`include/adapters/logging/file_event_logger.h`、`src/application/coordinators/system_context_private.h`、`src/application/coordinators/system_context.c`、`src/main/main.c`、`src/platform/linux/controller_scheduler_linux.c` 和 `tests/`，清除残留 `owns_storage`、`system_context_create`、`system_context_initialize`、`system_context_destroy` 与 `system_context_t local_context;` 正式路径，并确认公开头全部具备中文 Doxygen 注释、测试不依赖池容量具体数值。验证：`rg -n "owns_storage|system_context_(create|initialize|destroy)|system_context_t [A-Za-z_][A-Za-z0-9_]*;" include src tests` 仅保留受控白盒例外或返回空结果，公开头注释完整，且生命周期测试只断言正式容量语义。

---

## 依赖与执行顺序

### 阶段依赖

- **Phase 1**：无依赖，可立即开始。
- **Phase 2**：依赖 Phase 1 完成，并阻塞所有用户故事。
- **Phase 3（US1）**：依赖 Phase 2 完成，是本特性的 MVP。
- **Phase 4（US2）**：依赖 Phase 2，且最好在 US1 完成后推进，以便直接复用正式入口迁移结果。
- **Phase 5（US3）**：依赖 Phase 2，且最好在 US1、US2 后推进，以便基于最终生命周期语义固化异常路径。
- **Phase 6（US4）**：依赖 US1、US2、US3 完成后进行，避免测试迁移反复返工。
- **Phase 7**：依赖所有目标用户故事完成。

### 用户故事依赖

- **US1**：基础阶段完成后即可开始，不依赖其他故事。
- **US2**：语义上可独立验证，但实现上共享 `system_context` 核心文件，建议在 US1 迁移入口后推进。
- **US3**：语义上可独立验证，但实现上依赖正式入口和 reset/release 拆分已经落地。
- **US4**：依赖最终正式生命周期和异常语义稳定后再统一迁移测试。

### 每个用户故事内部顺序

- 先写或改验证，再做实现。
- 先修改 `system_context` 核心接口和实现，再迁移外围调用方。
- 先收紧测试辅助，再批量迁移测试文件。
- 同一故事完成并通过独立验证后，再进入下一个优先级故事。

## 并行机会

- Phase 1 中的 T001、T002 可并行。
- Phase 2 中的 T003、T004 可并行，T005 依赖它们完成。
- US1 中的 T006、T007 可并行；T010、T011、T012 可在 T008、T009 完成后分层并行。
- US2 中的 T014、T015 可并行；T017 可在 T016 完成后与测试修正并行。
- US3 中的 T019、T020、T021 可并行；T023 可在 T022 完成后与异常回归调整并行。
- US4 中的 T025、T026、T027、T028 可并行；T029 在这些迁移稳定后收口白盒边界。

## 并行示例：用户故事 1

```text
T006 [US1] 更新 tests/contract/test_system_context_boundary_contract.c 和 tests/integration/test_formal_entry_ownership.c
T007 [US1] 更新 tests/integration/test_compat_entrypoint_removal.c 和 tests/contract/test_controller_scheduler_contract.c

T010 [US1] 修改 src/platform/linux/main_loop.c 和 include/platform/linux/main_loop.h
T011 [US1] 修改 src/platform/linux/controller_scheduler_linux.c、src/platform/linux/controller_scheduler_linux_internal.h、include/platform/linux/controller_scheduler_linux.h、include/platform/controller_scheduler.h
T012 [US1] 修改 src/adapters/inbound/cli_command_adapter.c、src/adapters/outbound/file_program_repository.c、src/adapters/logging/file_event_logger.c 及对应头文件
```

## 并行示例：用户故事 2

```text
T014 [US2] 创建 tests/contract/test_system_context_lifecycle_contract.c
T015 [US2] 更新 tests/integration/test_controller_runtime_persistence.c 和 tests/simulation/test_basic_control_loop.c
```

## 并行示例：用户故事 3

```text
T019 [US3] 创建 tests/unit/test_system_context_pool.c
T020 [US3] 创建 tests/integration/test_system_context_error_paths.c
T021 [US3] 更新 tests/integration/test_scheduler_failure_paths.c 和 tests/contract/test_event_source_dispatch_contract.c
```

## 并行示例：用户故事 4

```text
T025 [US4] 更新 tests/unit/*.c 中剩余正式句柄测试，统一补齐 acquire / release 辅助入口闭环
T026 [US4] 更新 tests/contract/*.c 中剩余正式句柄测试，统一补齐边界断言与显式 release
T027 [US4] 更新 tests/integration/test_segment_runtime_orchestration.c 和 tests/integration/test_unique_session_result_sink.c
T028 [US4] 更新 tests/simulation/*.c 中剩余正式句柄测试，统一补齐 acquire / release 辅助入口闭环
```

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1 和 Phase 2。
2. 完成 Phase 3（US1），统一主程序、调度器、适配层和测试辅助的正式入口。
3. 运行 US1 的契约与集成验证。
4. 在 US1 稳定后再继续 reset/release 语义与异常路径。

### 增量交付

1. 先交付 US1，建立唯一正式入口。
2. 再交付 US2，稳定 reset / release 分离。
3. 再交付 US3，补齐池耗尽和非法使用语义。
4. 最后交付 US4，完成测试体系收口与白盒边界修正。

### 多人并行策略

1. 一人先完成 Phase 1、Phase 2。
2. 之后可按层并行：
   A. 一人负责 `src/main/` 与 `src/platform/linux/` 的正式入口迁移。
   B. 一人负责 `src/adapters/` 与 `tests/test_support.h` 的入口迁移。
   C. 一人负责新增契约/单元/集成测试文件与现有测试回归迁移。

## 备注

- 总任务数：31
- 用户故事任务数：US1 `8`，US2 `5`，US3 `6`，US4 `5`
- 准备/基础/收尾任务数：`7`
- 所有任务均符合 `- [ ] T### [P?] [US?] 描述` 清单格式
