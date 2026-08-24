## ADDED Requirements

### Requirement: 按故障码配置需确认，默认全部可续动

每台电机 MUST 在 `motor_motor_cfg_t` 携带 `confirm_faults` 位图。某 `motor_exec_fault_code_t` 对应位为 1 时该码为需确认，为 0 时为可续动。全 0 MUST 表示已定义的非硬覆盖故障码全部可续动。运行期 MUST NOT 改写该位图。查询接口 MUST 按码返回是否需确认。

`MOTOR_FAULT_DRIVER_PORT_FATAL` MUST 始终视为需确认（或不可本路径放行），即使对应位被置 1。bind 时若位图包含未定义故障码位，MUST 拒绝 bind。

#### Scenario: 默认未配置则可续动

- **GIVEN** 电机配置已 `memset` 为零且其余字段合法，bind 成功，轴因过流进入非 fatal FAULT
- **WHEN** 查询 `OVERCURRENT` 是否需确认
- **THEN** MUST 为否（可续动）

#### Scenario: 显式置位后该码需确认

- **GIVEN** 该轴 `confirm_faults` 含 `OVERCURRENT` 位且 bind 成功，轴因过流进入 FAULT
- **WHEN** 查询 `OVERCURRENT` 是否需确认
- **THEN** MUST 为是

#### Scenario: 同轴不同码策略不同

- **GIVEN** 该轴仅将 `OVERTEMP` 列入 `confirm_faults`
- **WHEN** 分别查询 `OVERCURRENT` 与 `OVERTEMP`
- **THEN** 过流 MUST 可续动，过温 MUST 需确认

#### Scenario: 未定义位导致 bind 失败

- **GIVEN** `confirm_faults` 含已定义故障码范围之外的位
- **WHEN** 调用 `motor_executor_bind`
- **THEN** bind MUST 失败，该槽 MUST 不进入可 tick 状态

---

### Requirement: 可续动 FAULT 在运动命令内清除后再启动

轴处于非 fatal FAULT 且当前 `fault_code` 为可续动时，`run` 与 `home` MUST 在速度、方向、编码器与互锁等本次命令校验通过后，再完成与显式 recover 相同的两步清除并启动。调用方 MUST NOT 被要求先调用 `recover`。校验失败 MUST NOT 内清，MUST 保持该次 FAULT。内部清除失败时 MUST 保持 FAULT 并拒绝本次命令。tick 停稳 MUST NOT 自行把可续动 FAULT 变为 `STOPPED`。

#### Scenario: 可续动过流后直接 run 成功受理

- **GIVEN** 轴因过流 FAULT，过流为可续动，驱动复位会成功
- **WHEN** 调用 `run`（未先 `recover`）
- **THEN** 命令 MUST 被受理（或进入启动路径），状态 MUST 离开该次过流 FAULT

#### Scenario: 可续动内清失败则仍拒绝

- **GIVEN** 轴因过流 FAULT，过流为可续动，驱动复位失败
- **WHEN** 调用 `run`
- **THEN** 命令 MUST 拒绝，状态 MUST 仍为 FAULT

#### Scenario: 仅 tick 不会自复可续动 FAULT

- **GIVEN** 轴处于可续动 FAULT 且已停稳
- **WHEN** 经过至少 1 个 tick 且无运动命令
- **THEN** 状态 MUST 仍为 FAULT

#### Scenario: 校验失败不得内清

- **GIVEN** 轴因过流 FAULT，过流为可续动
- **WHEN** 调用方向非法的 `run`，或互锁不满足时调用 `run`
- **THEN** 命令 MUST 拒绝，状态 MUST 仍为该次过流 FAULT，MUST NOT 执行驱动复位

---

### Requirement: 需确认 FAULT 必须显式 recover 才可再运动

轴处于非 fatal FAULT 且当前码为需确认时，`run` 与 `home` MUST 拒绝。两步显式 `recover` 成功后 MUST 进入 `STOPPED` 并允许后续运动。本门闩 MUST NOT 读取报警注册表。

#### Scenario: 需确认码拒绝未 recover 的 run

