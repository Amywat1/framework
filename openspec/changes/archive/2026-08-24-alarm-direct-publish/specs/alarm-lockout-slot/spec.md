# alarm-lockout-slot Specification（本变更 delta）

驱逐语义不变；CLEARED / TRIGGERED 改为总线事件，不再经 pending 域事件队列。

## MODIFIED Requirements

### Requirement: lockout 等级在活跃池满时优先准入
当活动表已达 `ALARM_ACTIVE_MAX` 且新码尚未活跃时，若该码等级 `alarm_level_forces_lockout` 为真，实现 MUST 尝试驱逐一条非 lockout 活动条目后再插入；被驱逐条目 MUST 发出 `EVT_ALARM_CLEARED`（param 为被驱逐码）并记错误级日志。驱逐选择 MUST 先取「不阻塞开洗」的最早触发条目（MINOR），再取其余非 lockout 的最早触发条目（MAJOR）。若活动表已全部为 lockout 等级，MUST 拒绝新码并返回 `SW_ERR_OVERFLOW`，MUST NOT 驱逐 lockout 条目。非 lockout 新码在池满时 MUST 仍返回 `SW_ERR_OVERFLOW` 且不驱逐。未知等级按矩阵最严格值视为 lockout。本函数返回前 MUST 已将本次产生的 CLEARED / TRIGGERED（及若有的姿态边沿）入队。

#### Scenario: 池满 MINOR 时 CRITICAL 插入并驱逐最早 MINOR
- **GIVEN** 活动表已有 `ALARM_ACTIVE_MAX` 条 MINOR，各 `triggered_at_ms` 不同
- **WHEN** `alarm_registry_trigger` 一条尚未活跃的 CRITICAL
- **THEN** 返回 `SW_OK`；活动表仍为 `ALARM_ACTIVE_MAX` 条且含该 CRITICAL；`triggered_at_ms` 最早的那条 MINOR MUST 已删除；返回前总线 MUST 含该 MINOR 的 `EVT_ALARM_CLEARED` 与该 CRITICAL 的 `EVT_ALARM_TRIGGERED` 以及 `EVT_SAFETY_LOCKOUT`；姿态 MUST 为 LOCKOUT
- **TIMING** 上述入队 MUST 在 trigger 返回前完成

#### Scenario: 仅有 MAJOR 可驱逐时驱逐最早 MAJOR
- **GIVEN** 活动表满员且均为 MAJOR（blocking 但不 lockout）
- **WHEN** trigger 一条新 CRITICAL
- **THEN** 返回 `SW_OK`；最早触发的 MAJOR MUST 被 CLEARED；新 CRITICAL MUST 活跃

#### Scenario: 全是 CRITICAL 时拒绝新 CRITICAL
- **GIVEN** 活动表满员且均为 CRITICAL
- **WHEN** trigger 另一条尚未活跃的 CRITICAL
- **THEN** 返回 `SW_ERR_OVERFLOW`；活动表 MUST 不变；MUST NOT 产生 CLEARED

#### Scenario: 池满时新 MINOR 仍被拒绝
- **GIVEN** 活动表满员（任意等级组合）
- **WHEN** trigger 一条尚未活跃的 MINOR
- **THEN** 返回 `SW_ERR_OVERFLOW`；MUST NOT 驱逐任何条目

#### Scenario: 已活跃的 CRITICAL 在池满时仍只刷新条件
- **GIVEN** 活动表满员且含码 A（CRITICAL）
- **WHEN** 再次 trigger A
- **THEN** 返回 `SW_OK`；MUST NOT 驱逐；MUST NOT 再入队 TRIGGERED；`condition_active` MUST 为 true

---

## Invariants

- **INV-01**: 活动表条目数 MUST always ≤ `ALARM_ACTIVE_MAX`。
- **INV-02**: 存在 `forces_lockout` 活跃条目时，安全视图的姿态 MUST 为 LOCKOUT。
- **INV-03**: 因腾槽删除的条目 MUST 走与正常清除相同的 `EVT_ALARM_CLEARED` 路径，MUST NOT 无事件消失。
- **INV-04**: lockout 条目 MUST NOT 被另一 lockout 或非 lockout 插入驱逐。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 活动报警 | `ALARM_ACTIVE_MAX`（32） | 非 lockout 硬失败；lockout 可驱逐非 lockout；全 lockout 硬失败 | 与目录一半对齐；锁存 CRITICAL 不得被诊断级报警挡住 |
