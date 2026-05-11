# 实施计划：主控 Runtime/Bootstrap 正式入口

**分支**：`010-controller-runtime-bootstrap` | **日期**：2026-05-11 | **规格**：[spec.md](./spec.md)  
**输入**：来自 `specs/010-controller-runtime-bootstrap/spec.md` 的功能规格

**说明**：本计划围绕“把主控组合根与启动/停机闭环收口成一套正式 runtime/bootstrap 入口”展开。目标不是重做洗车业务流程，也不是扩展调度能力，而是把 `main.c`、测试辅助和适配器初始化里分散的装配步骤统一起来，固定为 `create/run/destroy` 生命周期，并把启动失败回滚、销毁顺序、单实例约束和资源所有权边界正式化。

## 概要

本次特性新增一个正式 `controller_runtime` 入口对象，用来承载“获取 `system_context`、装配端口、初始化仓储/日志、创建 scheduler、运行主循环、销毁 runtime owned 资源”的完整闭环。`create` 成功后实例必须达到“可运行但未运行”状态，且 `system_context`、必需端口、程序仓储、事件日志和 scheduler 全部就绪；同一进程同一时刻只允许存在 1 个正式 runtime 实例；`run` 只负责进入阻塞式运行，不再补做正式装配，且一旦返回就把实例推进到只能 `destroy` 的终态；`destroy` 负责清理 runtime 自己创建的资源，并能安全处理空对象、部分创建成功对象和已销毁对象。

实现重点不是新增一层概念，而是把现有分散装配路径收成唯一正式主路径。正式可执行入口和测试辅助都要复用同一套装配闭环，只通过配置注入替换驱动、路径、stdio 和测试桩。与此同时，`file_program_repository_init` 与 `file_event_logger_init` 需要改成返回 `operation_result_t`，这样 `create` 才能建立统一错误传播与逆序回滚链路。

## 技术上下文

**语言/版本**：C17  
**主要依赖**：Linux、CMake/CTest、JSON 配置、模拟驱动、现有 `system_context`、Linux scheduler、DDD + 六边形 + 显式状态机分层  
**存储**：进程内运行态；程序配置与事件日志继续使用文件适配器；不新增数据库或新型持久化  
**测试方式**：CTest 驱动的 `unit` / `contract` / `integration` / `simulation` 测试  
**目标平台**：Linux 单进程主控仿真环境  
**项目类型**：单项目 C 静态库 + 主程序  
**性能目标**：不改变现有控制周期与阻塞式运行语义；创建与销毁路径保持有界、可预测；不引入新的动态运行模型  
**约束**：不新增技术栈；不改洗车业务语义、工步段模型或 JSON 规则；不引入异步 `start/stop`；`controller_runtime` 公开接口不得无控制暴露 Linux 平台细节；`system_context_t` 继续只承载组合根运行期状态，不扩大职责面  
**规模/范围**：核心修改集中在 `application/coordinators`、`main`、`tests/test_support.h`、文件仓储/日志适配器、scheduler 创建路径及相关测试

## 宪章检查

- 编码前先澄清：本次方向已经固定为“正式 runtime/bootstrap 入口 + `create/run/destroy` + 明确所有权边界 + 统一失败回滚与销毁顺序”，不存在待澄清阻塞项。
- 简单优先：不引入线程、后台服务、异步 stop、额外运行时或新框架；直接在现有分层上增加最小正式入口对象。
- 变更要精确：改动限定在 runtime/bootstrap 入口、装配流程收口、适配器初始化返回值和主程序/测试辅助复用路径；不触碰洗车业务规则和调度器业务语义。
- 以目标驱动执行：每一步都围绕“唯一正式入口、显式启动失败、统一回滚、统一销毁、测试复用装配路径”定义验证项。
- 嵌入式 C 规范：公开接口需补齐中文 Doxygen；错误返回统一走 `operation_result_t`；保持显式状态边界和资源所有权约束。
- 中文规范：计划、研究、数据模型、契约和 quickstart 全部使用中文，仅保留必要标识符与路径。

**宪章豁免**：无

## 假设、问题与权衡

**假设**

- 当前主控仍采用单进程、阻塞式 scheduler run loop，因此正式生命周期定义为 `create/run/destroy` 已足够贴合现状。
- 同一进程同一时刻只允许存在 1 个正式 runtime 实例；如果已有实例尚未销毁，新的 `create` 必须显式失败。
- `run` 是一次性运行动作；无论其因正常退出还是失败退出返回，原实例都不得再次进入运行。
- `run` 的正常退出应返回成功结果，只有运行期错误、生命周期误用或内部故障才返回失败。
- 驱动上下文、stdio 绑定、部分测试桩与外部配置路径由调用方提供；runtime 只绑定它们，不拥有也不销毁调用方对象。
- `system_context_t` 已经收紧为组合根运行资源，本期无需再次调整其业务字段语义，只需把更上层装配与生命周期闭环正式化。

**未决问题**

- 无。当前阶段可以直接进入设计与任务拆解。

**被拒绝的方案**

- 直接在 `main.c` 和 `tests/test_support.h` 各自继续维护一套装配逻辑：会让主路径与测试路径再次分叉，破坏本轮目标。
- 现在就定义 `init/start/stop/destroy`：会制造异步运行时假象，与当前阻塞式 `run` 模型不匹配。
- 让 runtime 默认销毁所有绑定进来的适配器或驱动对象：会混淆 `runtime owned` 与 `caller owned`，扩大释放责任面。
- 把 runtime 完全下沉到 `platform/linux`：分层更纯，但当前目标是先把主控骨架收口，放在 `application/coordinators` 改动更小且更贴近现状。

**最小可行方案**

