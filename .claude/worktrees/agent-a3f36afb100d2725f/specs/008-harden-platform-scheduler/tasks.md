# 任务：平台层调度器收敛

**输入**：来自 `specs/008-harden-platform-scheduler/` 的设计文档  
**前置条件**：`plan.md`、`spec.md`、`research.md`、`data-model.md`、`contracts/`、`quickstart.md`

**验证**：每个用户故事都必须先定义调度语义验证，再进入实现；所有验收都围绕“单线程领域推进、平台层调度边界、事件源分类、退出语义、调度观测”展开。  
**组织方式**：任务按用户故事分组，保证每个故事都能独立实现、独立测试、独立验收。

## 格式：`[ID] [P?] [Story] 描述`

- `[P]`：可并行执行
- `[Story]`：仅用户故事阶段使用，格式为 `[US1]`、`[US2]`、`[US3]`、`[US4]`
- 每个任务描述必须包含精确文件路径

## Phase 1：准备阶段（共享基础）

**目的**：为调度器抽象、Linux 实现和新增测试目标准备构建入口与基础测试辅助。

- [X] T001 在 `src/CMakeLists.txt`、`tests/CMakeLists.txt` 中注册 `controller_scheduler` 相关源文件和新增 contract/integration/simulation 测试目标
- [X] T002 [P] 在 `tests/test_support.h` 中补齐调度器驱动、周期推进、事件注入和观测断言的测试辅助入口

---

## Phase 2：基础阶段（阻塞前置）

**目的**：先建立所有用户故事共享的调度器骨架、私有运行时容器和测试支撑点。  
**关键约束**：本阶段完成前，不开始任何用户故事实现。

- [X] T003 在 `include/platform/controller_scheduler.h`、`include/platform/linux/controller_scheduler_linux.h` 中定义 `controller_scheduler` 抽象、配置结构、运行状态和只读观测视图
- [X] T004 [P] 在 `src/platform/linux/controller_scheduler_linux.c`、`src/platform/linux/controller_scheduler_linux_internal.h` 中创建 Linux 调度器私有运行时、事件源注册表和退出状态骨架
- [X] T005 [P] 在 `tests/test_support.h`、`src/platform/linux/controller_scheduler_linux.c` 中补齐测试模式下的周期源、命令源、通知源和退出源注入挂钩

**检查点**：调度器公开边界、Linux 私有容器和测试注入点已存在，后续用户故事可以围绕同一骨架增量实现。

---

## Phase 3：用户故事 1 - 调度器成为唯一运行壳（优先级：P1）

**目标**：把等待、唤醒和周期推进从 `main.c` 收进平台调度器，保留 `main_loop_*()` 的单拍语义。  
**独立验证方式**：无外部输入时，固定周期仍能驱动 `main_loop_run()`；`main.c` 只保留组合根职责。

### 用户故事 1 的验证任务

- [X] T006 [P] [US1] 在 `tests/contract/test_controller_scheduler_contract.c`、`tests/CMakeLists.txt` 中编写调度器公开边界契约测试
- [X] T007 [P] [US1] 在 `tests/contract/test_main_loop_boundary_contract.c`、`tests/CMakeLists.txt` 中编写 `main.c` 与 `main_loop_*()` 责任分离契约测试
- [X] T008 [P] [US1] 在 `tests/simulation/test_fixed_period_scheduler_tick.c`、`tests/CMakeLists.txt` 中编写固定周期稳定驱动单拍推进的仿真测试

### 用户故事 1 的实现任务

- [X] T009 [US1] 在 `src/platform/linux/controller_scheduler_linux.c` 中实现基于 `epoll + timerfd + eventfd + CLOCK_MONOTONIC` 的周期等待循环、通知唤醒接线与调度主循环
- [X] T010 [US1] 在 `src/main/main.c`、`include/platform/linux/controller_scheduler_linux.h` 中把主程序收敛为组合根，并改为构造配置后启动 `controller_scheduler`
- [X] T011 [US1] 在 `include/platform/linux/main_loop.h`、`src/platform/linux/main_loop.c` 中收紧 `main_loop_advance_time()` 与 `main_loop_run()` 的单拍边界，移除等待职责表述

