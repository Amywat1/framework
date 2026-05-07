# 研究记录：控制器运行时收敛

## 决策 1：正式命令只保留 `prepare -> submit -> main_loop_run -> finalize` 单一路径

**Decision**：生产环境命令执行统一采用 `cli_command_adapter_prepare_line()` 解析、`main_loop_submit_trigger()` 入队、`main_loop_run()` 消费、`cli_command_adapter_finalize_trigger_response()` 生成响应的正式链路；不再把 `cli_command_adapter_process_line()` 作为正式路径的一部分保留。  
**Rationale**：
- 当前 `main.c` 已经实现这条正式链路，且它最符合规格定义的目标路径。
- 当前 `cli_command_adapter_process_line()` 直接调用 `start_wash_cycle_execute()`、`stop_wash_cycle_with_reason_execute()`、`acknowledge_fault_execute()` 或直接反馈处理，形成与主循环并行的第二条业务链路。
- 只保留一条正式链可以统一优先级竞争、timeout 入队、最近结果记录和最终响应语义。  
**Alternatives considered**：
- 保留双路径并通过测试约束其行为一致：会长期增加理解与维护成本。
- 彻底移除解析/入队分步接口，只留一个“全自动处理”入口：会让 `main.c` 难以保留主循环骨架，也不利于测试驱动正式路径。

## 决策 2：`status` 的 stdin 命令进入正式链，但内部只读查询接口继续保留

**Decision**：`status` 作为正式 stdin 命令也必须走统一受理路径；同时保留 `query_wash_session_status_execute()` 作为内部只读查询接口，供测试和内部模块复用。  
**Rationale**：
- 澄清结果已经固定：正式 stdin 不允许例外分支，而查询入口的独立语义仍需保留。
- 当前 `execute_status()` 已经建立了从只读查询接口到单行响应的映射，复用成本低。
- 这种分层方式能同时满足“正式单路径”和“只读查询不承担写职责”。  
**Alternatives considered**：
- 让 `status` 继续完全绕过正式路径：违反正式 stdin 单路径目标。
- 删除内部只读查询接口：会让测试和内部观测只能依赖字符串响应，降低可验证性。

## 决策 3：薄写 use_case 收敛，`process_wash_trigger` 保持唯一业务分发核心

**Decision**：`start_wash_cycle.c`、`stop_wash_cycle.c`、`acknowledge_fault.c` 这类仅负责构造 `wash_trigger_event` 并立即转调 `process_wash_trigger_execute()` 的薄写 use_case，不再承担主业务入口角色；本轮按“保留文件但降级为兼容性辅助包装”处理，正式写路径统一收敛到 trigger 受理和 `process_wash_trigger_execute()`。  
**Rationale**：
- 当前三个文件都只是“组 trigger + 直调核心”的薄转发层，已经造成正式路径外的第二套写入口。
- 收敛后，维护者阅读 `start/stop/fault` 主链路时会减少跨文件跳转。
- `query_wash_session_status_execute()` 仍保留为独立只读查询入口，满足规格中的查询语义要求。  
**Alternatives considered**：
- 保留这些 use_case，但要求测试少用：无法从结构上消除第二套主链路。
- 新增更高一层命令协调器来替换它们：对当前范围来说过度设计。

## 决策 4：最近结果、日志和响应语义统一由少数记录辅助逻辑驱动

**Decision**：把 `last_result_code`、`last_reason_code`、`last_transition_record`、日志调用和 CLI 响应之间的关系统一到少数记录辅助逻辑；接受、拒绝和忽略等既有事件结论按统一规则刷新最近结果。  
**Rationale**：
- 当前 `cli_command_adapter.c` 与 `process_wash_trigger.c` 各自维护一部分结果记录逻辑，存在语义分散。
- 澄清结果要求最近结果必须代表“最近一次被系统处理的事件”，因此需要把刷新规则从分散分支收口到统一辅助逻辑。
- 统一记录来源后，`status`、日志和命令响应才能稳定对齐。  
**Alternatives considered**：
- 仅统一响应字符串，不统一最近结果写法：仍会留下 `status` 与日志不一致风险。
- 新建完整事件总线：超出本次范围。

## 决策 5：状态写入责任按状态类别收口，`global_fault` 只允许高层策略设置/清除

**Decision**：
- `wash_session_state_machine` 只负责 session 迁移；
- `wash_execution_service` 只负责 execution/wait 推进；
- `wait_timeout_service` 只负责 timeout 裁决；
- `process_wash_trigger_execute()` 只负责路由、高层策略以及 `global_fault` 的 set/clear。  
**Rationale**：
- 当前 `process_wash_trigger.c` 直接修改 `global_fault_present/global_fault_code/global_fault_reason`，这与澄清结果一致，应保留为唯一高层策略写入口。
- `handle_start()` 里既有 precheck、program capture、session start、execution reset/begin next stage，最需要通过职责边界避免后续继续膨胀。
- 如果不在设计阶段明确边界，后续加入维护、恢复或更多 fault 规则时会再次出现多处直接改状态。  
**Alternatives considered**：
- 把 `global_fault` 下放给 fault service：会引入新的责任中心，不符合最小方案。
- 允许主循环层直接写 `global_fault`：会让平台层越界承担业务策略。

## 决策 6：`fault clear` 在无 `global_fault` 时沿用既有外部语义，本轮不重定义

**Decision**：当不存在 `global_fault` 时收到 `fault clear`，本轮不新增也不改写其外部语义定义；实现只要求该场景继续沿用当前既有行为，并纳入统一路径与统一记录框架。  
**Rationale**：
- 本特性的目标是收敛入口、职责和记录方式，而不是借设计文档重新发明命令语义。
- 将该场景写成新的显式语义会与“六类命令响应语义保持不变”的边界冲突。
- 只要求它进入统一路径和统一记录框架，足以支持本轮实现与回归验证。  
**Alternatives considered**：
- 在计划阶段显式规定新的忽略/成功/错误结果：都会把本轮范围从收敛扩展成外部语义变更。

## 决策 7：`main.c` 变薄但不改变 `select + timeout + stdin` 运行模型

**Decision**：保留 `main.c` 的单进程主循环骨架，但把命令读取细节、等待/排空协调细节和运行时驱动细节下沉到辅助模块或辅助函数。  
**Rationale**：
- 当前 `main.c` 同时承担初始化、select 等待、时间推进、stdin 处理、trigger 等待消费和 runtime drain，已经开始成为变化热点。
- 规格明确不允许改成多线程或 socket/IPC，也不允许改变 select 模型，因此只能做职责拆分而不是运行模型替换。
- 这种拆法可以直接服务后续实现，而不改变行为边界。  
**Alternatives considered**：
- 维持当前 `main.c` 结构：可运行，但后续演进成本会持续增加。
- 直接改成事件循环框架或异步模型：超出当前范围，也违反“不改运行模型”的约束。
