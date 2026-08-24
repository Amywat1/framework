# alarm-session-journal Specification（本变更 delta）

journal 写入、满池丢弃、读取不清零、不参与洗后 STOPPED 的语义不变。拷贝改为与活动表同一次持锁（见 `alarm-safety-view`）。删除与 pending「取出即清」的对照——pending 已退役。

## MODIFIED Requirements

### Requirement: 安全快照包含会话 journal
`safety_snapshot_t` MUST 包含本会话 journal 码表、条数与丢弃计数。遥测投影在刷新安全快照时 MUST 从 registry 的一次安全视图拷贝上述字段（与活动表、blocking、姿态同一把锁）。读接口拷贝码表时 MUST 同时给出当前累计丢弃数，且 MUST NOT 因读取而清零丢弃计数。洗后是否进入 STOPPED MUST NOT 以 journal 为准，MUST 仍以当前 `has_blocking_active` 为准。

#### Scenario: 投影刷新带出 journal
- **GIVEN** 会话中已记录 2 个 MAJOR 码且丢弃计数为 0
- **WHEN** 遥测投影刷新安全快照
- **THEN** 快照中 `session_journal_count` 为 2，两个码与 registry 一致，`session_journal_dropped` 为 0

#### Scenario: 快照可见丢弃计数
- **GIVEN** 会话 journal 已满后又触发至少 1 条新 MAJOR，丢弃计数 ≥ 1
- **WHEN** 读取安全快照
- **THEN** `session_journal_dropped` MUST ≥ 1，码表长度 MUST 为 `ALARM_SESSION_JOURNAL_MAX`

#### Scenario: 连续两次读取丢弃数不清零
- **GIVEN** 丢弃计数为 3
- **WHEN** 连续两次拷贝安全视图或两次快照刷新
- **THEN** 两次读到的丢弃计数 MUST 均为 3

#### Scenario: journal 不决定洗后停机
- **GIVEN** 会话中曾记录 MAJOR 且该报警已 AUTO_STATIC 清除，当前无 blocking 活跃
- **WHEN** 洗车正常结束走 `op_mode_on_wash_session_completed`
- **THEN** 模式 MUST NOT 仅因 journal 非空而进入 STOPPED；journal 快照仍可含该码

---

## Invariants

- **INV-01**: `session_journal_count` MUST always ≤ `ALARM_SESSION_JOURNAL_MAX`。
- **INV-02**: journal 中每个码 MUST 是本会话窗口内触发过的、当时满足 `records_in_journal` 的码；MUST NOT 含 MINOR。
- **INV-03**: 丢弃计数 MUST 只在「应记入但码表已满且码不在表中」时增加，MUST NOT 因重复同码或 MINOR 增加。
- **INV-04**: 开洗拒绝与 LOCKOUT 姿态 MUST NOT 读取 journal。
- **INV-05**: 刷新安全快照时 journal 与活动表 MUST 来自同一次 registry 持锁。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 会话 journal | `ALARM_SESSION_JOURNAL_MAX`（16） | 不覆盖；累计 `session_journal_dropped` | 单次洗车通常 0~2 条 MAJOR+；满则取证不完整但必须可见 |
