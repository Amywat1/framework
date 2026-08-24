## Context

电机执行器在过流、编码器升级、驱动故障等路径进入 `MOTOR_STATE_FAULT` 并切断本轴。今日所有码都闩到显式 `recover`，调用方无法「直接再下一条运动命令」。清障、点动、下一步洗步都是普通调用方，框架不应为其中某一个单独扫轴 recover。

本轴能不能再动由执行器根据**当前故障码**与该电机的 `confirm_faults` 决定，不读报警活动表。报警系统继续管 LOCKOUT、禁开洗和跨轴干涉。

中文用语：

| 用语 | 含义 |
|------|------|
| 可续动 | 该故障码下，下一次 `run`/`home` 无需调用方先确认；执行器内部清除后再启动 |
| 需确认 | 该故障码下，必须显式 `recover` 后才受理运动 |

默认全部可续动；需确认的码在该电机配置里显式置位。仓库处于开发期，行为默认值允许改变，项目须为真正要确认的码填位图。

## Goals / Non-Goals

**Goals:**

- 按电机、按 `motor_exec_fault_code_t` 配置需确认码；默认未列入即可续动。
- 可续动 FAULT：运动命令内部走两步 recover，再执行本次 `run`/`home`。
- 需确认 FAULT：运动命令拒绝，直到显式 recover。
- FATAL、ESTOP/hold、看门狗安全态不可被位图放行。
- 策略对洗、清障、点动、回原同样生效，无场景分支。

**Non-Goals:**

- 不在 `safety_session_coordinator` / `recovery_service` 增加按策略扫槽。
- 不把确认表配进报警目录。
- 不实现跨轴同波次连带。
- 不停稳后偷偷变 `STOPPED`（可续动也必须在命令里完成清除，驱动可能仍锁着）。
- 不改变 `output_hold` / LOCKOUT / cutout 约定。

## Decisions

### D1. 用语与配置：`confirm_faults` 位图，默认 0

不使用「危险 / 不危险」：那是现场安全等级语言，和「下一次命令要不要人点头」不是同一件事。

```c
uint32_t confirm_faults; /* (1u << MOTOR_FAULT_*) 需确认；0 = 全部可续动 */
```

`MOTOR_FAULT_NONE` 不占位。装载期忽略未知高位或拒绝超出已定义码的位（实现取拒绝，避免静默吞配置笔误）。查询：`motor_exec_fault_requires_confirm(exec, motor, code)`。

**备选：** 每轴一个总开关。无法表达「过流可续动、过温需确认」。

### D2. 可续动在 `run`/`home` 内清除，不在 tick 里自复

见 FAULT 且当前码未列入 `confirm_faults` 且非 fatal：速度、方向、编码器与互锁校验通过后，再内部 `DRIVER_RESET` + `MODULE_STOP`，成功则按本次命令继续启动；校验失败不得复位，状态保持该次 FAULT；复位失败则保持 FAULT 并拒绝本次命令。调用方无需先调 `recover`。

需确认码：保持现有 `MOTOR_CMD_REJECT_FAULT`。

**备选：** tick 停稳后自动 `STOPPED`。没有清除动作，驱动可能仍故障。**备选：** 清障启动扫可续动轴。把场景写进框架，点动/洗步还要再写一遍。

### D3. 硬覆盖优先于位图

即使位图包含这些情况，也不得按可续动放行：

- `MOTOR_FAULT_DRIVER_PORT_FATAL` / `fatal` 标志：须 `reinit`
- `ESTOP` 或 `safety_output_hold_is_active()`：须 hold 释放
- 看门狗 `safe_latched`：须 `motor_executor_reset_watchdog`

`SHARED_DRIVER` 默认可续动（位图为 0）。若项目认为共享驱动连带停必须确认，把该轴的 `SHARED_DRIVER` 位置 1。

### D4. 显式 recover 仍是需确认码的唯一放行

设备 `DEV_CMD_RECOVER`、点动前的 `motor_axis_recover`、人工确认都走现有 recover。可续动码也可以被显式 recover（幂等：已非 FAULT 则 `not-fault`），但不依赖这条路径。

本能力不修改 `recovery_service` 去扫全部 FAULT 轴；若归位前仍有需确认 FAULT，`home` 会被拒，与今日一致。项目可在 RECOVER 流程里自行 recover 各轴。

### D5. 不读报警、不挂钩清障

INV：运动门禁不得 `#include` / 查询 `alarm_registry`。`abort_home` 仍是项目黑盒；可续动轴被 `home()` 时自己内清。

## Risks / Trade-offs

- [默认改为可续动，未改配置的升降过流会在清障里自己再动] → **BREAKING**，在提案中标明；项目把升降过流等码写入 `confirm_faults`。
- [同一步编排对可续动过流轴循环 `run` 会连打] → 与「调用方直接再下命令」同义；编排应在 FAULT 结局上停步，而不是忽略返回值猛打。
- [内清占用 `run` 调用线程做驱动复位] → 与今日显式 recover 相同代价，仍在命令锁内。
- [位图笔误把 fatal 置 1] → D3 硬覆盖，bind 可对 fatal 位打 WARN 或拒绝。

## Migration Plan

- 新增字段默认 0：未填即全部可续动。
- 项目审查各轴：须确认的码置入 `confirm_faults`。
- 回滚：去掉位图与 `run`/`home` 内清分支，恢复「FAULT 一律拒令」。

## Open Questions

- 无。
