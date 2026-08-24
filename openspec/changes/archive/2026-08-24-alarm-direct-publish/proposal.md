## Why

报警变位要经过 pending 队列、50ms 周期 drain、LOCKOUT 立即 drain、drain 互斥和 `EVT_ALARM_RESYNC` 才能上总线，而命令路径早已同步读 registry。这条管道是报警域自己加的特例：分层表允许 domain 发布事件，`operational_mode` 已经「不订阅、放锁后 `event_publish`」。管道让「条件成立」到「事件入队」的路径难懂，且 MINOR 与 CRITICAL 时效规则不一致。开发期应取消该特例，把报警拉回仓级惯例。

## What Changes

- `alarm_registry` 在 `trigger` / `clear` / `reset_all` / `reevaluate_group` 放锁后直接 `event_publish_required`：逐码 `EVT_ALARM_*`，姿态边沿 `EVT_SAFETY_*`。持锁期间不得调用总线。
- **BREAKING**：删除 pending 队列、`alarm_registry_pull_events`、50ms 周期 drain、`alarm_bridge_drain` 导出接口、`EVT_ALARM_RESYNC`。MINOR / MAJOR / 回到 NOMINAL 均在调用返回前入队，不再等待周期任务。
- `alarm_binding_bridge` 并入 `alarm_bridge`：只注册入站端口、开关会话 journal、可选 ON_MOTION 查表。detector 仍只走 `alarm_binding_port`。
- 安全投影一次持锁读出活动表、blocking、最高码、姿态与会话 journal（结构体，不再五参数可选指针）。
- `op_mode_on_critical_alarm` 与 `on_blocking_alarm` 合并；`EVT_SAFETY_LOCKOUT` 只驱动 `abort_wash`。
- 不改：三等级 × 三清除、`safety_matrix`、lockout 驱逐、journal 不参与洗后 STOPPED、急停 cutout 不进通用 trigger、入队不等于机构已停。

## Capabilities

### New Capabilities

- `alarm-event-publish`：registry 放锁后发布报警与姿态边沿；无 pending / drain / RESYNC；采集线程只入队不 abort / cutout。
- `alarm-safety-view`：一次持锁的安全视图结构体（含 journal）；单项查询为薄封装。

### Modified Capabilities

- `alarm-lockout-drain`：立即 drain 与周期 drain 退役；LOCKOUT 入队时效改由 `alarm-event-publish` 保证。
- `alarm-lockout-slot`：驱逐产生的 CLEARED 改为总线事件，不再经 pending。
- `alarm-session-journal`：journal 并入同一次安全视图持锁；删去与 pending「取出即清」的对照。

## Impact

- `domain/safety/alarm_registry/`：删除 pending；放锁后发布；引入 `alarm_safety_view_t`。
- `application/bridges/alarm_bridge.*`、`alarm_binding_bridge.*`：合文件；去掉 drain 与周期任务。
- `common/event_types.h`：删除 `EVT_ALARM_RESYNC`。
- `application/bridges/op_mode_bridge.c`、`domain/op_mode/operational_mode.c`：单一 blocking 收敛入口。
- `application/telemetry_projection.c`：改读 view；取消 RESYNC 订阅。
- 测试：`test_alarm_event_bridge` / concurrent drain / RESYNC / binding 立即 drain 按新契约改写；registry 与 journal、驱逐用例改为断言总线事件。
- 文档：报警模块设计、架构 05、行为契约 ALRM-13/14/18/19/22、整治记录。
- 不改 `safety_port` / `safety_output_hold` / 急停热路径。