**检查点**：到此为止，控制器已由平台调度器统一驱动，`main.c` 不再包含 `select`、外层循环或无限 `drain_runtime()`。

---

## Phase 4：用户故事 2 - 外部事件源被分类并隔离（优先级：P1）

**目标**：将命令、通知、退出三类事件源纳入统一分类模型，保证通知路径不直接修改领域状态，状态查询保持只读。  
**独立验证方式**：命令事件仍能启动洗车；通知事件只更新缓存/唤醒；`status` 查询无副作用；多事件顺序一致。

### 用户故事 2 的验证任务

- [X] T012 [P] [US2] 在 `tests/contract/test_event_source_dispatch_contract.c`、`tests/CMakeLists.txt` 中编写命令/通知/退出三类事件源的分发契约测试
- [X] T013 [P] [US2] 在 `tests/integration/test_formal_command_scheduler_dispatch.c`、`tests/CMakeLists.txt` 中编写正式命令经调度器入队和补跑的集成测试
- [ ] T014 [P] [US2] 在 `tests/integration/test_status_query_no_side_effect.c`、`tests/CMakeLists.txt` 中扩展状态查询保持只读的集成验证
- [X] T015 [P] [US2] 在 `tests/simulation/test_scheduler_event_priority.c`、`tests/CMakeLists.txt` 中编写命令/通知/退出同时到来时固定优先级与顺序 100% 一致的仿真测试

### 用户故事 2 的实现任务

- [X] T016 [US2] 在 `include/platform/controller_scheduler.h`、`src/platform/linux/controller_scheduler_linux.c` 中实现命令事件源、通知事件源、退出事件源三类描述符，以及“退出优先于高优先级命令补跑、命令补跑优先于通知唤醒、通知唤醒优先于常规周期推进”的固定分发策略
- [X] T017 [US2] 在 `src/adapters/inbound/cli_command_adapter.c`、`src/application/use_cases/process_formal_command.c` 中把 CLI 输入改为经调度器命令事件入队，而不是直接拥有推进循环
- [ ] T018 [US2] 在 `include/application/coordinators/system_context.h`、`src/platform/linux/controller_scheduler_linux.c` 中接入通知事件的快照更新与 `eventfd` 唤醒语义，禁止通知路径直接改写领域状态
- [X] T019 [US2] 在 `src/application/use_cases/query_wash_session_status.c`、`include/application/use_cases/query_wash_session_status.h` 中保持状态查询为只读投影，不暴露调度器内部可变句柄

**检查点**：到此为止，外部事件源已经分类收口，命令事件受控入队，通知事件只影响缓存和唤醒，退出事件进入正式退出路径。

---

## Phase 5：用户故事 3 - 退出与失败语义有界且可解释（优先级：P1）

**目标**：定义调度器的退出状态机、失败语义和有界排空策略，替代现有无限排空式运行方式。  
**独立验证方式**：退出、输入失败、周期源失败、触发积压和单拍失败都得到单一明确的结论，不出现无限排空。

### 用户故事 3 的验证任务

- [X] T020 [P] [US3] 在 `tests/contract/test_scheduler_exit_contract.c`、`tests/CMakeLists.txt` 中根据 `specs/008-harden-platform-scheduler/contracts/scheduler-exit-contract.md` 编写有界排空和退出状态迁移契约测试
- [ ] T021 [P] [US3] 在 `tests/integration/test_scheduler_failure_paths.c`、`tests/CMakeLists.txt` 中编写周期源失效、输入失败和单拍失败均产生单一明确结论且无无限排空的集成测试
- [X] T022 [P] [US3] 在 `tests/simulation/test_scheduler_backlog_behavior.c`、`tests/CMakeLists.txt` 中编写触发积压和单拍工作量上界的仿真测试

### 用户故事 3 的实现任务

