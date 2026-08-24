# alarm-lockout-drain Specification（本变更 delta）

立即 drain 与 50ms 周期 drain 退役。LOCKOUT（以及其余变位）在调用返回前入队改由 `alarm-event-publish` 保证。

## REMOVED Requirements

### Requirement: LOCKOUT 姿态下 binding 触发立即排空
经 `alarm_binding_port` 的 `trigger` 或 `clear` 成功返回后，若此时姿态为 LOCKOUT，实现曾 MUST 在返回前调用 `alarm_bridge_drain`；50ms 周期 drain 曾 MUST 仍注册；registry 曾 MUST NOT 直接依赖 event_bus。

**Reason**: pending + 双路径 drain 是报警域相对仓级惯例的特例。`operational_mode` 已放锁后直接发布；分层表允许 domain 依赖 `runtime/event_bus`。双路径使 MINOR 与 CRITICAL 时效不一致，并引入 drain 互斥与 `EVT_ALARM_RESYNC`。

**Migration**: 删除 `alarm_bridge_drain`、周期 drain 任务与 binding 上的 `drain_if_lockout`。所有活动表变位由 `alarm_registry_*` 放锁后 `event_publish_required`。LOCKOUT 入队时效见 `alarm-event-publish`「首次 CRITICAL 触发发布 LOCKOUT」。采集线程仍不得 abort/cutout（原 INV-03 迁入该能力）。

#### Scenario: 不再存在周期 drain 依赖
- **GIVEN** 本变更已落地，未注册报警 drain 周期任务
- **WHEN** 经 binding trigger 一条 CRITICAL
- **THEN** 返回前总线 MUST 已含 `EVT_SAFETY_LOCKOUT`；MUST NOT 调用已删除的 `alarm_bridge_drain`
- **TIMING** 与 `alarm-event-publish` 相同：函数返回前入队

---

## Invariants

- **INV-01**: 实现 MUST NOT 再注册名为报警 pending 排空的周期任务。
- **INV-02**: 原「registry 不得 include event_bus」不再适用；改为 MUST NOT include `application/`（见 `alarm-event-publish` INV-01）。
