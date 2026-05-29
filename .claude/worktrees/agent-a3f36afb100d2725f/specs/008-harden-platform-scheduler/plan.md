# 实施计划：平台层调度器收敛

**分支**：`008-harden-platform-scheduler` | **日期**：2026-05-09 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/008-harden-platform-scheduler/spec.md` 的功能规格  
**说明**：本计划围绕“单线程领域推进模型 + 平台层 `controller_scheduler` 抽象”展开，目标是在不改写现有领域状态机语义的前提下，把等待、唤醒、周期驱动、退出和调度观测收敛到平台层。

## 概要

本次特性把控制器运行时拆成两层职责：平台层 `controller_scheduler` 负责等待、唤醒、外部事件源接入、退出与观测；现有 `main_loop_*()` 继续负责时间推进后的单拍领域处理。`main.c` 收敛为纯组合根，只创建 `system_context_t`、装配驱动和端口、构造调度配置并启动调度器。Linux 实现采用 `epoll + timerfd + eventfd + CLOCK_MONOTONIC`，必要时允许 `pthread` 仅承担采集/通知职责，但领域推进始终保持单线程。现有 CTest 测试体系与日志体系继续扩展到调度语义与节拍观测。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：Linux/POSIX、`epoll`、`timerfd`、`eventfd`、`clock_gettime(CLOCK_MONOTONIC)`、必要时 `pthread`、CMake、CTest、现有 `adapters / application / domain / platform / main` 分层  
**存储**：进程内运行态、JSON 程序配置、运行时事件日志文件、平台层调度观测输出  
**测试方式**：CTest 驱动的 contract / integration / simulation / unit 测试，必要时复用现有 `tests/test_support.h` 辅助  
**目标平台**：Linux 仿真与工控运行环境  
**项目类型**：单进程嵌入式控制器主控程序  
**性能目标**：控制周期可配置；稳定态每拍工作量有上界；不再依赖无限排空；超拍与连续超拍必须可观测  
**约束**：保持单线程领域推进；`system_context_t` 不承载 OS 对象；`main_loop_run()` 不承担等待；`main.c` 不保留事件循环细节；通知路径不直接改领域状态；不引入消息总线或新的脚本运行时  
**规模/范围**：重点修改 `src/main/main.c`、`src/platform/linux/*`、`include/platform/*`、`process_formal_command`/`cli_command_adapter` 与相关 contract/integration/simulation tests；设计产物落在 `specs/008-harden-platform-scheduler/`

## 宪章检查

*关卡：进入 Phase 0 前必须通过；Phase 1 设计完成后必须复核。*

- 编码前先澄清：本计划已明确运行时壳层、单拍推进、主程序组合根和外部事件语义四条边界；未保留未解决的澄清占位。
- 简单优先：采用最小增量方案，在现有 `system_context_t + main_loop_*()` 上叠加 `controller_scheduler`，不引入新的总线、队列框架或并发架构。
- 变更要精准：改动集中在平台调度边界、`main.c` 外层循环剥离、命令/通知/退出事件源分类，以及调度语义测试；不重写领域算法。
- 以目标驱动执行：每个实现阶段都绑定明确验证点，包括周期驱动、通知只更新缓存、命令补跑、退出有界排空、观测指标输出。
- 嵌入式 C 规范：继续遵守 `snake_case`、显式错误返回、等待必须有超时与失败语义、共享数据边界明确、中文 Doxygen 注释要求。
- 中文规范：本计划与后续 `research.md`、`data-model.md`、`quickstart.md`、`contracts/` 默认使用中文，仅保留必要英文标识符。

**宪章豁免**：无

## 假设、问题与权衡

**假设**

- 现有 `main_loop_advance_time()` 与 `main_loop_run()` 已经足够表达“推进时间后执行一拍”的核心语义。
- 现有 `main_loop_submit_trigger()` 可以继续承担“受控入队”职责，但其调用权将更多收敛到调度器与正式入口。
- 现有 `event_logger_port` 可以扩展调度器节拍、超拍和事件触发观测，而无需引入新的日志基础设施。
- 采集侧如需线程，仅用于刷新快照缓存并通过 `eventfd` 唤醒调度器，不直接推进领域状态。

**未决问题**

- 无。`setup-plan` 阶段没有遗留阻塞实现的澄清项。

**被拒绝的方案**

- 保留 `main.c` 中的 `select + stdin + wait_timeout` 外层循环：会继续把 Linux 等待细节暴露在组合根，并使业务超时继续充当线程唤醒时钟。
- 把等待逻辑塞回 `main_loop_run()`：会破坏“单拍推进”的边界，让应用/领域层再次感知平台等待机制。
- 继续让 `process_formal_command_execute()` 通过循环调用 `main_loop_run()` 驱动领域推进：会让命令路径绕过统一调度策略，破坏“调度器决定何时补跑一拍”的模型。
- 直接引入多线程工作队列或消息总线：超出当前单进程单线程主控需求，也不符合最小可行方案。

**正式实现约束**：
- `process_formal_command_execute()` 只保留命令解析、正式受理、入队请求与响应生成职责；命令提交后的实际消费时机、补跑次数和排空边界统一交由平台调度器决定。

**最小可行方案**

- 新增 `controller_scheduler` 抽象头文件，定义配置、启动、停止、事件源启停和观测语义。
- 在 Linux 平台层实现私有调度器对象，内部统一持有 `epoll`、`timerfd`、`eventfd`、事件循环状态、周期驱动、事件源注册、退出状态和统计信息；首期实现以固定控制周期事件源、至少一种命令事件源和退出事件源为必选，通知事件源先完成分类边界、缓存更新语义与 `eventfd` 唤醒接线，独立通知通道可按后续增量接入。
- `eventfd` 作为通知路径或辅助线程的统一唤醒句柄：采集侧或通知侧只负责刷新快照缓存并写入 `eventfd`，由调度器在线程内统一消费，不直接推进领域状态。
- 用固定周期驱动作为稳定态唯一主时钟；每次周期唤醒先推进时间，再按固定顺序处理退出事件、高优先级命令补跑、通知唤醒与常规周期推进，然后执行一次或有限次 `main_loop_run()`；单拍推进的补跑机会、有限排空和退出期推进策略只允许由调度器决定。
- 外部命令、通知、退出事件都先进入平台事件分类，再由调度器选择“入队”“仅更新缓存”“开始有界退出”；命令受理路径不得再自行通过循环调用 `main_loop_run()` 消费待处理队列。
- `main.c` 只保留装配和启动；现有 `drain_runtime()` 和 `wait_for_input_or_timeout()` 从主程序移出。
- 为调度语义新增 contract / integration / simulation 测试，并扩展运行日志观测；`epoll`/`timerfd`/`eventfd` 初始化、注册、读写和关闭失败都必须进入正式错误语义并记录最近错误原因。

**范围边界**

- 会改：`include/platform/`、`include/platform/linux/`、`src/platform/linux/`、`src/main/main.c`、`src/application/use_cases/process_formal_command.c`、`src/adapters/inbound/cli_command_adapter.c`、调度相关 tests、`AGENTS.md` 计划引用。
- 不会改：现有工艺规则、领域状态机核心语义、JSON 程序规则本身、模拟驱动能力范围、UI/管理界面、与本期无关的驱动重构。
- 首期不会一次性做完所有潜在通知事件源的完整接入；若当前没有真实采集线程或独立通知通道，本期只需完成通知事件的分类语义、缓存更新边界、调度器接口留口与对应验证。

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：继续使用 `snake_case`、宏全大写、类型 `_t` 结尾、`g_`/`s_` 前缀，新增调度器接口落在 `platform` 边界，Linux 专有实现落在 `platform/linux`  
**格式与常量**：统一 4 空格缩进、禁 Tab、控制语句带花括号；周期、排空上限、观测阈值使用具名常量或配置字段  
**错误策略**：调度器启动失败、事件源关闭、`epoll`/`timerfd`/`eventfd` 操作失败、触发队列满、单拍推进失败都返回明确错误并记录最近失败原因  
**等待策略**：所有等待由调度器统一完成；周期定时与外部唤醒都有显式超时/失败路径；退出阶段采用有界排空次数或时长，不允许无限等待  
**中断策略**：本期不引入 ISR 业务逻辑；若后续存在采集线程，仅做缓存更新与 `eventfd` 唤醒，不进行领域计算与日志格式化  
**并发与内存安全**：领域推进保持单线程；若使用 `pthread`，共享内容只限快照缓存、通知标记和线程退出同步，必须有显式保护边界  
**状态机**：保留现有会话/执行/等待状态机；新增调度器运行状态仅覆盖 `initialized -> running -> draining -> stopped/failed`  
**注释风格**：新增公开调度器接口、配置结构、事件源类别与观测结构均补齐中文 Doxygen 注释

## 项目结构

### 本功能文档
```text
specs/008-harden-platform-scheduler/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- controller-scheduler-contract.md
|   |-- event-source-dispatch-contract.md
|   |-- main-loop-boundary-contract.md
|   |-- scheduler-exit-contract.md
|   `-- scheduler-observability-contract.md
`-- tasks.md
```

### 源码结构（仓库根目录）
```text
src/
|-- adapters/
|   `-- inbound/
|-- application/
|   |-- coordinators/
|   `-- use_cases/
|-- domain/
|   |-- model/
|   `-- services/
|-- main/
`-- platform/
    |-- drivers/
    `-- linux/

include/
|-- adapters/
|-- application/
|-- domain/
|-- platform/
|   `-- linux/
`-- shared/

tests/
|-- contract/
|-- integration/
|-- simulation/
|-- unit/
`-- fixtures/
```

**结构决策**：保持现有单项目目录结构不变。在 `include/platform/` 下新增调度器抽象头文件，在 `src/platform/linux/` 下新增 Linux 私有实现，并复用现有 `main_loop_*()`、`system_context`、`process_formal_command` 与测试辅助结构。

## 设计后复核结果

- Phase 0 研究已把调度器边界、Linux 等待机制、可选通知线程、观测策略和测试扩展方案收敛为明确决策，没有保留未决澄清。
- Phase 1 设计保持单线程领域推进模型不变，只把等待/唤醒/调度壳层上收至平台层，符合“简单优先”和“变更要精准”。
- 调度器公开边界不暴露 `epoll fd`、`timerfd`、`eventfd` 等平台细节；Linux 专有对象继续留在私有实现文件中。
- 新增契约围绕三类关键边界：调度器抽象、事件源分类分发、`main.c`/`main_loop_*()` 责任分离。
- `AGENTS.md` 已切换到本计划，后续 `/speckit-tasks` 可直接依据本设计产物继续分解任务。

## 复杂度跟踪

无
