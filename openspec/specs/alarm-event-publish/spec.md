# alarm-event-publish Specification

## Purpose

报警聚合根在放锁后直接向事件总线发布变位与姿态边沿，与 `operational_mode` 相同：不订阅、只发布。不再经过 pending 队列或 `alarm_bridge` drain。

## Requirements

### Requirement: 变位在调用返回前入队
`alarm_registry_trigger`、`alarm_registry_clear`、`alarm_registry_reset_all`、`alarm_registry_reevaluate_group` 在成功改变活动表（含插入、删除、因 lockout 驱逐删除）后，MUST 在返回调用方之前将对应的 `EVT_ALARM_TRIGGERED` / `EVT_ALARM_CLEARED` 以 `event_publish_required` 入队。param MUST 为报警码。重复 trigger 仅刷新 `condition_active`、未产生新活动条目时 MUST NOT 再发 TRIGGERED。未知码失败时 MUST NOT 发事件。实现 MUST NOT 维护待发域事件队列，MUST NOT 提供 `alarm_registry_pull_events`，MUST NOT 发布 `EVT_ALARM_RESYNC`。

#### Scenario: MINOR 触发立即入队
- **GIVEN** 目录含 MINOR 码 A，A 未活跃，事件总线已初始化
- **WHEN** `alarm_registry_trigger(A)` 返回 `SW_OK`
- **THEN** 返回前总线队列 MUST 含一条 `EVT_ALARM_TRIGGERED`（param=A）；MUST NOT 依赖周期任务
- **TIMING** 入队 MUST 发生在函数返回之前

#### Scenario: AUTO_STATIC 清除立即入队 CLEARED
- **GIVEN** AUTO_STATIC 码 A 已活跃
- **WHEN** `alarm_registry_clear(A)` 返回 `SW_OK`
- **THEN** 返回前总线队列 MUST 含 `EVT_ALARM_CLEARED`（param=A）；A MUST 已不在活动表

#### Scenario: 重复 trigger 不重复 TRIGGERED
- **GIVEN** 码 A 已活跃
- **WHEN** 再次 `alarm_registry_trigger(A)`
- **THEN** 返回 `SW_OK`；MUST NOT 再入队 `EVT_ALARM_TRIGGERED`；`condition_active` MUST 为 true

#### Scenario: reset_all 批量清除后全部 CLEARED 已入队
- **GIVEN** 两条 MANUAL_RESET 报警均活跃且 `condition_active==false`
- **WHEN** 调用 `alarm_registry_reset_all`
- **THEN** 两条均已从活动表删除；返回前总线队列 MUST 含两条 `EVT_ALARM_CLEARED`，param 分别为两码
- **TIMING** 入队 MUST 发生在函数返回之前，MUST NOT 等待周期任务

#### Scenario: 总线高优先级队列满时仍不得静默丢控制边沿
- **GIVEN** 将发布 `EVT_ALARM_TRIGGERED` 且该事件走 `event_publish_required`
- **WHEN** 发布时高优先级队列已满
- **THEN** 行为 MUST 与仓内 `event_publish_required` 契约一致（失败可见，不得假装已通知）；registry 活动表 MUST 仍反映本次 trigger/clear 的事实
- **TIMING** 不额外重试发布，避免采集线程无界等待

### Requirement: 姿态边沿由 registry 发布
活动表变更导致 `safety_posture` 变化时，registry MUST 在逐码 `EVT_ALARM_*` 之后、函数返回之前入队恰好一条对应边沿：进入 LOCKOUT 发 `EVT_SAFETY_LOCKOUT`，回到 NOMINAL 发 `EVT_SAFETY_NOMINAL`。姿态未变 MUST NOT 发姿态事件。MUST NOT 由 `alarm_bridge` 再推导或补发姿态边沿。

#### Scenario: 首次 CRITICAL 触发发布 LOCKOUT
- **GIVEN** 姿态为 NOMINAL，目录含 CRITICAL 码 C
- **WHEN** `alarm_registry_trigger(C)` 成功
- **THEN** 返回前队列 MUST 先后含 `EVT_ALARM_TRIGGERED`(C) 与 `EVT_SAFETY_LOCKOUT`；`alarm_registry_safety_posture()` MUST 为 LOCKOUT
- **TIMING** 两条入队均 MUST 在函数返回前完成

#### Scenario: 第二条 CRITICAL 不重复 LOCKOUT
- **GIVEN** 已有一条 CRITICAL 活跃，姿态为 LOCKOUT
- **WHEN** trigger 另一条尚未活跃的 CRITICAL
- **THEN** 返回前 MUST 含新码的 `EVT_ALARM_TRIGGERED`；MUST NOT 再发 `EVT_SAFETY_LOCKOUT`

#### Scenario: 清除最后一条 CRITICAL 立即发 NOMINAL
- **GIVEN** 仅一条 AUTO_STATIC CRITICAL 活跃
- **WHEN** `alarm_registry_clear` 该码成功，姿态变为 NOMINAL
- **THEN** 返回前队列 MUST 含 `EVT_ALARM_CLEARED` 与 `EVT_SAFETY_NOMINAL`
- **TIMING** 入队 MUST 在函数返回前完成

### Requirement: 发布不得持 registry 锁且不得在采集线程停机
registry MUST 在释放自身互斥量之后才调用 `event_publish` / `event_publish_required`。发布路径 MUST 只入队，MUST NOT 调用 `abort_wash`、`safety_cutout_execute` 或 `safety_output_hold_request`。`abort_wash` 仍由 dispatch 线程上的 `safety_session_coordinator` 消费 `EVT_SAFETY_LOCKOUT`。