- **GIVEN** 轴因过温 FAULT，过温已列入 `confirm_faults`
- **WHEN** 未 recover 即调用 `run` 或 `home`
- **THEN** MUST 以故障拒令拒绝，输出 MUST 保持切断

#### Scenario: 显式 recover 后允许再运动

- **GIVEN** 轴处于需确认非 fatal FAULT
- **WHEN** 依次成功 `DRIVER_RESET` 与 `MODULE_STOP`
- **THEN** 状态 MUST 为 `STOPPED`，随后 `run` MUST 可受理

#### Scenario: 无报警目录时需确认仍拒令

- **GIVEN** 未装载报警目录，轴因已列入确认表的码进入 FAULT
- **WHEN** 调用 `run`
- **THEN** MUST 拒绝

---

### Requirement: 硬覆盖不可被可续动放行

`fatal`、`MOTOR_FAULT_DRIVER_PORT_FATAL`、`ESTOP`、`safety_output_hold` 有效、看门狗安全态 MUST NOT 因 `confirm_faults` 未置位而在 `run`/`home` 上按可续动内清放行。

#### Scenario: fatal 即使位图未列 fatal 码也不得内清启动

- **GIVEN** 轴 `fatal` 且处于 FAULT，`confirm_faults` 为 0
- **WHEN** 调用 `run`
- **THEN** MUST 拒绝，MUST NOT 完成可续动内清后启动

#### Scenario: hold 有效时 ESTOP 不得当可续动 FAULT

- **GIVEN** `safety_output_hold` 有效，轴为 ESTOP，`confirm_faults` 为 0
- **WHEN** 调用 `run` 或内部可续动清除
- **THEN** MUST 因安全门拒绝，MUST 保持 ESTOP

---

## Invariants

- **INV-01**: 需确认且处于 FAULT 的轴 MUST NOT 被 `run`/`home` 受理。
- **INV-02**: 可续动 FAULT MUST NOT 在没有一次成功内部或显式 `MODULE_STOP` recover 的情况下变为可运动输出。
- **INV-03**: `fatal`、ESTOP、output_hold、看门狗安全态 MUST NOT 仅因位图未置位而被放行。
- **INV-04**: 本轴运动门禁 MUST NOT 读取 `alarm_registry`。
- **INV-05**: `confirm_faults` 运行期 MUST 只读；`tick` 与命令/recover MUST 由既有执行器锁串行。

---

## State Machine

上电默认 `STOPPED`。故障安全：监测故障 → `FAULT`；端口致命 → `FAULT+fatal`；hold → `ESTOP`。

| Current State | Event | Next State | Guard Condition | Side Effect |
|---------------|-------|------------|-----------------|-------------|
| RUNNING 等 | 监测故障 | FAULT | 非 hold | 切断，记录 fault_code |
| FAULT | `run`/`home` | 启动路径 | 非 fatal 且码可续动且内清成功 | 内部 recover 后执行本次命令 |
| FAULT | `run`/`home` | FAULT | 码可续动但内清失败 | 拒令 |
| FAULT | `run`/`home` | FAULT | 码需确认 | 拒令 |
| FAULT | 显式两步 recover 成功 | STOPPED | 非 fatal | 允许后续运动 |
| FAULT+fatal | `run`/`home`/普通 recover | FAULT+fatal | — | 拒令 |
| ESTOP | `run`/`home` | ESTOP | hold 有效 | 安全拒令 |
| ESTOP | hold 释放 | FAULT | 残留 `fault_code` 或 `fatal` | 恢复故障门闩，不得变成 STOPPED+残留码 |
| ESTOP | hold 释放 | STOPPED | 无残留故障 | 允许后续运动 |

- **Illegal transitions**: 需确认 FAULT 下受理运动；tick 自复 FAULT。
- **Power-on default state**: `STOPPED`，`confirm_faults` 来自 bind（默认 0）。
- **Fail-safe state**: `FAULT`、`FAULT+fatal` 或 `ESTOP`。

---

## Capacity & Resource Constraints

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| 故障码位图 | 已定义 `motor_exec_fault_code_t` 个数 | 超出位 bind 拒绝 | 枚举闭合，禁止静默忽略 |
| 每执行器电机 | `MOTOR_MAX_MOTORS` | 既有 bind 拒绝 | 既有容量 |
