# 实施计划：主控骨架边界收紧

**分支**：`007-controller-boundary-hardening` | **日期**：2026-05-08 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/007-controller-boundary-hardening/spec.md` 的功能规格  
**说明**：本计划基于已澄清的规格、项目宪章和当前代码现实生成，目标不是重做工步模型，而是在保持现有控制语义不变的前提下，把主控骨架收敛为“组合根 + 明确拥有者 + 最小依赖切片 + 边界型回归验证”的可扩展结构。

## 概要

本次工作不增加新的洗车工艺能力，也不改写现有工步段模型语义，而是收紧主控运行期边界。当前代码已经具备 `DDD + 六边形 + 显式状态机` 的基本形态：`system_context_t` 聚合共享对象，`process_wash_trigger_execute()` 作为统一触发编排入口，`query_wash_session_status_execute()` 作为只读查询入口，`wash_session_state_machine`、`wash_execution_service`、`wait_timeout_service` 等领域服务也已经采用参数切片结构，而不是直接把所有逻辑堆在主循环里。

当前最大风险不是结构体变大，而是共享状态、最终结果和跨层写入口还没有彻底收口。若继续在此基础上叠加安全联锁、故障策略和真机接入，`system_context_t` 很容易重新退化成任何人都能直接改的公共状态池。

本期最小可行方案是：
1. 保留 `system_context_t`，但把它明确固定为应用层组合根，只承载共享运行事实、端口和计数器，不再作为领域层直接依赖。
2. 继续使用现有 `*_service_args_t` 切片模式，把领域服务输入收缩为最小必要集合，禁止新的领域服务直接接收 `system_context_t`。
3. 把 `global_fault_*`、最近结果、触发队列和当前时间收口到固定入口：`process_wash_trigger_execute()`、`runtime_event_recorder_*()`、`main_loop_*()`。
4. 把 `process_formal_command` 明确固定为正式命令受理入口，把 `cli_command_adapter` 固定为协议适配层；适配层只允许提交事实或请求，不允许直接改写 `pending_triggers`、`pending_trigger_count` 等主循环调度状态。
5. 删除 `start_wash_cycle`、`stop_wash_cycle`、`acknowledge_fault` 这类兼容性用例入口的正式主路径地位，不再保留与正式命令入口并行的第二套受理链路。
6. 明确会话状态、执行状态、等待状态、程序快照、状态视图和结果投影的拥有者与最终解释权，其中 `wash_session.final_session_result` 是唯一会话最终落点，`last_result_code` 只保留最近一次对外投影语义。
7. 清理适配层直写业务状态、正式命令入口绕过主循环边界、兼容性入口残留、查询顺手改结果、领域服务越级控制流程等跨层直连。
8. 新增边界型回归验证，证明只读查询不改状态、故障只能从正式入口进入、会话结束后状态落点唯一，且正式命令路径不会让适配层直接碰调度状态。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：Linux、CMake、CTest、JSON 程序配置、标准 C/POSIX 库、现有模拟驱动、现有 `adapters / domain / application / platform / main` 分层  
**存储**：进程内运行时状态、JSON 程序配置文件、运行事件日志文件  
**测试方式**：CTest 驱动的单元测试、契约测试、集成测试、仿真测试、必要的 HIL smoke  
**目标平台**：Linux 仿真/工控运行环境  
**项目类型**：单进程嵌入式控制器主控骨架  
**性能目标**：保持现有单主循环推进方式；不引入额外线程、进程内消息总线或脚本解释器；边界收紧不得增加新的跨循环调度模型  
**约束**：不新增技术栈；继续坚持 `DDD + 六边形 + 显式状态机`；保留 `system_context_t` 但将其收口为组合根；领域层不得直接依赖总上下文；不改工步段模型语义、不改 JSON 工艺规则本身、不扩展真机驱动能力  
**规模/范围**：主要修改 `include/application/coordinators`、`include/application/use_cases`、`include/adapters/inbound`、`include/domain/model`、`include/domain/services`、`src/application/coordinators`、`src/application/use_cases`、`src/adapters/inbound`、`src/domain/services`、`src/platform/linux`、`src/main` 及直接相关测试；文档产物落在 `specs/007-controller-boundary-hardening/`

## 宪章检查

*关卡：进入 Phase 0 研究前必须通过；Phase 1 设计完成后必须复查。*

- 编码前先澄清：规格已明确本期只收紧边界，不做流程模型重写；用户已明确技术栈、架构原则和 `system_context_t` 的保留方式；当前计划不保留阻塞实现的未决问题。
- 简单优先：继续沿用现有 `C17 + Linux + CMake/CTest + JSON + 模拟驱动`；不新增运行时、框架、总线或 DSL；优先复用现有 `*_service_args_t` 最小依赖切片。
- 变更要精准：修改聚焦在组合根边界、正式写入口、运行期对象拥有权和边界型回归测试；不做目录大搬迁，不做与本期无关的驱动、界面和工艺扩展。
- 以目标驱动执行：每个设计产物都直接服务于三个结果：关键状态只允许少数入口改写、领域逻辑不再依赖总容器、后续单项功能的影响范围可控。
- 嵌入式 C 规范：保持 `snake_case`、头文件只声明、显式状态机、边界检查、空指针检查、明确错误返回、等待超时和中文 Doxygen 注释要求。
- 中文规范：本计划及其生成的 `research.md`、`data-model.md`、`quickstart.md`、`contracts/` 默认使用中文。

**宪章豁免**：无

## 假设、问题与权衡

**假设**：
- 当前 `process_wash_trigger_execute()`、`query_wash_session_status_execute()`、`main_loop_*()`、`runtime_event_recorder_*()` 已经足够作为边界收口的现成骨架。
- 当前 `process_formal_command_*()` 与 `cli_command_adapter_*()` 已经形成“正式命令受理入口 + 外部协议适配层”的两段式骨架，可以直接用于收口命令路径边界。
- 当前 `wash_session_service_args_t`、`wash_execution_service_args_t`、`program_snapshot_service_args_t` 等参数切片模式可继续作为领域服务最小依赖边界。
- 本期可以通过契约测试、集成测试和少量单元测试证明边界约束，而不需要引入新的测试工具栈。

**未决问题**：无。计划阶段不保留 `NEEDS CLARIFICATION`。

**被拒绝的方案**：
1. 删除 `system_context_t` 并彻底移除总上下文：当前代码已经围绕组合根成形，直接删除会把本期从“边界收紧”放大成架构推倒重来。
2. 继续允许领域服务或适配层按需访问 `system_context_t`：短期省事，但会让后续任何新增能力都直接扩大耦合面。
3. 用消息总线、事件总线或新的应用编排框架来强制解耦：超出当前单进程主控需求，也不符合“不新增栈”的要求。
4. 只补文档不补回归验证：无法防止后续功能再次把写入口和跨层依赖打散。

**最小可行方案**：
- 把 `system_context_t` 正式定义为应用层组合根，保留共享状态、端口、时间和队列，但不允许领域层直接持有或依赖它。
- 以现有 `*_service_args_t` 为基础，明确哪些字段允许进入领域服务，哪些字段必须留在应用层编排或主循环。
- 把 `global_fault_*`、`last_result_code`、`last_reason_code`、`last_transition_record`、`pending_triggers`、`pending_trigger_count`、`current_time_ms` 的拥有者和写入口写死到设计与测试里。
- 把 `process_formal_command_execute()` 明确为唯一正式命令入口，把 `cli_command_adapter_*()` 明确为只做协议适配与兼容性包装的外部适配层；正式命令路径只能通过正式提交入口驱动处理，不能让适配层直接操作调度队列。
- 删除 `start_wash_cycle`、`stop_wash_cycle`、`acknowledge_fault` 作为并行正式受理入口的保留地位，避免主控继续维持第二套等价主路径。
- 让 `query_wash_session_status_execute()` 保持只读投影入口；让 `runtime_event_recorder_apply_projection()` 成为最近结果和迁移记录的统一落点。
- 用边界型测试守住五件事：只读查询不改状态、非正式路径不能直接注入故障、会话结束后最终状态只有唯一解释落点、正式命令入口与适配层不会绕过主循环调度边界、兼容性用例入口不再作为并行主路径残留。

**范围边界**：
- 会改：`AGENTS.md` 计划引用、`specs/007-controller-boundary-hardening/*` 设计文档；实现阶段重点会落在 `include/application/coordinators/system_context.h`、`include/application/coordinators/runtime_event_recorder.h`、`include/application/use_cases/process_wash_trigger.h`、`include/application/use_cases/process_formal_command.h`、`include/application/use_cases/query_wash_session_status.h`、`include/application/use_cases/start_wash_cycle.h`、`include/application/use_cases/stop_wash_cycle.h`、`include/application/use_cases/acknowledge_fault.h`、`include/adapters/inbound/cli_command_adapter.h`、`src/application/use_cases/process_wash_trigger.c`、`src/application/use_cases/process_formal_command.c`、`src/application/use_cases/query_wash_session_status.c`、`src/application/use_cases/start_wash_cycle.c`、`src/application/use_cases/stop_wash_cycle.c`、`src/application/use_cases/acknowledge_fault.c`、`src/application/coordinators/runtime_event_recorder.c`、`src/adapters/inbound/cli_command_adapter.c`、`src/platform/linux/main_loop.c`，以及直接相关的领域模型/服务与测试。
- 不会改：工步段模型语义、JSON 工艺规则本身、真机驱动扩展、界面层、与本期边界无关的目录结构或外围协议。

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：继续遵守 `snake_case`、宏全大写、类型以 `_t` 结尾、全局变量 `g_`、静态变量 `s_`；保持 `adapters / domain / application / platform / main` 分层  
**格式与常量**：统一 4 空格缩进、禁 Tab、控制语句必须带花括号；用具名枚举、结果码和原因码替代隐式写法  
**错误策略**：正式入口在参数非法、状态非法和资源不足时返回明确错误；隐藏写入口必须收敛为显式失败或显式拒绝  
**等待策略**：继续沿用现有 `wait_condition` 与主循环超时触发机制；本期不新增新的等待模型  
**中断策略**：本期不引入 ISR 业务逻辑；所有边界收口仍在单主循环上下文完成  
**并发与内存安全**：保持单线程主循环；共享状态写入口显式化；队列、字符串复制、状态投影和服务参数切片继续做边界检查和空指针检查  
**状态机**：继续使用现有会话状态机、执行状态机和等待超时决议；本期重点是明确谁可以推进这些状态机、谁只能观察结果  
**注释风格**：新增或调整的公开接口、边界收口函数和状态拥有权说明继续使用中文 Doxygen 注释

## 项目结构

### 本功能文档

```text
specs/007-controller-boundary-hardening/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- status-and-session-finalization-contract.md
|   |-- system-context-boundary-contract.md
|   `-- trigger-runtime-orchestration-contract.md
`-- tasks.md
```

### 源码结构（仓库根目录）

```text
src/
|-- adapters/
|-- application/
|   |-- coordinators/
|   `-- use_cases/
|-- domain/
|   |-- model/
|   `-- services/
|-- platform/
|   `-- linux/
`-- main/

include/
|-- application/
|   |-- coordinators/
|   `-- use_cases/
|-- domain/
|   |-- model/
|   `-- services/
`-- platform/

tests/
|-- contract/
|-- integration/
|-- simulation/
`-- unit/
```

**结构决策**：保持现有单项目目录结构不变。本期不新增中间层，只在既有 `adapters / application / domain / platform` 边界上进一步收紧：`adapters/inbound` 只负责 CLI 协议适配与兼容性包装；`application` 负责编排组合根、正式命令入口、正式触发入口和结果投影；`domain` 负责会话、执行、等待和快照相关规则；`platform/linux` 负责时间推进与触发队列；测试按契约、集成、单元三层覆盖边界约束。

## 设计后复核结果

- Phase 0 研究已把技术栈保持、组合根保留、领域最小依赖切片、关键状态写入口和回归验证策略固化为显式决策，无 `NEEDS CLARIFICATION` 残留。
- Phase 1 设计产物保持 `C17 + Linux + CMake/CTest + JSON + 模拟驱动` 朴素技术栈，不引入新运行时或额外架构层。
- 数据模型已把组合根、关键运行状态、运行期对象和服务依赖切片拆成独立边界，并明确每类对象的拥有者与最终解释权。
- 契约文档已覆盖总上下文边界、触发编排与运行时写入口、正式命令/适配层边界、兼容性入口退场、只读查询与会话结束落点三类关键约束。
- 可进入 `/speckit-tasks`。

## 复杂度跟踪

无
