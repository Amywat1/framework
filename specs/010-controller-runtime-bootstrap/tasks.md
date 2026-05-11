# 任务：主控 Runtime/Bootstrap 正式入口

**输入**：来自 `/specs/010-controller-runtime-bootstrap/` 的设计文档  
**前置条件**：`plan.md`（必需）、`spec.md`（必需，用于用户故事）、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证**：每个用户故事都必须先建立契约测试、集成测试或人工检查方式，再推进实现；最终以 `quickstart.md` 中的运行闭环和 `ctest` 回归作为收尾依据。

**组织方式**：任务按用户故事分组，确保每个故事都能独立实现、独立验证，并保持 runtime/bootstrap 边界只围绕正式主控实例生命周期展开。

## 格式：`[ID] [P?] [Story] 描述`

- **[P]**：可并行执行（文件不同、无依赖冲突）
- **[Story]**：任务所属用户故事（如 `US1`、`US2`、`US3`）
- 描述中必须包含准确文件路径
- 每个任务都附带 `验证：`

## 路径约定

- 生产代码：`include/`、`src/`
- 测试代码：`tests/`
- 特性文档：`specs/010-controller-runtime-bootstrap/`

## Phase 1：准备阶段（共享基础）

**目的**：先把构建入口和测试注册点准备好，避免后续在改生命周期时反复补脚手架。

- [X] T001 更新 `src/CMakeLists.txt` 和 `tests/CMakeLists.txt`，为 `include/application/coordinators/controller_runtime.h`、`src/application/coordinators/controller_runtime.c` 以及本特性的新增 runtime 契约/集成测试预留编译入口。验证：`cmake -S . -B build` 不会因新增 runtime 源文件或测试目标缺失而失败。
- [ ] T002 [P] 在 `specs/010-controller-runtime-bootstrap/quickstart.md`、`specs/010-controller-runtime-bootstrap/contracts/runtime-lifecycle-contract.md`、`specs/010-controller-runtime-bootstrap/contracts/startup-rollback-contract.md`、`specs/010-controller-runtime-bootstrap/contracts/shared-bootstrap-contract.md` 标注即将实现的公开 API 名称与测试目标命名约定。验证：文档中的 API 名称、状态名和测试命名约定在实现前已统一，不再保留含糊描述。

---

## Phase 2：基础阶段（阻塞前置）

**目的**：建立所有故事共享的 runtime 公开边界、配置注入面、最小状态模型和适配器错误传播基础。

**关键约束**：本阶段完成前，不得开始任何用户故事实现。

- [X] T003 在 `include/application/coordinators/controller_runtime.h` 定义 `controller_runtime` 公开类型、`controller_runtime_config_t`、最小状态观测结构、配置校验入口和 `create/run/destroy` 声明，并补齐中文 Doxygen。验证：公开头已包含正式配置注入面，且只暴露运行实例语义，不直接暴露 Linux 平台管理细节，也不把 `system_context_t` 扩大成新的管理面。
- [X] T004 [P] 在 `src/application/coordinators/controller_runtime.c` 建立 runtime 私有状态骨架，包含 runtime owned 资源跟踪、caller owned 绑定记录、配置归一化/校验辅助函数、逆序清理辅助函数和终态状态位。验证：实现文件存在可复用的配置校验与回滚/销毁骨架，且资源所有权边界可从代码结构上读出。
- [X] T005 [P] 修改 `include/adapters/outbound/file_program_repository.h`、`src/adapters/outbound/file_program_repository.c`、`include/adapters/logging/file_event_logger.h`、`src/adapters/logging/file_event_logger.c`，把初始化入口统一改为返回 `operation_result_t`。验证：文件仓储与事件日志初始化失败可以被调用方显式识别，现有调用点不再只能依赖隐式状态或日志。

**检查点**：runtime 公共 API 骨架、正式配置注入面、内部资源跟踪骨架和适配器错误返回链路已经到位，用户故事可以围绕同一生命周期闭环推进。

---

## Phase 3：用户故事 1 - 统一创建并运行主控实例（优先级：P1）MVP

**目标**：提供唯一正式 runtime 入口，让主程序通过 `create/run/destroy` 完成启动、运行和销毁，不再手工拼装 `system_context`、适配器和 scheduler。

