# alarm-safety-view Specification

一次持锁读出活动表、阻塞标志、最高码、安全姿态与会话 journal，避免拼接多次查询或活动表与 journal 取自不同时刻。

## ADDED Requirements

### Requirement: 安全视图一次持锁
框架 MUST 提供 `alarm_safety_view_t`（或等价结构体），字段至少包含：活动实例数组与条数、`blocking`、最高等级报警码、`posture`、会话 journal 码表与条数、`journal_dropped`。`alarm_registry_copy_safety_view`（或替换它的单一拷贝入口）MUST 在同一次持锁内填充上述字段。调用方需要多项时 MUST 走该入口，MUST NOT 再拼多次单项查询来组装快照。

#### Scenario: 活动表与姿态来自同一时刻
- **GIVEN** 活动表含一条 MAJOR 与一条 CRITICAL
- **WHEN** 拷贝安全视图
- **THEN** `count>=2`，`blocking==true`，`posture==LOCKOUT`，`top_code` 为其中最高等级那条的码；四项与 journal 字段 MUST 可同时断言且无中间态

#### Scenario: 空表视图
- **GIVEN** 无活动报警且无会话 journal
- **WHEN** 拷贝安全视图
- **THEN** `count==0`，`blocking==false`，`posture==NOMINAL`，`top_code==ALARM_CODE_NONE`，journal 条数与丢弃均为 0

#### Scenario: 驱逐过程中并发拷贝不越界
- **GIVEN** 活动表满员 MINOR，另一线程正在 trigger CRITICAL 以驱逐腾槽
- **WHEN** 并发多次拷贝安全视图
- **THEN** `count` MUST always ≤ `ALARM_ACTIVE_MAX`；若 `posture==LOCKOUT` 则活动表中 MUST 存在 `forces_lockout` 条目（与 ALRM-17 同构）

### Requirement: 单项查询与视图一致
`alarm_registry_has_blocking_active()` 与 `alarm_registry_safety_posture()` MUST 与同一次视图中的 `blocking` / `posture` 语义一致（可薄封装同一持锁扫描）。遥测投影刷新安全快照时 MUST 从该视图填充活动表、blocking、最高码、姿态与 journal 字段，MUST NOT 再分两次取锁分别拷活动表与 journal。

#### Scenario: 投影一次刷新带出 journal
- **GIVEN** 会话中已记录 2 个 MAJOR，丢弃计数为 0
- **WHEN** 遥测投影刷新安全快照
- **THEN** 快照活动表、blocking、journal 条数 2 与丢弃 0 MUST 来自同一次 registry 持锁拷贝

#### Scenario: has_blocking 与视图字段一致
- **GIVEN** 仅一条 MAJOR 活跃
- **WHEN** 先拷贝视图再读 `has_blocking_active`（中间无其他写）
- **THEN** 两者 MUST 均为 true；`posture` MUST 为 NOMINAL

---

## Invariants

- **INV-01**: 视图中 `count` MUST always ≤ `ALARM_ACTIVE_MAX`。
- **INV-02**: `posture==LOCKOUT` ⇔ 活动表中存在 `alarm_level_forces_lockout` 为真的条目。
- **INV-03**: `blocking==true` ⇔ 活动表中存在 `alarm_level_blocks_wash` 为真的条目。
- **INV-04**: journal 字段 MUST 与 `alarm-session-journal` 的计数、去重、不清零语义一致；MUST NOT 因拷贝视图而清零 `journal_dropped`。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 视图活动表拷贝 | `ALARM_ACTIVE_MAX`（32） | 截断列表但不截断聚合字段 | 与现网 `copy_safety_view` 一致 |
| 视图 journal 拷贝 | `ALARM_SESSION_JOURNAL_MAX`（16） | 与 registry journal 满池策略相同 | 见 `alarm-session-journal` |
