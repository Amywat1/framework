# vfd-channel-sample Specification

## Purpose

VFD 监测按通道服务等级调度：`OFF` / `BACKGROUND` / `FAST`。允许多个 FAST 并存，每拍最多读一个通道并让出 CPU。RST 脉冲与 Modbus 监测拆开。运行中 FAST 电流跳过同实例故障码，后台通道靠保活配额穿插。快采间隔达不到目标时告警并继续运行，同类告警节流。绑定只认显式 `fault` / `current` 策略，不再提供 mask 或周期字段回退。

## Requirements

### Requirement: 通道服务等级 OFF、BACKGROUND、FAST

每个已绑定实例 MUST 为故障码通道与电流通道各携带 `hal_vfd_channel_policy_t`。`OFF` 的 `period_ms` MUST 为 0；`BACKGROUND` 与 `FAST` 的 `period_ms` MUST 大于 0。默认快采周期常量 MUST 为 `HAL_VFD_DEFAULT_FAST_PERIOD_MS`（20ms），默认后台周期常量 MUST 为 `HAL_VFD_DEFAULT_BACKGROUND_PERIOD_MS`（2000ms）。绑定 MUST 允许任意多个实例的任意通道为 `FAST`。非法策略 MUST 使 bind 失败。监测调度 MUST 只使用 `fault` 与 `current` 策略字段。

#### Scenario: 显式 FAST 与 BACKGROUND 绑定成功

- **GIVEN** 实例电流为 FAST 20ms、故障为 BACKGROUND 2000ms，backend 指针合法
- **WHEN** 调用 `hal_vfd_manager_bind`
- **THEN** bind MUST 返回 `SW_OK`，后续监测 MUST 按上述策略调度

#### Scenario: FAST 周期为 0 则 bind 失败

- **GIVEN** 电流 class 为 FAST 且 `period_ms` 为 0
- **WHEN** 调用 `hal_vfd_manager_bind`
- **THEN** MUST 返回 `SW_ERR_PARAM`，该槽 MUST 保持未绑定

#### Scenario: OFF 且周期非 0 则 bind 失败

- **GIVEN** 故障 class 为 OFF 且 `period_ms` 为 1
- **WHEN** 调用 `hal_vfd_manager_bind`
- **THEN** MUST 返回 `SW_ERR_PARAM`，该槽 MUST 保持未绑定

#### Scenario: 策略全 OFF 则不发总线读

- **GIVEN** 故障与电流策略均为 OFF，实例已 init
- **WHEN** 连续推进监测至少 3 拍
- **THEN** backend `read` 次数 MUST 保持 0

---

### Requirement: 每拍最多一个通道且禁止忙等

监测周期任务每拍 MUST 最多发起 1 次 backend `read`，然后 MUST 返回以便调度器睡眠。RST 脉冲 MUST 在独立周期任务中推进，该任务 MUST NOT 调用 `read`。空闲拍（无到期通道）MUST 不发总线事务。多个到期 FAST 通道 MUST 轮询，不得在同一拍读第二个通道。

#### Scenario: 一拍只读一个通道

- **GIVEN** 同一实例故障与电流均为 BACKGROUND 且周期 1ms，实例为 FWD，两通道均到期
- **WHEN** 推进 1 次监测 tick
- **THEN** `read` 次数 MUST 增加 1

#### Scenario: 两个 FAST 电流轮询

- **GIVEN** 槽 0 与槽 1 电流均为 FAST 20ms 且均为 FWD，故障为 OFF
- **WHEN** 连续推进 2 次监测 tick
- **THEN** 两次 `read` MUST 分别落在两个不同实例的电流寄存器上（顺序可从槽 0 起）

#### Scenario: 脉冲 tick 不发 Modbus

- **GIVEN** 实例已绑定 FAST 电流且为 FWD
- **WHEN** 只推进脉冲 tick、不推进监测 tick
- **THEN** `read` 次数 MUST 保持 0，若 RST 脉冲已到时 MUST 仍能落沿

#### Scenario: 读失败仍只占一拍

- **GIVEN** 到期 FAST 电流，backend `read` 返回 `SW_ERR_COMM`
- **WHEN** 推进 1 次监测 tick
- **THEN** 本拍 `read` MUST 恰好 1 次，MUST NOT 在同一拍重试第二笔

