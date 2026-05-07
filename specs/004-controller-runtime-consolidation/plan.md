# 实施计划：控制器运行时收敛

**分支**：`004-controller-runtime-consolidation` | **日期**：2026-05-06 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/004-controller-runtime-consolidation/spec.md` 的功能规格  
**说明**：本计划基于已完成澄清的规格、项目宪章和当前代码现实生成，目标是在保持 C17、Linux、DDD 分层、单进程主循环和既有命令语义不变的前提下，收敛控制器运行时的正式命令入口、写路径入口、结果记录方式、状态写入责任边界和 `main.c` 顶层职责。

## 概要

本次工作不是新增洗车业务能力，而是把已经存在的两套命令处理方式、分散的结果记录写法和模糊的状态写入边界收敛成一组稳定的运行时骨架。最小可行方案是：

1. 将正式 stdin 命令统一收敛到正式受理入口；其中写命令采用 `命令解析 -> trigger 入队 -> main_loop_run -> 响应生成` 路径，`status` 采用 `命令解析 -> 只读查询 -> 响应生成` 路径。  
2. 保留必要测试 helper 和内部只读查询接口，但去掉 helper 承载第二套主业务执行链的能力。  
3. 将 `last_result_code`、`last_reason_code`、transition/log/response 的写入规则统一到少数辅助逻辑。  
4. 以 `wash_session_state_machine`、`wash_execution_service`、`wait_timeout_service` 和 `process_wash_trigger` 重新划定状态写入责任。  
5. 把 `main.c` 下沉为主入口和主循环骨架，仅保留初始化、等待输入、推进时间、协调运行时和输出结果的顶层流程。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：CMake、标准 C/POSIX 库、`system_context`、`main_loop`、`cli_command_adapter`、`process_wash_trigger`、`query_wash_session_status`、`wait_timeout_service`、`trigger_priority_service`、文件日志与程序仓库适配器  
**存储**：进程内内存状态、配置文件、运行时事件日志文件  
**测试方式**：单元测试、集成测试、契约测试、仿真测试  
**目标平台**：Linux 仿真/工控运行环境  
**项目类型**：单进程嵌入式控制器主控骨架  
**性能目标**：不改变现有主循环吞吐与等待模型；重点保证命令语义零回归、结果记录一致和运行时持续可排空  
**约束**：保持 stdin 单行命令协议不变；保持 select + timeout + stdin 模型不变；保持 trigger 队列和优先级规则不变；保持单任务单车位控制边界不变；所有公开接口继续满足中文 Doxygen 要求  
**规模/范围**：主要修改 `src/main`、`src/platform/linux`、`src/adapters/inbound`、少量 `src/application/use_cases` 与 `include/application`，以及相关 `tests/contract`、`tests/integration`、`tests/simulation`

## 宪章检查
*关卡：进入 Phase 0 研究前必须通过，Phase 1 设计完成后必须复查。*

- 编码前先澄清：规格中的高影响问题已通过 `/speckit-clarify` 固化，包括 `global_fault` 写入责任、`status` 的正式路径地位、helper 边界、忽略事件是否刷新最近结果，以及 `fault clear` 在本轮不重定义外部语义。
- 简单优先：本计划只收敛已有命令与运行时骨架，不引入多线程、socket、IPC、新事件总线或完整故障中心。
- 变更要精确：实现范围限定在 `src/main/main.c`、`src/platform/linux/main_loop.c`、`src/adapters/inbound/cli_command_adapter.c`、薄 use_case、`system_context` 及对应头文件与测试。
- 以目标驱动执行：后续任务围绕五类验收目标展开：正式单路径、写入口减少、记录语义一致、状态写入边界清晰、主入口职责变薄。
- 嵌入式 C 规范：继续遵守 `snake_case`、头文件仅声明、4 空格缩进、显式状态机、错误返回检查、边界检查、超时规则和中文 Doxygen 约束。
- 中文规范：本计划和本次生成的 `research.md`、`data-model.md`、`quickstart.md`、`contracts/` 默认使用中文。

**宪章豁免**：无

## 假设、问题与权衡

**假设**：
- `main_loop_submit_trigger()`、`main_loop_run()`、`main_loop_advance_time()` 可继续作为正式运行时骨架复用，不需要重写事件调度器。
- `query_wash_session_status_execute()` 可以继续作为内部只读查询入口保留。
- 现有测试支持代码允许从“驱动正式路径”和“观察只读状态”两个方向调整，而不需要长期保留直调业务核心的 helper。

**未决问题**：无。计划阶段不再存在阻塞实现正确性的待澄清项。

**被拒绝的方案**：
1. 保留 `cli_command_adapter_process_line()` 作为长期正式路径旁路：被拒绝，因为会延续“正式路径 + 直调路径”双链路。
2. 通过新增统一运行时管理器/消息总线解决职责问题：被拒绝，因为这会超出“收敛现有结构”的最小目标。
3. 让 `main.c` 继续同时承载命令解析、入队、排空和响应细节：被拒绝，因为会阻碍后续运行时演进。
4. 在本轮借收敛名义重定义 `fault clear` 在无 `global_fault` 时的外部结果：被拒绝，因为本轮只做结构收敛，既有命令响应语义保持不变。

**最小可行方案**：
- 将正式 stdin 命令统一收敛到正式受理入口；其中写命令采用 `prepare -> submit -> drain -> finalize` 模式，`status` 采用 `prepare -> 只读查询 -> 统一响应生成` 模式。
- 保留 `start_wash_cycle.c`、`stop_wash_cycle.c`、`acknowledge_fault.c` 这些现有文件，但将其降级为兼容性辅助包装，不再作为正式主路径入口。
- 在 `process_wash_trigger.c` 内抽出统一的“事件结论记录”与“响应语义来源”逻辑。
- 通过职责约束消除 `global_fault`、`wash_session`、`wash_execution`、`wait_condition` 的越界写入。
- 将 `main.c` 拆为输入等待/时间推进/排空协调辅助模块或辅助函数组合，保留现有运行模型。

**范围边界**：
- 会改：`src/main/main.c`、`src/platform/linux/main_loop.c`、`src/adapters/inbound/cli_command_adapter.c`、`src/application/use_cases/process_wash_trigger.c`、薄 use_case、`include/application/coordinators/system_context.h`、相关头文件、`tests/contract`、`tests/integration`、`tests/simulation`。
- 不会改：命令协议字段、主循环优先级规则、队列容量/模型、领域模型定义、目录结构、驱动端口设计、完整故障分类体系。

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：文件/函数/变量保持 `snake_case`；宏全大写；类型以 `_t` 结尾；头文件仅声明；继续遵守 `domain / application / adapters / platform / main` 分层  
**格式与常量**：统一 4 空格缩进；禁用 Tab；控制语句必须带花括号；结果码、原因码、响应文本和固定大小缓冲区继续使用具名常量或现有约定  
**错误策略**：未知命令、参数缺失、全局故障阻断、运行中重复 start、空闲 stop/feedback 及其他既有异常/空操作场景，都必须保持当前显式结论与最近结果语义，不得借本轮收敛重定义对外行为  
**等待策略**：等待与超时仍由 `wait_condition` 和 `wait_timeout_service` 驱动；不引入无边界等待  
**中断策略**：本次不涉及 ISR；若未来底层引入唤醒中断，仍不在中断上下文推进主控状态  
**并发与内存安全**：继续默认单线程主循环；所有解析与响应缓冲区保持边界检查；`system_context` 中最近结果与全局故障字段必须显式初始化与重置  
**状态机**：`wash_session_state_machine` 仅负责 session 迁移；`wash_execution_service` 仅负责 execution/wait 推进；`wait_timeout_service` 仅负责 timeout 决策；`process_wash_trigger` 仅负责路由、高层策略和 `global_fault` set/clear  
**注释风格**：所有新增或调整的公开接口使用中文 Doxygen 注释

## 项目结构

### 本功能文档
```text
specs/004-controller-runtime-consolidation/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- command-path-contract.md
|   |-- result-semantics-contract.md
|   `-- state-write-boundaries.md
`-- tasks.md
```

### 源码结构（仓库根目录）
```text
src/
|-- adapters/
|   `-- inbound/cli_command_adapter.c
|-- application/
|   |-- use_cases/process_wash_trigger.c
|   |-- use_cases/query_wash_session_status.c
|   |-- use_cases/start_wash_cycle.c
|   |-- use_cases/stop_wash_cycle.c
|   `-- use_cases/acknowledge_fault.c
|-- domain/
|   `-- services/
|       |-- wait_timeout_service.c
|       |-- trigger_priority_service.c
|       |-- wash_execution_service.c
|       `-- wash_session_state_machine.c
|-- main/main.c
`-- platform/linux/main_loop.c

include/
|-- adapters/inbound/cli_command_adapter.h
|-- application/coordinators/system_context.h
|-- application/use_cases/
`-- platform/linux/main_loop.h

tests/
|-- contract/
|   `-- test_controller_stdin_protocol.c
|-- integration/
|   `-- test_controller_command_paths.c
`-- simulation/
    `-- test_controller_global_fault_behavior.c
```

**结构决策**：保持现有单项目结构不变。实现将聚焦于正式命令入口、主循环与应用层写路径的收敛，不做目录搬迁或层次扩张。
其中 `start_wash_cycle.c`、`stop_wash_cycle.c`、`acknowledge_fault.c` 在本轮计划中按“保留但降级”处理：文件可继续存在，但不得再作为生产代码的正式主路径入口。

## 设计后复核结果

- Phase 0 研究已将所有实现关键决策落成显式文档，无 `NEEDS CLARIFICATION` 残留。
- Phase 1 设计产物保持单进程、单路径、显式职责和中文文档约束。
- 设计中没有引入违反宪章的新抽象层或多余运行模型。
- 可进入 `/speckit-tasks`。

## 复杂度跟踪

无
