# alarm-lockout-drain Specification

## Purpose

立即 drain 与 50ms 周期 drain 已退役。LOCKOUT（以及其余变位）在调用返回前入队由 `alarm-event-publish` 保证。命令路径仍同步读 registry；abort_wash / cutout 仍在 dispatch 线程执行。

## Requirements

### Requirement: 不得再排空 pending
实现 MUST NOT 注册报警 pending 排空周期任务，MUST NOT 导出 `alarm_bridge_drain`。经 `alarm_binding_port` 的 trigger/clear 成功后，MUST NOT 再调用 drain。LOCKOUT 入队时效见 `alarm-event-publish`「首次 CRITICAL 触发发布 LOCKOUT」。采集线程仍不得 abort/cutout。

#### Scenario: 不再存在周期 drain 依赖
- **GIVEN** 本能力已落地，未注册报警 drain 周期任务
- **WHEN** 经 binding trigger 一条 CRITICAL
- **THEN** 返回前总线 MUST 已含 `EVT_SAFETY_LOCKOUT`；MUST NOT 调用已删除的 `alarm_bridge_drain`
- **TIMING** 与 `alarm-event-publish` 相同：函数返回前入队

---

## Invariants

- **INV-01**: 实现 MUST NOT 再注册名为报警 pending 排空的周期任务。
- **INV-02**: 原「registry 不得 include event_bus」不再适用；改为 MUST NOT include `application/`（见 `alarm-event-publish` INV-01）。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| drain 周期任务 | 0（删除） | 无 | 变位函数自身发布，见 `alarm-event-publish` |