---

### Requirement: 运行中 FAST 电流跳过同实例故障码并穿插后台保活

当实例 `get_state` 为 FWD 或 REV 且电流策略为 FAST 时，该实例故障通道 MUST NOT 被选为监测工作项。连续完成 `HAL_VFD_FAST_KEEPALIVE_EVERY`（8）笔 FAST 读之后，若存在到期 BACKGROUND 通道，下一拍 MUST 改为读该后台通道并将保活计数清零。未运行实例的电流通道 MUST NOT 占用总线。

#### Scenario: FAST 电流运行时不读本实例故障

- **GIVEN** 电流 FAST、故障 BACKGROUND 周期 1ms，实例 FWD，故障码非 0
- **WHEN** 连续推进 3 次监测 tick
- **THEN** 三次 `read` MUST 均为电流寄存器，缓存故障码 MUST 保持绑定后初值 0

#### Scenario: 停机后可以读故障码

- **GIVEN** 同上，已推进若干 FAST 电流拍，随后实例改为 STOPPED，故障 BACKGROUND 已到期
- **WHEN** 再推进 1 次监测 tick
- **THEN** MUST 读取故障码寄存器

#### Scenario: 第八笔 FAST 后穿插其它实例后台

- **GIVEN** 槽 0 电流 FAST 且 FWD，槽 1 故障 BACKGROUND 周期 1ms 且 STOPPED
- **WHEN** 连续推进 9 次监测 tick
- **THEN** 前 8 次 `read` MUST 为槽 0 电流，第 9 次 MUST 为槽 1 故障码

#### Scenario: 停机电流不读总线

- **GIVEN** 电流 FAST，实例 STOPPED
- **WHEN** 推进 3 次监测 tick
- **THEN** 对该实例 `read` 次数 MUST 为 0

---

### Requirement: 快采间隔未达标时告警节流且继续采样

若某 FAST 通道两次成功采样的间隔大于其 `period_ms`，监测 MUST 继续运行，MUST 用 `LOG_WARN` 报告一次。自该警告上次发出起不足 2000ms 时，即使再次未达标 MUST NOT 再发同类警告。通讯连续失败仍 MUST 按现有 3 次规则上报 `COMM_LOST`，不得因节流而抑制该事件。

#### Scenario: 未达标告警后 2s 内不重复

- **GIVEN** 单 FAST 通道，测试替身使单笔 `read` 耗时大于 20ms，使成功采样间隔 > period
- **WHEN** 在 2000ms 内连续发生至少两次未达标
- **THEN** 该类 WARN MUST 最多出现 1 次，随后的监测 tick MUST 仍发起 `read`

#### Scenario: 通讯丢失不受告警节流影响

- **GIVEN** FAST 电流运行中，`read` 连续返回 `SW_ERR_COMM`
- **WHEN** 失败次数达到 3
- **THEN** MUST 发出 `HAL_VFD_EVT_COMM_LOST` 恰好一次

---

## Invariants

- **INV-01**: 监测回调单次进入 MUST NOT 发起超过 1 次 backend `read`。
- **INV-02**: 脉冲周期任务 MUST NOT 调用 backend `read`。
- **INV-03**: 监测实现 MUST NOT 在回调内循环等待下一笔总线事务（禁止忙等）。
- **INV-04**: 急停切断路径 MUST NOT 因本监测调度而改为走 Modbus。
- **INV-05**: 运行中电流为 FAST 的实例，其故障通道 MUST NOT 与该电流在同一调度选择中同时入选。
- **INV-06**: 快采未达标 WARN 的全局间隔 MUST always ≥ 2000ms。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 实例槽位 | 8 | bind 拒绝非法 id | 现有 `HAL_VFD_MANAGER_SLOT_MAX` |
| 每拍总线事务 | 1 | 其余到期项留到后续拍 | 单 RS-485 串行、让出 CPU |
| FAST 保活比 | 每 8 笔 FAST 最多 1 笔后台 | 无到期后台则继续 FAST | 已拍板 |
| 未达标告警 | 每 2000ms 至多 1 条 | 静默 | 减少刷屏 |
| 连续通讯失败上报 | 3 次 | 发 `COMM_LOST` 一次 | 现有 `VFD_COMM_FAIL_NOTIFY` |