**独立验证方式**：通过 runtime 正式入口成功创建实例并进入运行；创建成功时已处于“已创建未运行”，`run` 不再补做装配；`run` 返回后实例进入终态且不能再次运行；正常退出返回成功。

### 用户故事 1 的验证任务

- [X] T006 [P] [US1] 新建 `tests/contract/test_controller_runtime_lifecycle_contract.c`，先固定 `create -> CREATED -> run -> TERMINATED -> destroy` 的正式生命周期契约。验证：测试先失败，直到 `create`、`run`、`destroy` 的状态转换、一次性 `run` 语义和正常退出成功语义全部成立。
- [X] T007 [P] [US1] 更新 `tests/integration/test_controller_runtime_persistence.c`，覆盖“创建成功时全部依赖就绪”“`run` 返回后不得再次运行”“正常退出返回成功”的集成场景。验证：测试先失败，直到实例创建闭环和终态行为与 `spec.md` 的 V-001、V-006、V-007 一致。

### 用户故事 1 的实现任务

- [X] T008 [US1] 在 `include/application/coordinators/controller_runtime.h` 和 `src/application/coordinators/controller_runtime.c` 基于正式 `controller_runtime_config_t` 实现 `controller_runtime_create(...)` 成功路径，统一完成 `system_context` 获取、端口绑定、程序仓储初始化、事件日志初始化和 scheduler 创建。验证：`test_controller_runtime_lifecycle_contract` 和 `test_controller_runtime_persistence` 能观察到创建成功即进入“已创建未运行”状态，且 create 所需输入全部来自正式配置注入面。
- [X] T009 [US1] 在 `include/application/coordinators/controller_runtime.h` 和 `src/application/coordinators/controller_runtime.c` 实现 `controller_runtime_run(...)` 与最小只读状态观测接口，保证 `run` 只负责进入运行、正常退出返回成功、返回后进入不可再次运行的终态。验证：`test_controller_runtime_lifecycle_contract` 能区分正常退出成功与重复运行失败。
- [X] T010 [US1] 在 `src/main/main.c` 迁移主程序启动路径，只通过 `controller_runtime_create/run/destroy` 启动主控，移除手工 `system_context_acquire()`、`file_*_init()` 和 `controller_scheduler_linux_create()` 主路径。验证：`rg -n "system_context_acquire|file_program_repository_init|file_event_logger_init|controller_scheduler_linux_create" src/main/main.c` 仅保留 runtime 内部所需路径或返回空结果，且 `test_controller_runtime_persistence` 仍通过。
- [ ] T011 [US1] 更新 `tests/unit/test_main.c` 和 `tests/CMakeLists.txt`，让主程序相关测试与构建目标匹配新的 runtime-only 启动路径。验证：主程序测试不再依赖旧的手工装配流程，测试目标命名与构建入口一致。

**检查点**：到此为止，主程序已经只能通过正式 runtime 入口启动，US1 可独立交付为 MVP。

---

## Phase 4：用户故事 2 - 启动失败显式返回并完整回滚（优先级：P1）

**目标**：把缺失配置、路径错误、端口缺失、适配器初始化失败、scheduler 创建失败、单实例冲突和重复运行全部收成显式失败，并统一由 runtime 执行逆序回滚或安全拒绝。

**独立验证方式**：制造不同创建失败场景后，runtime 返回明确失败且不残留半初始化对象；已有正式实例存活时第二次创建显式失败且不破坏现有实例；`destroy` 可安全处理空对象、部分创建成功对象和已销毁对象。

### 用户故事 2 的验证任务

- [X] T012 [P] [US2] 新建 `tests/contract/test_controller_runtime_startup_rollback_contract.c`，先固定创建失败回滚、空对象销毁、部分创建成功销毁和已销毁对象重复销毁的正式契约。验证：测试先失败，直到 runtime 失败路径统一回滚且 `destroy` 对中间态安全。
- [X] T013 [P] [US2] 新建 `tests/integration/test_controller_runtime_startup_failures.c`，覆盖缺失绑定、坏路径、仓储初始化失败、日志初始化失败、scheduler 创建失败后的显式失败与立即重建。验证：测试先失败，直到失败后能立即再次成功创建新实例。
- [X] T014 [P] [US2] 新建 `tests/integration/test_controller_runtime_single_instance.c`，覆盖“同一进程仅 1 个正式 runtime”“第二次创建失败且不影响现有实例”“`run` 返回前后单实例约束保持一致”的集成场景。验证：测试先失败，直到单实例约束和不破坏现有实例的要求成立。

