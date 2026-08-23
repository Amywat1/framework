# alarm-lockout-drain Specification

## Purpose

经报警绑定端口成功触发或清除后，若安全姿态已为 LOCKOUT，在返回调用方之前排空待发域事件并入队 `EVT_SAFETY_LOCKOUT`，不再等待 50ms 周期 drain。命令路径仍同步读 registry；abort_wash / cutout 仍在 dispatch 线程执行。

## Requirements

### Requirement: LOCKOUT 姿态下 binding 触发立即排空
经 `alarm_binding_port` 的 `trigger` 或 `clear` 成功返回后，若此时 `alarm_registry_safety_posture()` 为 `SAFETY_POSTURE_LOCKOUT`，实现 MUST 在返回调用方之前完成一次 `alarm_bridge_drain`，将待发域事件与姿态边沿发布入事件总线队列。50ms 周期 drain MUST 仍注册，作为不经 binding 的 registry 变更兜底。domain registry MUST NOT 直接依赖 event_bus。

#### Scenario: 首次 CRITICAL 触发后 LOCKOUT 事件已入队
- **GIVEN** 目录含一条 CRITICAL 报警且当前姿态为 NOMINAL，周期 drain 被暂停或尚未到期
- **WHEN** 经 `alarm_binding_port.trigger` 上报该码成功
- **THEN** 在 trigger 返回前，事件总线队列中 MUST 含 `EVT_ALARM_TRIGGERED`（param 为该码）以及 `EVT_SAFETY_LOCKOUT`；registry 姿态 MUST 为 LOCKOUT
- **TIMING** 上述入队 MUST 发生在 trigger 返回之前，不得等待 50ms 周期任务

#### Scenario: 已处于 LOCKOUT 时再触发另一条 CRITICAL
- **GIVEN** 已有一条 CRITICAL 活跃且姿态为 LOCKOUT
- **WHEN** 经 binding 成功 trigger 另一条 CRITICAL
- **THEN** trigger 返回前 MUST 已 drain，队列中 MUST 含新码的 `EVT_ALARM_TRIGGERED`；MUST NOT 再发布第二条 `EVT_SAFETY_LOCKOUT`

#### Scenario: MINOR 触发不立即 drain
- **GIVEN** 姿态为 NOMINAL，目录含 MINOR 与 CRITICAL
- **WHEN** 经 binding 成功 trigger 该 MINOR
- **THEN** trigger 返回时 MUST NOT 依赖立即 drain 才能看到 `EVT_ALARM_TRIGGERED`；该事件 MAY 留在 registry pending 直到周期 drain。姿态 MUST 仍为 NOMINAL

#### Scenario: 清除最后一条 CRITICAL 不要求立即 NOMINAL 入队
- **GIVEN** 仅一条 AUTO_STATIC CRITICAL 活跃
- **WHEN** 经 binding 成功 clear 该码，姿态变为 NOMINAL
- **THEN** `EVT_SAFETY_NOMINAL` MAY 延迟到下一次周期 drain 入队；在入队前命令路径读 registry MUST 已看到 NOMINAL

#### Scenario: 周期任务与 binding 并发 drain
- **GIVEN** 50ms 周期任务正在运行且测试或 wiring 同时从另一线程经 binding trigger 一条 CRITICAL
- **WHEN** 两路 drain 重叠
- **THEN** 临界区 MUST NOT 重叠；一次 CRITICAL 边沿 MUST 只发布一条 `EVT_SAFETY_LOCKOUT`
- **TIMING** 与 ALRM-19 相同：锁整个 drain，边沿顺序不得倒置

## Invariants

- **INV-01**: `alarm_registry` MUST NOT include event_bus 或 `alarm_bridge` 头文件。
- **INV-02**: 立即 drain 与周期 drain MUST 共用同一把 drain 锁，MUST NOT 并行发布姿态边沿。
- **INV-03**: 立即 drain MUST 只入队事件，MUST NOT 在采集线程同步执行 `abort_wash` 或 cutout。
- **INV-04**: 当姿态为 LOCKOUT 且本次 binding trigger/clear 成功时，返回时 pending 队列 MUST 已被该次 drain 抽空或仅留下超出 `ALARM_DRAIN_MAX_ROUNDS` 的剩余（与现有轮次上限一致）。

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| drain 轮次 | `ALARM_DRAIN_MAX_ROUNDS`（4） | 超出留待下次 drain，不丢 | 与现网一致，防止抖动时周期任务不返回 |
| 周期 drain | 50ms | 兜底非 binding 路径 | 现网 `periodic_task_register` |
