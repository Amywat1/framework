# Phase 1 数据模型：平台层调度器收敛

## 1. `controller_scheduler_config`

**用途**  
描述调度器启动所需的全部平台语义配置，不暴露底层 `fd` 或 `epoll` 细节。

**核心字段**

- `control_period_ms`：控制周期，必须大于 0
- `command_event_source_enabled`：是否启用命令事件源
- `notification_event_source_enabled`：是否启用通知事件源
- `exit_event_source_enabled`：是否启用退出事件源
- `bounded_drain_ticks`：退出阶段允许额外执行的最大单拍次数
- `max_triggers_per_tick`：单拍前后允许消费的最大触发数量上界
- `overrun_warning_threshold_ms`：判定超拍的阈值
- `observability_enabled`：是否输出调度指标

**校验规则**

- `control_period_ms` 必须非零
- `bounded_drain_ticks` 和 `max_triggers_per_tick` 必须为有限正值
- 至少允许一个退出路径可用，防止调度器不可关闭

## 2. `controller_scheduler_runtime`

**用途**  
Linux 平台层私有运行壳对象，拥有等待机制、事件源注册、退出状态和观测统计。

**核心字段**

- `runtime_state`：`initialized / running / draining / stopped / failed`
- `epoll_fd`：事件分发中心
- `timer_fd`：固定控制周期源
- `wakeup_fd`：采集线程或通知路径唤醒句柄
- `event_sources[]`：外部事件源注册表
- `exit_requested`：是否收到退出请求
- `last_error_code`：最近调度器错误类别
- `metrics`：运行时统计结构
- `bound_system_context`：被调度的 `system_context_t`

**状态迁移**

- `initialized -> running`：调度器创建成功并启动
- `running -> draining`：收到退出事件，且配置要求有限排空
- `running -> failed`：周期源、等待原语或单拍推进出现不可恢复错误
- `draining -> stopped`：有界排空完成
- `draining -> failed`：排空期间发生不可恢复错误
- `running -> stopped`：直接退出策略触发

## 3. `scheduler_event_source_descriptor`

**用途**  
统一描述命令、通知、退出三类外部事件源在调度器中的注册信息。

**核心字段**

- `source_id`：稳定标识
- `source_kind`：`command / notification / exit`
- `source_state`：`enabled / disabled / degraded / closed`
- `wake_mode`：`epoll_fd / eventfd / direct_poll`
- `priority_class`：调度优先级分类
- `trigger_count`：累计触发次数
- `last_seen_time_ms`：最近触发时间
- `failure_policy`：关闭或读取失败时的处理策略

**关系**

- 一个 `controller_scheduler_runtime` 包含多个 `scheduler_event_source_descriptor`
- 每个事件源通过统一入口把事件交给调度器，而不是直接操作领域状态

## 4. `scheduler_notification_snapshot`

**用途**  
承载采集侧或驱动侧维护的“最新事实缓存”，供控制周期内领域统一读取。

**核心字段**

- `snapshot_version`：版本号或单调递增计数
- `captured_time_ms`：快照生成时间
- `sensor_values`：最新采样值
- `fault_flags`：外设层检测到的原始故障标志
- `dirty_flag`：是否存在待消费更新

**校验规则**

- 快照更新只能来自采集侧/驱动侧
- 快照被读取不应自动改变会话或执行状态
- 若由辅助线程更新，必须有显式同步边界

## 5. `scheduler_metrics`

**用途**  
记录调度器观测指标，支持日志输出和测试断言。

**核心字段**

- `cycle_count`
- `overrun_count`
- `consecutive_overrun_count`
- `command_event_count`
- `notification_event_count`
- `exit_event_count`
- `pending_trigger_count`
- `last_error_reason`

**派生规则**

- `pending_trigger_count` 从 `system_context` 或受控队列状态投影而来
- `consecutive_overrun_count` 在非超拍周期清零
- `last_error_reason` 只保留最近一次正式错误结论

## 6. `controller_runtime_state_view`

**用途**  
对外暴露只读调度视图，供状态输出、日志或测试读取。

**核心字段**

- `runtime_state`
- `control_period_ms`
- `last_cycle_start_ms`
- `last_cycle_duration_ms`
- `metrics`
- `enabled_sources[]`

**校验规则**

- 只读查询不得修改调度器状态或领域状态
- 视图内容必须足够支撑超拍、事件源和退出行为诊断

## 7. 与现有 `system_context_t` 的关系

**保留在 `system_context_t` 的内容**

- 业务运行事实
- 领域状态机相关对象
- 共享端口
- 当前运行时间、待处理触发和最近结果投影等现有运行事实

**迁移到调度器私有运行时的内容**

- `epoll` / `timerfd` / `eventfd`
- 事件源注册和启停状态
- 退出标志和排空计数
- 节拍统计与最近调度器错误

**边界原则**

- `system_context_t` 不承载 OS 对象
- 调度器运行时不直接拥有领域状态机，只通过 `main_loop_*()` 驱动