### 用户故事 2 的实现任务

- [X] T015 [US2] 在 `src/application/coordinators/controller_runtime.c` 落实统一逆序回滚链路，把 `system_context`、scheduler 和已装配资源按创建逆序清理，并让 `destroy` 复用这套清理逻辑。验证：`test_controller_runtime_startup_rollback_contract` 和 `test_controller_runtime_startup_failures` 通过，失败点不再各写一套分支清理。
- [X] T016 [US2] 在 `src/application/coordinators/controller_runtime.c` 实现单正式实例约束、重复 `run` 拒绝和 `destroy` 的空对象/已销毁安全处理。验证：`test_controller_runtime_single_instance` 能证明同一进程不会并存多个正式 runtime，`test_controller_runtime_lifecycle_contract` 能证明终态实例不能再次 `run`。
- [ ] T017 [P] [US2] 更新 `src/platform/linux/controller_scheduler_linux.c`、`include/platform/linux/controller_scheduler_linux.h`、`include/platform/controller_scheduler.h`，确保 runtime 销毁只清理 runtime owned 的 scheduler 资源，且 post-failure/post-destroy 观测路径不会要求调用方手工补清理。验证：startup rollback 和 scheduler 相关契约测试在失败后仍能安全读取有限状态或收到明确失败。
- [ ] T018 [P] [US2] 更新 `tests/integration/test_system_context_error_paths.c`、`tests/integration/test_scheduler_failure_paths.c`、`tests/integration/test_scheduler_command_fd_nonblocking.c` 和 `tests/contract/test_controller_scheduler_contract.c`，让现有错误路径测试适配新的 `operation_result_t` 初始化失败链路与 runtime 回滚语义。验证：现有错误路径测试不再依赖旧的 `void` 初始化或手工清理模型。

**检查点**：到此为止，启动失败、单实例冲突、重复运行和销毁中间态都已有正式失败语义，US2 可独立验证。

---

## Phase 5：用户故事 3 - 测试与正式入口共用同一套装配闭环（优先级：P2）

**目标**：让测试辅助与正式入口复用同一套 runtime/bootstrap 流程，只通过注入替换驱动、路径、stdio、调度参数和测试桩，并显式清理仓库内残留的测试侧直连 bootstrap 路径。

**独立验证方式**：测试侧不再把 `system_context`、文件适配器初始化和 scheduler 创建拼成第二套主路径；正式入口和测试入口的差异只剩注入参数；仓库内剩余测试文件都经过显式审计，不再隐含保留旁路 bootstrap。

### 用户故事 3 的验证任务

- [ ] T019 [P] [US3] 新建 `tests/contract/test_controller_runtime_shared_bootstrap_contract.c`，先固定“正式入口与测试入口必须复用同一套 create/destroy 闭环”的契约。验证：测试先失败，直到测试辅助不再作为独立装配主路径存在。
- [ ] T020 [P] [US3] 更新 `tests/integration/test_formal_entry_ownership.c` 和 `tests/integration/test_formal_command_scheduler_dispatch.c`，验证正式入口与测试入口只通过配置注入区分，命令路径和调度路径不再各自复制 bootstrap。验证：测试先失败，直到测试辅助与正式入口共享同一套 runtime 配置面。

### 用户故事 3 的实现任务

