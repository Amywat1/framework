# 实施计划：DDD 架构基线加固

**分支**：`005-harden-ddd-baseline` | **日期**：2026-05-07 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/005-harden-ddd-baseline/spec.md` 的功能规格  
**说明**：本计划基于已澄清的规格、项目宪章和当前代码现实生成，目标是在保持现有主控外部行为不变的前提下，修正领域层依赖方向，收敛应用层统一编排入口，缩小 `system_context` 在领域核心中的渗透，并统一最近结果、状态摘要、日志与 CLI 响应之间的语义映射。

## 概要

本次工作不是新增洗车业务能力，而是在当前已有 `domain / application / adapters / platform / main` 分层上补齐真正稳定的 DDD 基线。当前代码已经有统一命令受理、`runtime_event_recorder` 和若干协调器雏形，但领域服务仍直接依赖 `system_context` 与应用层记录逻辑，`process_wash_trigger` 仍承担过多细节写入与结果拼装。最小可行方案是：

1. 保留单进程、单主循环、`select + timeout + stdin` 和 trigger 队列模型不变，把正式命令继续统一收敛到应用层正式入口。
2. 把领域服务从 `system_context` 和 `runtime_event_recorder` 直接依赖中剥离，改为接收窄化后的领域模型与端口，并返回领域事实或决策结果。
3. 将 `global_fault`、最近结果以及典型事件的日志/CLI/状态摘要映射统一收敛到应用层编排与记录辅助逻辑。
4. 明确会话、执行、等待条件、超时决议和全局故障的主写入边界，避免继续通过整块运行时上下文跨层改写状态。
5. 让 `main.c` 继续只承担系统外壳和主循环骨架，不让新的结构调整继续向顶层入口堆积细节。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：CMake、标准 C/POSIX 库、`system_context`、`main_loop`、`cli_command_adapter`、`runtime_event_recorder`、`process_wash_trigger`、`query_wash_session_status`、`wait_timeout_service`、`wash_execution_service`、`wash_session_state_machine`、程序快照与预检服务  
**存储**：进程内运行时状态、配置文件、运行时事件日志文件  
**测试方式**：单元测试、契约测试、集成测试、仿真测试、HIL smoke  
**目标平台**：Linux 仿真/工控运行环境  
**项目类型**：单进程嵌入式控制器主控骨架  
**性能目标**：不改变现有主循环吞吐与等待模型；重点保证正式命令语义零回归、状态可观测性稳定、运行时定位成本下降  
**约束**：保留 `select + timeout + stdin` 驱动方式、trigger 队列模型和现有正式命令协议；`status` 进入统一应用层正式入口但保持只读且不入队；`global_fault` 与最近结果只允许统一主触发编排入口写入；不得引入线程、消息总线、IPC 或新的正式入口路径；公开接口继续满足中文 Doxygen 要求  
**规模/范围**：主要修改 `src/application/use_cases`、`src/application/coordinators`、`src/domain/services`、少量 `src/adapters/inbound`、`src/platform/linux`、`src/main`、对应 `include/` 头文件及相关 contract/integration/simulation/unit 测试

## 宪章检查
*关卡：进入 Phase 0 研究前必须通过；Phase 1 设计完成后必须复查。*

- 编码前先澄清：高影响歧义已通过 `/speckit-clarify` 固化，包括 `status` 的正式入口语义、最近结果写入边界、`global_fault` 所有权以及会话/执行/等待条件/超时决议边界。
- 简单优先：本计划只收敛既有运行时结构，不新增线程、总线、目录级大搬迁或抽象层扩张；如需新增文件，仅限现有 `application/coordinators` 或相邻层中的小型辅助模块。
- 变更要精准：实现范围限定在正式入口编排、领域服务签名、运行时记录与少量顶层/适配层代码；不改命令协议、不改驱动端口设计、不做无关清理。
- 以目标驱动执行：每个实现步骤都对应明确验证目标，围绕领域层去反向依赖、统一编排入口、清晰状态所有权、统一结果表达和外部行为稳定五类验收目标展开。
- 嵌入式 C 规范：继续遵守 `snake_case`、头文件只声明、4 空格缩进、显式状态机、错误返回检查、边界检查、超时规则和中文 Doxygen 约束。
- 中文规范：本计划及其生成的 `research.md`、`data-model.md`、`quickstart.md`、`contracts/` 默认使用中文。

**宪章豁免**：无

## 假设、问题与权衡

**假设**：
- 现有 `runtime_event_recorder` 可继续作为应用层统一记录辅助，而不是继续被领域服务直接依赖。
- 现有 `query_wash_session_status_execute()` 可以继续保留为只读查询入口，并复用为 `status` 的状态视图来源。
- 领域服务签名可以在不做目录搬迁的前提下改为接收窄化的领域模型与端口组合，必要时可在领域头文件中新增仅由领域模型/端口组成的入参或结果结构。
- 当前回归测试足以支撑外部行为稳定性验证，或可通过补充局部测试完成收口验证。

**未决问题**：无。计划阶段不再存在阻塞实现正确性的待澄清项。

**被拒绝的方案**：
1. 通过新增运行时管理器、消息总线或故障中心来统一职责：超出本期“结构收敛”的最小目标。
2. 仅通过注释和约定声明领域层不应依赖应用层，而不调整函数签名与返回事实：无法真正纠正依赖方向。
3. 将 `status` 继续保留为适配层旁路查询：违背统一正式入口目标。
4. 允许领域服务或 CLI 适配层继续直接写最近结果或 `global_fault`：会继续制造跨层状态所有权混乱。

**最小可行方案**：
- 保持 `cli_command_adapter_execute_formal_line() -> main_loop_submit_trigger() -> main_loop_run()` 为正式写命令主链路，`status` 走统一应用层入口下的只读查询分支。
- 把领域服务改造成“只返回领域事实/决策结果”的接口，去掉对 `runtime_event_recorder` 与整块 `system_context` 的直接依赖。
- 由应用层统一完成领域事实到最近结果、状态摘要、日志和 CLI 响应的映射。
- 把 `global_fault` 和最近结果的最终写入权收口到统一主触发编排入口。

**范围边界**：
- 会改：`src/application/use_cases/process_wash_trigger.c`、`query_wash_session_status.c`、`src/application/coordinators/*`、`src/domain/services/*` 中与运行时编排直接相关的服务、`src/adapters/inbound/cli_command_adapter.c`、`src/platform/linux/main_loop.c`、`src/main/main.c`、对应头文件和相关测试。
- 不会改：命令协议字段、主循环调度规则、队列容量模型、驱动端口接口、部署方式、完整故障分类体系、目录总体结构。

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：继续遵守 `snake_case`、宏全大写、类型以 `_t` 结尾、头文件仅声明；保持 `domain / application / adapters / platform / main` 分层，避免再让领域直接引用应用协调器  
**格式与常量**：统一 4 空格缩进，禁用 Tab，控制语句必须带花括号，结果码/原因码/固定缓冲区继续使用具名常量或现有约定  
**错误策略**：领域服务继续返回显式 `operation_result_t` 或领域事实；应用层负责把预检失败、重复事件、空闲 stop、fault clear 违规、timeout 决议等情况映射为稳定的对外结果  
**等待策略**：等待与超时仍由 `wait_condition` 和 `wait_timeout_service` 驱动，不引入无边界等待  
**中断策略**：本期不涉及 ISR；若底层未来引入唤醒中断，仍不得在中断上下文推进主控业务状态  
**并发与内存安全**：继续默认单线程主循环；窄化领域服务参数时保持指针判空、缓冲区边界和共享状态访问边界清晰；避免把整块 `system_context` 作为万能容器继续透传  
**状态机**：`wash_session_state_machine` 只负责 session 迁移；`wash_execution_service` 只负责 execution/wait 推进；`wait_timeout_service` 只负责 timeout 决议；统一主触发编排入口负责跨对象策略、`global_fault` set/clear 和最近结果最终落点  
**注释风格**：新增或调整的公开接口继续使用中文 Doxygen 注释，说明职责、参数、返回值和状态边界

## 项目结构

### 本功能文档
```text
specs/005-harden-ddd-baseline/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- application-entry-contract.md
|   |-- runtime-result-projection-contract.md
|   `-- state-write-boundaries-contract.md
`-- tasks.md
```

### 源码结构（仓库根目录）
```text
src/
|-- adapters/
|   `-- inbound/cli_command_adapter.c
|-- application/
|   |-- coordinators/
|   |   |-- compatibility_trigger_runner.c
|   |   `-- runtime_event_recorder.c
|   `-- use_cases/
|       |-- process_wash_trigger.c
|       |-- query_wash_session_status.c
|       |-- start_wash_cycle.c
|       |-- stop_wash_cycle.c
|       `-- acknowledge_fault.c
|-- domain/
|   `-- services/
|       |-- wait_timeout_service.c
|       |-- wash_execution_service.c
|       `-- wash_session_state_machine.c
|-- main/main.c
`-- platform/linux/main_loop.c

include/
|-- application/coordinators/
|-- application/use_cases/
|-- domain/services/
`-- adapters/inbound/

tests/
|-- contract/
|-- integration/
|-- simulation/
`-- unit/
```

**结构决策**：保持现有单项目结构不变。实现将聚焦于应用协调器、用例和领域服务职责边界的收敛；如需新增统一正式入口等薄应用层骨架文件，可放在现有 `application/use_cases` 及对应头文件目录；如需新增投影、记录等辅助模块，仅放在现有 `application/coordinators` 或对应头文件目录中，不做目录迁移。

## 设计后复核结果

- Phase 0 研究已把所有关键设计决策落实为显式文档，无 `NEEDS CLARIFICATION` 残留。
- Phase 1 设计产物保持单进程、单主循环、统一正式入口和显式状态所有权，不引入违背宪章的新运行时模型。
- 契约文档已把统一入口、统一结果映射和状态写入边界收敛为可评审的外部/内部接口约束。
- 可进入 `/speckit-tasks`。

## 复杂度跟踪

无