- [X] T023 [US3] 在 `src/platform/linux/controller_scheduler_linux.c`、`include/platform/controller_scheduler.h` 中实现 `running / draining / stopped / failed` 运行状态、退出标志和有界排空计数
- [ ] T024 [US3] 在 `src/platform/linux/controller_scheduler_linux.c`、`src/application/use_cases/process_formal_command.c` 中实现周期源失效、事件源关闭、`eventfd` 创建/注册/读写失败、读取失败、队列满和单拍推进失败的正式错误语义
- [X] T025 [US3] 在 `src/application/use_cases/process_formal_command.c`、`src/main/main.c` 中移除命令路径自带的持续 `main_loop_run()`/`drain_runtime()` 消费逻辑，把补跑和排空决策彻底收回调度器

**检查点**：到此为止，退出和失败语义已经正式化，单拍工作量有上界，steady-state 不再依赖无限排空。

---

## Phase 6：用户故事 4 - 调度行为可观测（优先级：P2）

**目标**：扩展调度器指标与日志观测，使周期次数、超拍、事件源计数和最近错误原因都可读。  
**独立验证方式**：运行多个周期并注入混合事件后，可稳定读取指标和最近错误原因，不破坏只读查询语义。

### 用户故事 4 的验证任务

- [X] T026 [P] [US4] 在 `tests/contract/test_scheduler_observability_contract.c`、`tests/CMakeLists.txt` 中根据 `specs/008-harden-platform-scheduler/contracts/scheduler-observability-contract.md` 编写调度器指标和状态视图契约测试
- [X] T027 [P] [US4] 在 `tests/integration/test_session_status_observability.c`、`tests/CMakeLists.txt` 中扩展异常与超拍场景下观测输出 100% 包含必需指标与最近错误原因的集成验证

### 用户故事 4 的实现任务

- [X] T028 [US4] 在 `include/platform/controller_scheduler.h`、`src/platform/linux/controller_scheduler_linux.c` 中实现周期次数、超拍次数、连续超拍次数、事件源计数、挂起触发数和最近错误原因的指标采集
- [X] T029 [US4] 在 `src/adapters/logging/file_event_logger.c`、`src/platform/linux/controller_scheduler_linux.c` 中把调度器节拍与事件观测接入现有日志体系
- [X] T030 [US4] 在 `include/application/use_cases/query_wash_session_status.h`、`src/application/use_cases/query_wash_session_status.c` 中扩展只读状态视图以暴露调度器观测结果

**检查点**：到此为止，调度器行为已可观测，且观测输出不反向改变领域状态或调度状态。

---

## Phase 7：收尾与跨故事事项

**目的**：统一收尾构建、清理旧逻辑、刷新文档并完成全量验证。

- [ ] T031 [P] 在 `include/platform/controller_scheduler.h`、`include/platform/linux/controller_scheduler_linux.h`、`include/platform/linux/main_loop.h` 中补齐中文 Doxygen 注释并复核公开边界说明
- [X] T032 在 `src/main/main.c`、`src/platform/linux/main_loop.c`、`src/application/use_cases/process_formal_command.c` 中清理遗留的 `select`、`wait_timeout`、无限 `drain_runtime()` 和无用包含
- [ ] T033 在 `specs/008-harden-platform-scheduler/quickstart.md`、`tests/CMakeLists.txt` 中同步最终验证命令并对齐新增测试目标名称
- [X] T034 在 `build/` 对应构建输出执行完整 `ctest --output-on-failure` 回归，并修复 `src/CMakeLists.txt`、`tests/CMakeLists.txt` 中暴露的遗漏注册问题

---

## 依赖与执行顺序

### 阶段依赖

- Phase 1 无依赖，可立即开始。
- Phase 2 依赖 Phase 1 完成，并阻塞所有用户故事。
- Phase 3 依赖 Phase 2 完成，是 MVP 的首个交付增量。
- Phase 4 依赖 Phase 3 完成，因为事件源分类要建立在调度器已成为唯一运行壳的基础上。
- Phase 5 依赖 Phase 4 完成，因为退出与失败语义必须建立在统一事件分发和补跑归属已经收口之后。
- Phase 6 依赖 Phase 5 完成，因为观测指标需要覆盖正式退出和失败路径。
- Phase 7 依赖所有用户故事完成。

### 用户故事依赖