- [ ] T021 [US3] 在 `include/application/coordinators/controller_runtime.h` 和 `src/application/coordinators/controller_runtime.c` 补齐测试复用所需的最小配置构造/默认装配辅助能力，但不再改变已在基础阶段定义完成的正式配置注入面。验证：`test_controller_runtime_shared_bootstrap_contract` 能证明 main 与 tests 使用同一套 create/destroy 表达面，且不会在 US3 再次改写 runtime 核心输入模型。
- [ ] T022 [US3] 重构 `tests/test_support.h`，把测试辅助改成“组装 `controller_runtime_config_t` -> 调用 `controller_runtime_create(...)` / `controller_runtime_destroy(...)`”的正式闭环，不再直接调用 `file_program_repository_init()`、`file_event_logger_init()` 或 `controller_scheduler_linux_create()` 作为主路径。验证：`rg -n "file_program_repository_init|file_event_logger_init|controller_scheduler_linux_create" tests/test_support.h` 仅保留必要兼容辅助或返回空结果。
- [ ] T023 [P] [US3] 更新 `tests/contract/test_main_loop_boundary_contract.c`、`tests/contract/test_event_source_dispatch_contract.c`、`tests/contract/test_scheduler_exit_contract.c`、`tests/contract/test_scheduler_observability_contract.c` 和 `tests/integration/test_controller_runtime_persistence.c`，让剩余 bootstrap 邻近测试统一走正式 runtime 辅助入口。验证：这些测试不再各自拼装 `system_context` 和 scheduler，也不会形成第二套正式装配路径。
- [ ] T024 [US3] 审计并迁移 `tests/contract/`、`tests/integration/`、`tests/simulation/`、`tests/unit/` 中其余仍可能直连 `system_context`、`file_*_init()` 或 `controller_scheduler_linux_create()` 的测试路径，必要时在 `tests/test_support.h` 增补统一辅助入口或记录允许保留的最小白名单。验证：仓库范围 `rg -n "file_program_repository_init|file_event_logger_init|controller_scheduler_linux_create|system_context_acquire" tests` 的结果要么转为通过正式 runtime 辅助入口，要么属于已记录的受控例外。

**检查点**：到此为止，正式入口和测试入口已经收口到同一套 runtime/bootstrap 闭环，US3 可独立验收。

---

## Phase 6：收尾与跨故事事项

**目的**：同步最终文档、清理残留旧语义并完成聚焦回归。

- [ ] T025 [P] 更新 `specs/010-controller-runtime-bootstrap/quickstart.md`、`specs/010-controller-runtime-bootstrap/contracts/runtime-lifecycle-contract.md`、`specs/010-controller-runtime-bootstrap/contracts/startup-rollback-contract.md`、`specs/010-controller-runtime-bootstrap/contracts/shared-bootstrap-contract.md`，回写实际落地的 API、状态名、失败语义和 `ctest -R` 示例。验证：文档中的 API、状态和测试名都能在实现与构建配置中找到对应实体。
- [ ] T026 在 `include/application/coordinators/controller_runtime.h`、`src/application/coordinators/controller_runtime.c`、`include/adapters/outbound/file_program_repository.h`、`include/adapters/logging/file_event_logger.h`、`src/main/main.c`、`tests/test_support.h` 做最终边界审计，清除残留第二主路径和不一致的 ownership/终态表述，并补齐中文 Doxygen。验证：公开头文件均带中文 Doxygen，`main.c` 与 `tests/test_support.h` 不再各自维护独立 bootstrap 语义。
- [ ] T027 依据 `specs/010-controller-runtime-bootstrap/quickstart.md` 运行聚焦回归，重点覆盖 `tests/contract/test_controller_runtime_lifecycle_contract.c`、`tests/contract/test_controller_runtime_startup_rollback_contract.c`、`tests/contract/test_controller_runtime_shared_bootstrap_contract.c`、`tests/integration/test_controller_runtime_startup_failures.c`、`tests/integration/test_controller_runtime_single_instance.c` 以及受影响的 scheduler/main_loop 现有回归。验证：`quickstart.md` 中列出的 `ctest` 过滤表达式都能命中实际目标，且 SC-001 至 SC-007 有对应验证证据。

---

## 依赖与执行顺序

### 阶段依赖

- **Phase 1**：无依赖，可立即开始。
- **Phase 2**：依赖 Phase 1 完成，并阻塞所有用户故事。
- **Phase 3（US1）**：依赖 Phase 2 完成，是本特性的 MVP。
- **Phase 4（US2）**：依赖 US1 完成后的 runtime 骨架和正式入口。
- **Phase 5（US3）**：依赖 US1、US2 完成后的稳定 runtime 配置面与失败语义。
- **Phase 6**：依赖所有目标用户故事完成。

