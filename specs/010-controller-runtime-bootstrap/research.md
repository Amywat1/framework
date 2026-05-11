# 研究记录：主控 Runtime/Bootstrap 正式入口

## 决策 1：正式入口新增 `controller_runtime`，先放在 `application/coordinators`

**Decision**  
新增正式 `controller_runtime` 入口对象，并先将其放在 `application/coordinators`，作为当前主控可执行骨架的组合根协调器。

**Rationale**  
当前问题的核心不是平台抽象不够纯，而是主控装配路径分散在 `main.c`、测试辅助和适配器初始化里。先在应用协调层增加一个正式 runtime 入口，可以最小改动地把获取 `system_context`、装配、scheduler 创建、运行和销毁闭环收口起来，同时保持对领域层的依赖方向不变。

**Alternatives considered**  
- 放在 `platform/linux`：分层更纯，但会把“主控装配闭环”过早等同于 Linux 细节，当前阶段改动更大。
- 继续散落在 `main.c` / 测试辅助：实现成本最低，但无法形成正式单一入口，违背本期目标。

## 决策 2：生命周期固定为 `create/run/destroy`

**Decision**  
正式 runtime 生命周期固定为 `create/run/destroy`，本期不引入 `start/stop`。

**Rationale**  
当前 scheduler 运行模型是阻塞式 run loop，没有后台线程、异步 stop 或 join 语义。此时定义 `start/stop` 会让接口看起来像异步运行时，但实现并不具备对应行为。`create/run/destroy` 既贴合现状，也更容易把启动失败和销毁顺序收口清楚。

**Alternatives considered**  
- `init/start/stop/destroy`：语义虚高，容易误导调用方。
- 只有 `run/destroy`：会把创建和运行混在一起，不利于显式创建失败回滚。

## 决策 3：`create` 必须完成全部正式装配并达到“可运行未运行”

**Decision**  
`controller_runtime_create(...)` 成功返回前，必须完成 `system_context` acquire、端口绑定、程序仓储初始化、事件日志初始化和 scheduler 创建，使 runtime 进入“已创建未运行”状态。

**Rationale**  
如果把关键依赖延后到 `run`，启动失败就会分散到多个阶段，回滚链路也会变复杂。把所有正式装配放进 `create`，可以让 `run` 专注于进入运行，让错误路径全部集中在创建阶段。

**Alternatives considered**  
- 在 `run` 里补建 scheduler 或初始化适配器：会让生命周期边界模糊。
- 只做部分 create，剩余由调用方拼装：无法形成唯一正式入口。

## 决策 4：统一错误传播依赖 `operation_result_t`，适配器初始化必须返回结果

**Decision**  
`file_program_repository_init` 与 `file_event_logger_init` 改为返回 `operation_result_t`，并纳入 runtime create 的统一错误传播与回滚链路。

**Rationale**  
当前这两个初始化函数是 `void`，意味着 runtime 无法把“路径错误、文件不可用、初始化失败”作为正式启动失败处理。只有让它们返回显式结果，`create` 才能在失败点统一回滚，而不是留下半初始化状态。

**Alternatives considered**  
- 保持 `void`，只靠日志或隐式状态表示失败：错误不可组合，也不利于测试。
- 引入新的结果类型：没有必要，现有 `operation_result_t` 已足够。

## 决策 5：严格区分 `runtime owned` 与 `caller owned`

**Decision**  
runtime 只销毁自己创建或明确拥有的资源，例如 `system_context` 占用权和 scheduler；驱动上下文、端口源对象、stdio、测试桩和外部路径参数仍由调用方拥有。

**Rationale**  
当前很多装配对象只是“绑定进 runtime”，并不等于 runtime 应该负责其销毁。如果不先固定所有权边界，destroy 很容易演变成“顺手清一切”，导致重复释放或越权释放。

**Alternatives considered**  
- runtime 统一接管全部绑定对象生命周期：接口更大，释放风险更高。
- 完全不记录所有权边界：会让失败回滚和销毁顺序持续模糊。

## 决策 6：`destroy` 统一承担回滚与正常销毁，至少重复调用安全

**Decision**  
`controller_runtime_destroy(...)` 既用于正常销毁，也用于 create 中途失败后的统一回滚；它必须支持空对象、部分创建成功对象和已销毁对象，至少做到重复调用安全。

**Rationale**  
如果每个失败点都自己写清理逻辑，create 的代码会继续膨胀，而且更容易遗漏资源释放。让 destroy 承担统一逆序清理职责，可以把失败路径和正常路径合并到同一套闭环。

**Alternatives considered**  
- 每个 create 失败点单独手写回滚：重复且易漏。
- destroy 只允许处理完全创建成功对象：无法支持统一失败回滚。

## 决策 7：同一进程同一时刻只允许 1 个正式 runtime 实例

**Decision**  
正式 runtime 在同一进程内固定为单实例；当已有实例处于已创建未销毁状态时，新的 `controller_runtime_create(...)` 必须显式失败。

**Rationale**  
当前主控是单进程、单主循环骨架，system_context、scheduler 和正式启动路径都围绕一个主控实例设计。此时允许多个正式 runtime 并存，只会把生命周期、资源边界和回滚语义迅速复杂化，不符合本轮“先把骨架收紧”的目标。

**Alternatives considered**  
- 允许多个正式 runtime 并存：会扩大 system_context 和 scheduler 的隔离问题，当前阶段收益低、风险高。
- 正式入口单实例、测试入口多实例：会让正式路径与测试路径再度分叉。

## 决策 8：`run` 是一次性动作，正常退出返回成功

**Decision**  
`controller_runtime_run(...)` 是一次性运行入口；一旦返回，原实例就进入只允许 `destroy` 的终态。若按正常退出条件返回，则结果判定为成功；只有运行期错误、生命周期误用或内部故障才判定为失败。

**Rationale**  
这与当前阻塞式 run loop 最匹配，也能避免把“正常退出”误记成错误，或者让调用方误以为同一实例可以重复启动。把 `run` 返回后的状态收成终态，可以显著简化状态机和回归验证。

**Alternatives considered**  
- `run` 返回后允许再次运行同一实例：会引入重新装配、旧状态复用和重复调度边界问题。
- 任何 `run` 返回都算失败：会把主动退出或正常结束错误地折叠到异常路径里。
