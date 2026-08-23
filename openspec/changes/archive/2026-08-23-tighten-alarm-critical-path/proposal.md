## Why

安全矩阵承诺 CRITICAL 是「立刻停机」，但运动路径要等最多 50ms 的 `alarm_bridge` 周期 drain 才发布 `EVT_SAFETY_LOCKOUT` 并 `abort_wash`。同时，活跃池满时新 CRITICAL 会被 MINOR 挤掉而进不了 LOCKOUT；会话 journal 已按矩阵第三列写入，却没有生产读侧，满池还静默丢条。这三条都是同一条原则的缺口：安全相关丢失必须可见，且 lockout 不得被低优先级状态挡住。

## What Changes

- 经 `alarm_binding_port` 的 trigger/clear 在姿态变为（或保持）LOCKOUT 后立刻 drain，使 `EVT_SAFETY_LOCKOUT` 不再排队等 50ms 周期；周期任务保留为非端口路径的兜底。
- 活跃池满时，`forces_lockout` 等级允许驱逐一条非 lockout 活动报警（发出 CLEARED 并记日志）后插入；仅当 32 条都是 lockout 时仍返回 `SW_ERR_OVERFLOW`。
- 会话 journal 满时累计丢弃条数，不再静默失败；journal 码表与丢弃计数并入 `safety_snapshot_t`，由遥测投影刷新。
- 同步更新模块设计、行为契约 ALRM 条目与容量注释。

## Capabilities

### New Capabilities

- `alarm-lockout-drain`：LOCKOUT 边沿的立即排空与 50ms 兜底并存，不把能量切断绑进通用 trigger。
- `alarm-lockout-slot`：活跃池对 lockout 等级的准入优先于非 lockout，驱逐可见。
- `alarm-session-journal`：会话 journal 可被快照读取，溢出可观测。

### Modified Capabilities

- 无。`openspec/specs/` 下尚无既有 capability；既有 ALRM 行为契约在实现阶段作为对照基线修订，不在此作为 OpenSpec delta。

## Impact

- `application/bridges/alarm_binding_bridge.c`：包装 trigger/clear，成功后按姿态决定是否 `alarm_bridge_drain`。
- `domain/safety/alarm_registry/`：插入时 lockout 可驱逐；journal 丢弃计数；拷贝接口交出丢弃数。
- `domain/telemetry/device_snapshot.h`、`application/telemetry_projection.c`：安全快照增加 journal 字段。
- 测试：`test_alarm_event_bridge`、`test_alarm_registry`、`test_telemetry_projection`、`test_alarm_lifecycle_bridge`。
- 文档：`报警系统模块设计.md`、`状态投影与设备快照模块设计.md`、`行为契约.md`、`整治记录.md` 容量表。
- 不改 `safety_port` / `safety_output_hold` / `safety_cutout_execute`。项目仍对需要断动力的码在 trigger 前自行 cutout。