### 用户故事依赖

- **US1（P1）**：基础阶段完成后即可开始，不依赖其他故事。
- **US2（P1）**：依赖 US1 的 runtime 入口和状态骨架，因为失败回滚、单实例和终态语义都建立在正式 runtime 之上。
- **US3（P2）**：依赖 US1、US2，因为测试与正式入口复用必须建立在最终 create/destroy 语义和错误传播链已经稳定之后。

### 每个用户故事内部顺序

- 先定义验证，再开始实现。
- 先收口公开头和契约，再改实现。
- 先稳定 runtime 核心语义，再迁移主程序和测试辅助。
- 当前故事完成并通过独立验证后，再进入下一个优先级故事。

## 并行机会

- Phase 1 中的 T001、T002 可并行。
- Phase 2 中的 T004、T005 可并行，T003 先定义公开边界后再汇总。
- US1 中的 T006、T007 可并行；T008 完成后，T009 与 T011 可并行，T010 在 T008/T009 后推进。
- US2 中的 T012、T013、T014 可并行；T017、T018 可在 T015/T016 基础上分层并行。
- US3 中的 T019、T020 可并行；T022 与 T023 可在 T021 后并行推进；T024 在 T022、T023 后做仓库范围审计更稳妥。
- Phase 6 中的 T025、T026 可并行，T027 最后执行。

## 并行示例：用户故事 1

```text
T006 [US1] 新建 tests/contract/test_controller_runtime_lifecycle_contract.c
T007 [US1] 更新 tests/integration/test_controller_runtime_persistence.c

T009 [US1] 在 include/application/coordinators/controller_runtime.h 和 src/application/coordinators/controller_runtime.c 实现 controller_runtime_run(...)
T011 [US1] 更新 tests/unit/test_main.c 和 tests/CMakeLists.txt
```

## 并行示例：用户故事 2

```text
T012 [US2] 新建 tests/contract/test_controller_runtime_startup_rollback_contract.c
T013 [US2] 新建 tests/integration/test_controller_runtime_startup_failures.c
T014 [US2] 新建 tests/integration/test_controller_runtime_single_instance.c
```

## 并行示例：用户故事 3

```text
T019 [US3] 新建 tests/contract/test_controller_runtime_shared_bootstrap_contract.c
T020 [US3] 更新 tests/integration/test_formal_entry_ownership.c 和 tests/integration/test_formal_command_scheduler_dispatch.c

T022 [US3] 重构 tests/test_support.h
T023 [US3] 更新 tests/contract/test_main_loop_boundary_contract.c、tests/contract/test_event_source_dispatch_contract.c、tests/contract/test_scheduler_exit_contract.c、tests/contract/test_scheduler_observability_contract.c 和 tests/integration/test_controller_runtime_persistence.c
T024 [US3] 审计 tests/contract/、tests/integration/、tests/simulation/、tests/unit/ 中剩余直连 bootstrap 路径
```

## 实施策略

### 先做 MVP（只交付用户故事 1）

1. 完成 Phase 1 和 Phase 2。
2. 完成 Phase 3（US1），建立唯一正式 runtime 入口。
3. 运行 US1 的契约和集成测试。
4. 若通过，则主控正式启动路径可以先独立验收。

### 增量交付

1. 先交付 US1，建立正式 `create/run/destroy` 入口。
2. 再交付 US2，收紧失败回滚、单实例和终态失败语义。
3. 最后交付 US3，完成测试与正式入口的 bootstrap 收口。

### 多人并行策略

1. 一人先完成 Phase 1、Phase 2。
2. 之后可按层并行：
   A. 一人负责 `controller_runtime` 核心实现与主程序迁移。
   B. 一人负责适配器错误返回与 scheduler 清理边界。
   C. 一人负责 runtime 契约/集成测试和 `tests/test_support.h` 收口。

## 备注

- 总任务数：27
- 用户故事任务数：US1 `6`，US2 `7`，US3 `6`
- 准备/基础/收尾任务数：`8`
- 所有任务均符合 `- [ ] T### [P?] [US?] 描述` 清单格式
