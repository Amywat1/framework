# alarm-session-journal Specification

## Purpose

洗车会话内 MAJOR 及以上首次触发记入 journal；满池不覆盖、累计丢弃计数且读取不清零。安全快照带出码表与丢弃数。开洗拒绝与 LOCKOUT、洗后是否 STOPPED 仍只看当前 blocking，不读 journal。

## Requirements

### Requirement: 会话 journal 溢出可观测
洗车会话窗口内，等级满足 `alarm_level_records_in_journal` 的首次触发 MUST 记入会话 journal（同码去重）。当 journal 条数已达 `ALARM_SESSION_JOURNAL_MAX` 且该码尚未在 journal 中时，实现 MUST 累加 `session_journal_dropped` 且 MUST NOT 覆盖已有条目。新会话开始 MUST 将码表与丢弃计数一并清零。会话结束 MUST 停止追加，但 MUST 保留码表与丢弃计数直到下一会话开始，供洗后读取。

#### Scenario: 未满时记入且去重
- **GIVEN** 会话已开始，journal 为空
- **WHEN** 同一 MAJOR 码 trigger 两次
- **THEN** journal 条数为 1，丢弃计数为 0

#### Scenario: 满时丢弃并计数
- **GIVEN** 会话已开始，journal 已有 `ALARM_SESSION_JOURNAL_MAX` 个不同 MAJOR 码，丢弃计数为 0
- **WHEN** trigger 一条尚未在 journal 中的新 MAJOR
- **THEN** 该码 MUST 进入活动表（若池未满）；journal 码表 MUST 不变；丢弃计数 MUST 为 1；MUST 有可观测日志（允许一段丢弃只打首条）

#### Scenario: MINOR 不记 journal 也不增加丢弃
- **GIVEN** 会话已开始，journal 已满
- **WHEN** trigger 一条 MINOR
- **THEN** journal 码表与丢弃计数 MUST 均不变

#### Scenario: 新会话清零
- **GIVEN** 上一会话 journal 非空且丢弃计数非 0
- **WHEN** 收到会话开始
- **THEN** 码表条数为 0，丢弃计数为 0

### Requirement: 安全快照包含会话 journal
`safety_snapshot_t` MUST 包含本会话 journal 码表、条数与丢弃计数。遥测投影在刷新安全快照时 MUST 从 registry 拷贝上述字段。读接口拷贝码表时 MUST 同时给出当前累计丢弃数，且 MUST NOT 因读取而清零丢弃计数（与 pending 的「取出即清」不同：journal 是多读者状态）。洗后是否进入 STOPPED MUST NOT 以 journal 为准，MUST 仍以当前 `has_blocking_active` 为准。

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
- **WHEN** 连续两次 `get_session_journal` 或两次快照刷新
- **THEN** 两次读到的丢弃计数 MUST 均为 3

#### Scenario: journal 不决定洗后停机
- **GIVEN** 会话中曾记录 MAJOR 且该报警已 AUTO_STATIC 清除，当前无 blocking 活跃
- **WHEN** 洗车正常结束走 `op_mode_on_wash_session_completed`
- **THEN** 模式 MUST NOT 仅因 journal 非空而进入 STOPPED；journal 快照仍可含该码

## Invariants

- **INV-01**: `session_journal_count` MUST always ≤ `ALARM_SESSION_JOURNAL_MAX`。
- **INV-02**: journal 中每个码 MUST 是本会话窗口内触发过的、当时满足 `records_in_journal` 的码；MUST NOT 含 MINOR。
- **INV-03**: 丢弃计数 MUST 只在「应记入但码表已满且码不在表中」时增加，MUST NOT 因重复同码或 MINOR 增加。
- **INV-04**: 开洗拒绝与 LOCKOUT 姿态 MUST NOT 读取 journal。

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 会话 journal | `ALARM_SESSION_JOURNAL_MAX`（16） | 不覆盖；累计 `session_journal_dropped` | 单次洗车通常 0~2 条 MAJOR+；满则取证不完整但必须可见 |