- US1：基础完成后即可开始，是建议 MVP。
- US2：依赖 US1 的调度器主循环和组合根收敛完成。
- US3：依赖 US2 的事件源分发和命令补跑收口完成。
- US4：依赖 US1-3 的完整调度语义，以便观测覆盖真实周期、事件和退出路径。

### 每个用户故事内部顺序

- 先写 contract/integration/simulation 验证任务，再做实现任务。
- 先收敛平台或应用边界，再修改调用路径。
- 先让测试暴露边界回退，再补实现使其通过。
- 每个故事完成后，先运行对应最小 `ctest -R` 集合，再进入下一个故事。

### 并行机会

- T002 与 T001 可并行。
- T004 与 T005 可并行。
- US1 的 T006/T007/T008 可并行准备。
- US2 的 T012/T013/T014/T015 可并行准备。
- US3 的 T020/T021/T022 可并行准备。
- US4 的 T026/T027 可并行准备。
- Phase 7 中 T031 与 T033 可并行。

---

## 并行执行示例

### 用户故事 1

```bash
Task: "在 tests/contract/test_controller_scheduler_contract.c、tests/CMakeLists.txt 中编写调度器公开边界契约测试"
Task: "在 tests/contract/test_main_loop_boundary_contract.c、tests/CMakeLists.txt 中编写 main.c 与 main_loop_*() 责任分离契约测试"
Task: "在 tests/simulation/test_fixed_period_scheduler_tick.c、tests/CMakeLists.txt 中编写固定周期稳定驱动单拍推进的仿真测试"
```

### 用户故事 2

```bash
Task: "在 tests/contract/test_event_source_dispatch_contract.c、tests/CMakeLists.txt 中编写命令/通知/退出三类事件源的分发契约测试"
Task: "在 tests/integration/test_formal_command_scheduler_dispatch.c、tests/CMakeLists.txt 中编写正式命令经调度器入队和补跑的集成测试"
Task: "在 tests/simulation/test_scheduler_event_priority.c、tests/CMakeLists.txt 中编写命令/通知/退出同时到来时的优先级与顺序仿真测试"
```

### 用户故事 3

```bash
Task: "在 tests/contract/test_scheduler_exit_contract.c、tests/CMakeLists.txt 中编写有界排空和退出状态迁移契约测试"
Task: "在 tests/integration/test_scheduler_failure_paths.c、tests/CMakeLists.txt 中编写周期源失效、输入失败和单拍失败的集成测试"
Task: "在 tests/simulation/test_scheduler_backlog_behavior.c、tests/CMakeLists.txt 中编写触发积压和单拍工作量上界的仿真测试"
```

### 用户故事 4

```bash
Task: "在 tests/contract/test_scheduler_observability_contract.c、tests/CMakeLists.txt 中编写调度器指标和状态视图契约测试"
Task: "在 tests/integration/test_session_status_observability.c、tests/CMakeLists.txt 中扩展节拍、事件计数和最近错误原因的集成验证"
```

---

## 实施策略

### MVP 优先

1. 完成 Phase 1-2。
2. 只交付 US1，让平台调度器成为唯一运行壳。
3. 用 US1 的 contract + simulation 测试验证 `main.c` 已收敛、固定周期已接管主节拍。

### 增量交付

1. 在 US1 之后交付 US2，完成事件源分类与只读查询边界。
2. 再交付 US3，完成退出、失败语义和有界排空。
3. 最后交付 US4，补齐观测指标与日志接入。

### 团队并行策略

1. 一名开发者推进 Phase 1-2 的调度器骨架。
2. 一名开发者在 US1 骨架稳定后并行准备 US2 的事件分发测试。
3. 一名开发者在 US2 进入实现后并行准备 US3 的退出/失败测试。
4. US4 在 US1-3 行为稳定后集中收尾。

---

## 备注

- 这次特性明确要求扩展测试到调度语义，因此测试任务是必选项，不是可选项。
- 所有 `[US*]` 任务都以可独立验证为目标，但当前架构特性决定了 US2 依赖 US1、US3 依赖 US2、US4 依赖 US1-3。
- 所有任务均采用严格 checklist 格式，可直接供后续 `/speckit-implement` 或人工实现使用。