- 新增 `controller_runtime` 公开接口，至少包含 `create/run/destroy`，以及仅用于生命周期状态、装配结果和失败回滚验证的最小只读观测能力。
- 在 `create` 内统一完成 `system_context` acquire、端口/仓储/日志装配、scheduler 创建，并在任一失败点触发统一逆序回滚。
- 在 `create` 内强制执行单实例约束；若已有正式实例处于已创建未销毁状态，则返回明确失败且不影响现有实例。
- 把 `file_program_repository_init`、`file_event_logger_init` 改成返回 `operation_result_t`，纳入 create 错误传播链。
- 让 `main.c` 和测试辅助复用同一套 runtime 装配流程，只保留调用方注入差异。
- 为正常创建、失败回滚、重复销毁安全、`run` 终态语义、正常退出成功语义和测试复用主路径补齐契约型验证。

## 资源所有权边界

- `runtime owned`
  - `system_context_t` 句柄占用权
  - `controller_scheduler_t *`
  - runtime 自己维护的生命周期状态与清理顺序
- `caller owned`
  - 驱动上下文与其底层存储
  - `sensor_port_t` / `actuator_port_t` 绑定源对象
  - stdio 句柄
  - 测试桩对象与测试注入参数
  - 配置路径、日志路径等外部参数

runtime 可以绑定和解除绑定调用方资源，但不能假定自己拥有其销毁权。

## 生命周期与失败回滚

- `create`
  - 输入：完整配置、调用方提供的绑定资源、路径、调度参数
  - 输出：成功时进入“已创建未运行”状态；失败时返回明确 `operation_result_t`
  - 要求：成功返回前完成全部正式装配，不把关键失败延后到 `run`
  - 约束：若同一进程中已有正式 runtime 实例尚未销毁，则新的 `create` 必须显式失败
- `run`
  - 只负责进入正式运行
  - 不再补做装配、绑定或资源创建
  - 正常退出返回成功；错误退出返回失败
  - 返回后实例进入终态，不允许再次 `run`
- `destroy`
  - 逆序清理 runtime owned 资源
  - 允许处理空对象、部分创建成功对象和已销毁对象
  - 至少做到重复调用安全，优先做到幂等

统一失败回滚顺序遵循“谁后创建谁先销毁”，并复用 `destroy` 语义，避免每个失败点各写一套清理分支。

## 范围边界

- 会改：`include/application/coordinators/controller_runtime.h`、`src/application/coordinators/controller_runtime.c`、`src/main/main.c`、`tests/test_support.h`、`include/adapters/outbound/file_program_repository.h`、`src/adapters/outbound/file_program_repository.c`、`include/adapters/logging/file_event_logger.h`、`src/adapters/logging/file_event_logger.c`、相关 scheduler 装配调用点、相关测试与 CMake 入口
- 不会改：洗车工步段模型、领域服务规则、JSON 结构与工艺规则语义、故障策略重设计、真机驱动扩展、界面层

## 嵌入式 C 安全计划

**是否适用**：是  
**命名与分层**：继续使用 `snake_case`、宏全大写、类型 `_t` 结尾；公开 runtime 接口放在 `include/application/coordinators/`，实现细节留在 `src/application/coordinators/`  
**格式与常量**：4 空格缩进、禁 Tab、控制语句带花括号；运行状态、默认参数和错误码使用具名常量或枚举  
**错误策略**：创建失败、参数缺失、绑定缺失、适配器初始化失败、scheduler 创建失败、单实例冲突和重复运行都返回明确 `operation_result_t`；正常退出不是失败；不得静默降级  
**等待策略**：本期不新增等待模型；`run` 仍沿用阻塞式调度循环；不引入异步停止与 join  
**中断策略**：不适用；本特性不新增 ISR 或硬件中断逻辑  
**并发与内存安全**：沿用当前单进程主控模型；所有输入指针、路径和绑定对象在 create 前显式校验；destroy 后句柄不得继续表现为有效绑定  
**状态机**：runtime 生命周期至少覆盖“未创建 -> 已创建未运行 -> 运行中 -> 运行已返回终态 -> 已销毁”；部分创建成功对象也必须能回到“已销毁/未占用”，且终态对象不得再次进入运行  
**注释风格**：新增或修改的公开接口补齐中文 Doxygen；复杂回滚和销毁顺序仅在必要处写简洁中文注释

## 项目结构

### 本功能文档
```text
specs/010-controller-runtime-bootstrap/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
|   |-- runtime-lifecycle-contract.md
|   |-- startup-rollback-contract.md
|   `-- shared-bootstrap-contract.md
`-- tasks.md
```

### 源码结构（仓库根目录）
```text
src/
|-- adapters/
|   |-- config/
|   |-- inbound/
|   |-- logging/
|   `-- outbound/
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
`-- shared/

tests/
|-- contract/
|-- integration/
|-- simulation/
`-- unit/
```

**结构决策**：保持现有单项目 C 工程结构不变。正式 runtime 入口先务实地放在 `application/coordinators`，作为面向当前主控可执行骨架的组合根实现；Linux scheduler 与驱动细节继续留在 `platform/linux` 和 `platform/drivers`，但不直接泄漏到 runtime 的高层语义中。

## 设计后复核结果

- Phase 0 已明确 `create/run/destroy` 是贴合当前阻塞式运行模型的最小生命周期，并补上“单正式实例”“`run` 一次性”“正常退出算成功”三个约束，不再保留异步 `start/stop` 的开放口子。
- Phase 1 已把 runtime owned / caller owned 边界、适配器初始化返回值、统一回滚链路和终态运行语义正式化，足以支撑后续任务拆解。
- 计划保持变更聚焦在 bootstrap 闭环、资源所有权和失败路径，不扩大 `system_context_t` 职责，也不触碰洗车业务语义。

## 复杂度跟踪

- 无新增宪章豁免。
