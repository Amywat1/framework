# 实施计划：洗车工步真实控制模型

**分支**：`006-wash-step-control` | **日期**：2026-05-08 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/006-wash-step-control/spec.md` 的功能规格  
**说明**：本计划基于已澄清的规格、项目宪章和当前代码现实生成，目标是在保持技术栈朴素的前提下，把当前按 `wash_stage` 粗阶段推进的控制器重构为“配置适配层 + 强类型领域模型 + 应用编排 + 平台运行时”四层分离的工步段控制模型。若现有程序缺少 RO 水段或风干段所需执行机构品类，本期一并补齐；若旧阶段路径与新工步段模型冲突，则直接以新模型替换，不保留兼容双轨。

## 概要

本次工作不是在现有阶段模型上继续打补丁，而是把洗车流程的核心语义改写成工步段模型。当前代码已经具备 `domain / application / adapters / platform / main` 分层、`json_program_parser`、`process_formal_command`、`process_wash_trigger`、`main_loop`、仿真驱动和现有测试体系，但领域模型仍以 `wash_program -> wash_stage` 为中心，配置解析仍停留在字符串探测和内建程序回退，运行期控制对象也还没有完整覆盖 RO 水段与风干段。

虽然规格中的三个用户故事业务优先级都为 `P1`，但本计划中的实施顺序表达的是技术依赖而不是业务价值排序：必须先完成配置与强类型模型，再切换主链路编排，最后补齐位置语义、异常收口与 RO 水/风干闭环。

本期最小可行方案是：
1. 保持 `C + Linux + CMake + ctest` 和单进程主循环不变，不引入脚本引擎、规则引擎、消息总线。
2. 在 `adapters/config` 把 JSON 配置严格映射为强类型 `wash_program_v1_t / wash_segment_t` 等内部模型，字符串只允许停留在该层。
3. 在 `domain/model` 和 `domain/services` 重建工步段、位置语义和超时/异常决议的强类型规则，其中 `continuous_controls` 必须是固定字段集合，`conditional_controls` 必须是受限结构且 `v1` 只覆盖药剂窗口控制，`exit_actions` 必须是固定收尾动作集合；直接替换旧 `wash_stage` 粗粒度语义。
4. 在 `application/use_cases` 和 `application/coordinators` 建立统一工步编排入口，按“进入段 -> 运行段 -> 退出段”持续推进，并统一收口 `fault / stop / timeout / exit_failure`。
5. 在 `platform/linux` 和 `platform/drivers` 保持运行时采集与控制输出职责，同时补齐 RO 水和风干所需执行机构品类与反馈闭环。
6. 继续使用当前测试体系，但把验证对象升级为工步段流程、条件控制、退出动作和异常收尾。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：CMake、ctest、标准 C/POSIX 库、现有 `main_loop`、`json_program_parser`、`process_formal_command`、`process_wash_trigger`、`wash_execution_service`、`wash_session_state_machine`、仿真驱动与程序仓储端口  
**存储**：进程内运行时状态、JSON 程序配置文件、运行事件日志文件  
**测试方式**：单元测试、契约测试、集成测试、仿真测试、HIL smoke  
**目标平台**：Linux 仿真/工控运行环境  
**项目类型**：单进程嵌入式控制器主控骨架  
**性能目标**：保持现有单主循环推进方式，不引入额外线程、总线或解释器；工步持续评估必须在现有主循环节拍内完成；配置解析只发生在配置加载阶段，不进入运行期字符串解释  
**约束**：继续保持朴素技术栈；JSON 仅作为外部配置格式；领域层禁止依赖 JSON 字符串；应用层只负责编排不解释工艺文本；平台层只带入现场事实和发出控制命令；允许删除不再需要的旧阶段模型代码；新增执行机构品类必须遵守现有安全与反馈要求；公开接口继续满足中文 Doxygen 要求  
**规模/范围**：主要修改 `src/adapters/config`、`src/domain/model`、`src/domain/services`、`src/application/use_cases`、`src/application/coordinators`、`src/domain/ports`、`src/platform/drivers`、`src/platform/linux` 及对应 `include/` 头文件，并补充 `tests/unit`、`tests/integration`、`tests/simulation`、`tests/contract` 中与工步段和配置契约直接相关的测试

## 宪章检查
*关卡：进入 Phase 0 研究前必须通过，Phase 1 设计完成后必须复查。*

- 编码前先澄清：规格已明确本期采用工步段模型、补齐 RO 水/风干执行机构缺口、且不要求兼容旧阶段模型；当前计划不保留阻塞实现的未决问题。
- 简单优先：继续使用 C、Linux、CMake、ctest 与现有主循环/测试骨架；不引入脚本引擎、规则引擎、消息总线或额外运行时。
- 变更要精准：变更集中在配置解析、强类型领域模型、应用编排、执行机构端口与仿真驱动、以及与之直接相关的测试；不做与工步段改造无关的目录级搬迁。
- 以目标驱动执行：每个设计产物都服务于六类目标：冻结工步段规则、真实化配置、重建主链路、统一位置语义、明确异常收尾、升级验收回归。
- 嵌入式 C 规范：保持 `snake_case`、头文件只声明、显式状态机、边界检查、错误返回、超时与重试、中文 Doxygen；新增执行机构品类和状态模型必须遵守同样约束。
- 中文规范：本计划及其生成的 `research.md`、`data-model.md`、`quickstart.md`、`contracts/` 默认使用中文。

**宪章豁免**：无

## 假设、问题与权衡

**假设**：
- 现有平台层可以继续作为现场事实入口，补齐 RO 水段和风干段所需执行机构时无需引入新的进程模型。
- 当前 `tests/unit`、`tests/integration`、`tests/simulation` 和 `tests/contract` 足以承载工步段模型的回归升级。
- 现有 `actuator_port_t`、`sensor_port_t` 和仿真驱动允许扩展为更明确的强类型执行机构接口。

**未决问题**：无。计划阶段不保留 `NEEDS CLARIFICATION`。

**被拒绝的方案**：
1. 引入脚本引擎或通用动作 DSL 来表达工步控制：控制对象有限且明确，固定结构体与枚举更稳、更易审查。
2. 保留 JSON 字符串一路进入领域层和应用层：会把字段名、条件名、退出动作名的字符串比较扩散到主控核心。
3. 通过规则引擎统一条件判断：超出当前控制软件需要，且会增加调试与定位成本。
4. 通过消息总线解耦应用层和平台层：与单进程主控现状不匹配，复杂度远高于收益。
5. 为兼容旧阶段模型保留新旧双轨运行：会让同一流程持续存在两套互相冲突的有效语义。

**最小可行方案**：
- 用强类型 `wash_program_v1_t -> wash_segment_t` 直接替换 `wash_program -> wash_stage` 作为核心工艺表达；`wash_segment_t` 内部不得退化成通用 `controls/conditions/exit_actions` 数组解释器，而必须拆为固定结构的 `continuous_controls + conditional_controls + exit_actions`。
- `continuous_controls` 采用固定字段表达当前已知持续控制能力，例如龙门移动、顶刷跟随、侧刷运行、RO 水运行、风干运行；不允许回到 `target/state/parameters` 风格的弱类型动作描述。
- `conditional_controls` 采用受限结构表达按位置启停的附加控制，`v1` 只覆盖药剂窗口控制；每条规则必须显式声明位置基准、开始条件和停止条件，不预留通用脚本式条件解释器。
- `exit_actions` 采用固定收尾动作集合表达停刷、停药、回零、RO 水关闭、风干关闭等正式收尾动作；运行语义固定为并行触发、全部完成才算收尾完成，且收尾阶段出现 `stop/fault` 时立即整体中断。
- 保留 JSON 作为外部配置，但把 `forward / reverse / tail / head / abort_session / safe_finish` 等文本在 `adapters/config` 一次性映射成枚举。
- 在应用层建立统一工步编排入口，持续推进“进入段 -> 运行段 -> 退出段”，并统一处理 stop/fault/timeout/exit_failure。
- 扩展 `actuator_port_t` 及平台驱动，补齐 RO 水与风干执行机构品类及关闭反馈。
- 以工步段业务结果而非旧阶段状态迁移作为主要验收口径。

**范围边界**：
- 会改：`include/domain/model/*`、`src/domain/model/*`、`include/domain/services/*`、`src/domain/services/*`、`include/domain/ports/*`、`src/adapters/config/*`、`include/adapters/config/*`、`src/application/use_cases/*`、`src/application/coordinators/*`、`src/platform/drivers/*`、`src/platform/linux/main_loop.c`、对应测试。
- 不会改：技术栈、构建系统、主循环单进程模式、与本期无关的驱动部署方式、与工步段无关的外围协议。

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：继续遵守 `snake_case`、宏全大写、类型以 `_t` 结尾、全局变量 `g_`、静态变量 `s_`；保持 `adapters / domain / application / platform / main` 四层分离  
**格式与常量**：统一 4 空格缩进、禁 Tab、控制语句必须带花括号、用具名枚举和具名常量替代字符串散落与魔法数字  
**错误策略**：配置适配层对非法字段、非法组合和不支持能力返回明确失败；应用层对 stop/fault/timeout/exit_failure 统一映射固定业务结果；平台层对新增执行机构操作返回明确错误  
**等待策略**：所有段内等待、退出等待和关闭确认都必须定义超时；区分段内超时与收尾超时；必要时保留有限重试  
**中断策略**：本期不引入 ISR 业务逻辑；若底层未来存在中断唤醒，也不得在中断上下文推进工步业务  
**并发与内存安全**：保持单线程主循环；领域模型使用固定结构体和上限数组；配置解析、状态推进、反馈匹配和字符串映射都要做边界检查和空指针检查  
**状态机**：至少显式区分工步进入、运行、退出、完成、异常中止；退出动作失败必须进入正式状态机语义而不是旁路处理  
**注释风格**：新增公开接口、执行机构端口、配置映射结构和关键状态机接口继续使用中文 Doxygen 注释

## 项目结构

### 本功能文档
```text
specs/006-wash-step-control/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- program-config-adapter-contract.md
|   |-- segment-runtime-orchestration-contract.md
|   `-- position-exit-semantics-contract.md
`-- tasks.md
```

### 源码结构（仓库根目录）
```text
src/
|-- adapters/
|   |-- config/
|   |   `-- json_program_parser.c
|   |-- inbound/
|   |-- logging/
|   `-- outbound/
|-- application/
|   |-- coordinators/
|   |-- dto/
|   `-- use_cases/
|-- domain/
|   |-- events/
|   |-- model/
|   |-- ports/
|   `-- services/
|-- platform/
|   |-- drivers/
|   |-- linux/
|   `-- osal/
`-- main/

include/
|-- adapters/
|-- application/
|-- domain/
|-- platform/
`-- shared/

tests/
|-- contract/
|-- hil/
|-- integration/
|-- simulation/
`-- unit/
```

**结构决策**：保持现有单项目目录结构不变。本期只在既有四层目录内推进改造：`adapters/config` 负责 JSON 映射，`domain/model` 与 `domain/services` 负责强类型工步规则，`application/use_cases` 与 `application/coordinators` 负责统一编排，`platform/drivers` 与 `platform/linux` 负责事实输入和控制输出。若旧 `wash_stage` 相关文件无法承载新模型，可在现有目录下直接替换或删除，不额外引入中间层。

## 设计后复核结果

- Phase 0 研究已把技术栈、四层分离、配置映射边界、位置语义、异常收尾和测试策略固定为显式决策，无 `NEEDS CLARIFICATION` 残留。
- Phase 1 设计产物保持 C/Linux/CMake/ctest 朴素技术栈，不引入脚本引擎、规则引擎、消息总线。
- 数据模型已把工步段、条件控制、退出动作、执行机构品类、位置快照和异常策略拆成强类型结构，且明确与旧 `wash_stage` 的替换关系。
- 契约文档已覆盖 JSON 配置映射、工步运行编排和位置/退出语义三类关键接口边界。
- 可进入 `/speckit-tasks`。

## 复杂度跟踪

无