#### Scenario: 持锁期间不进入总线
- **GIVEN** 测试可观测 registry 互斥与 `event_publish` 的嵌套
- **WHEN** 任意 trigger/clear/reset_all/reevaluate_group 发布事件
- **THEN** `event_publish*` MUST NOT 在仍持有 registry 互斥时被调用

#### Scenario: LOCKOUT 入队不等于机构已停
- **GIVEN** 洗车中经 binding trigger 一条 CRITICAL
- **WHEN** trigger 返回
- **THEN** 总线队列 MUST 已含 `EVT_SAFETY_LOCKOUT`；MUST NOT 在 trigger 调用栈内执行 `abort_wash` 或 cutout

#### Scenario: 两线程同时 trigger 不同码
- **GIVEN** 两线程并发对两个未活跃码调用 trigger
- **WHEN** 两次调用均返回
- **THEN** 活动表与已入队 TRIGGERED 条数 MUST 与成功插入次数一致，MUST NOT 重复或丢失码；姿态边沿次数 MUST 等于实际 NOMINAL→LOCKOUT 次数（0 或 1）

### Requirement: 应用桥不再排空 pending
`alarm_bridge` MUST NOT 注册周期 drain 任务，MUST NOT 导出 `alarm_bridge_drain`。入站 `alarm_binding_port` 的 trigger/clear/load_catalog MUST 转调对应 `alarm_registry_*`，MUST NOT 在成功后再 drain。桥接仍 MUST 订阅洗车会话事件以开关 journal，并 MAY 由项目调用 `alarm_bridge_reeval_init` 做 ON_MOTION 查表。

#### Scenario: 无周期 drain 时 MINOR 仍已入队
- **GIVEN** `alarm_bridge_init` 已完成且未注册报警 drain 周期任务
- **WHEN** 经 `alarm_binding_port.trigger` 上报 MINOR 成功
- **THEN** 返回前总线 MUST 已含该码 `EVT_ALARM_TRIGGERED`

### Requirement: 运行模式对报警只做一次收敛
`op_mode_bridge` MUST 在存在 blocking 活跃报警时调用单一收敛入口（离开接单/静态可运营态 → STOPPED）。MUST NOT 对同一次 CRITICAL 变位分别调用「blocking 收敛」与「critical 收敛」两个等价入口。`EVT_SAFETY_LOCKOUT` MUST 仍由 `safety_session_coordinator` 用于 `abort_wash`，MUST NOT 从总线上删除。

#### Scenario: CRITICAL 触发只收敛一次模式
- **GIVEN** 模式为 IDLE，目录含 CRITICAL 码 C
- **WHEN** trigger C 成功且事件被排空消费
- **THEN** 模式 MUST 进入 STOPPED；用于「因报警离开 IDLE」的模式切换 MUST 恰好一次（允许日志一次）

---

## Invariants

- **INV-01**: registry 源文件 MUST NOT include `alarm_bridge` 或任何 `application/` 头文件。
- **INV-02**: registry 发布事件时 MUST NOT 仍持有自身互斥量。
- **INV-03**: 采集 / binding 调用栈 MUST NOT 同步执行 `abort_wash` 或 `safety_cutout_execute`。
- **INV-04**: 同一姿态边沿 MUST 只发布一次；不得先 LOCKOUT 再 NOMINAL 顺序颠倒于实际姿态。
- **INV-05**: 系统中 MUST NOT 存在报警 pending 队列或 `EVT_ALARM_RESYNC`。
- **INV-06**: 命令路径（开洗拒绝、洗后 STOPPED、recover 复验）MUST 仍同步读 registry，MUST NOT 以事件到达作为事实源。

---

## State Machine

| Current State | Event | Next State | Guard Condition | Side Effect |
|---------------|-------|------------|-----------------|-------------|
| NOMINAL | 活动表出现 `forces_lockout` | LOCKOUT | 插入或驱逐后仍有 lockout 条目 | 发 TRIGGERED（若有）后发 `EVT_SAFETY_LOCKOUT` |
| LOCKOUT | 活动表不再有 `forces_lockout` | NOMINAL | 删除最后一条 lockout | 发 CLEARED（若有）后发 `EVT_SAFETY_NOMINAL` |
| LOCKOUT | 再插入 lockout | LOCKOUT | 已有 lockout | 只发 TRIGGERED，不发姿态事件 |
| NOMINAL | 只插入/删除非 lockout | NOMINAL | 无 lockout | 只发对应 `EVT_ALARM_*` |

- **Illegal transitions**: 不得在无 lockout 条目时发布 `EVT_SAFETY_LOCKOUT`；不得在仍有 lockout 条目时发布 `EVT_SAFETY_NOMINAL`。
- **Power-on default state**: `SAFETY_POSTURE_NOMINAL`（`alarm_registry_init` 清空活动表后）。
- **Fail-safe state**: 有 CRITICAL 活跃即为 LOCKOUT；总线入队失败不回滚活动表（事实源优先于通知）。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 本轮待发布码 | `ALARM_ACTIVE_MAX`（32） | 一批清除不会超过活动表规模；栈上缓存，不另设 pending | 与活动表对齐 |
| 事件总线高优先级队列 | `EVENT_BUS_HI_QUEUE_SIZE` | `event_publish_required` 既有契约 | 控制边沿走 required；不在报警域再做第二套丢件协议 |
| drain 周期任务 | 0（删除） | 无 | 变位函数自身发布 |
