## Why

电机进入 FAULT 后一律拒绝 `run`/`home`，直到调用方显式 `recover`。本轴再动被做成全局「必须确认」，点动、清障、下一步洗步都不能直接重试；报警目录也不该成为本轴门闩。需要由机构按**故障码**决定：默认下次运动命令可续动，仅显式列出的码必须确认后才能再动。

## What Changes

- 每台电机配置 `confirm_faults` 位图：列入的故障码为**需确认**；未列入为**可续动**。默认 0，全部可续动。
- 可续动：FAULT 下一次 `run`/`home` 由执行器内部完成与 `recover` 相同的清除后再启动；失败则保持 FAULT。
- 需确认：FAULT 下 `run`/`home` 拒绝，直到显式 `recover`（含设备 RECOVER 命令）。
- `DRIVER_PORT_FATAL`、急停 `output_hold`/`ESTOP`、看门狗安全态不受位图放行。
- 本轴门闩不读取 `alarm_registry`。不增加清障专用扫描或阶段挂钩。

**BREAKING**：默认从「FAULT 后一律需确认」改为「FAULT 后默认可续动」。现有轴若过流等码必须确认，须在配置中把对应码写入 `confirm_faults`。

## Capabilities

### New Capabilities

- `motor-fault-confirm`：按故障码区分需确认与可续动；默认可续动；可续动在运动命令内清除。

### Modified Capabilities

- 无。删除先前草案中的 `clearance-auto-recover`（清障不是本能力的场景特例）。

## Impact

- `domain/mechanism/motor/`：配置位图、bind 校验、`run`/`home` 在可续动 FAULT 上内清再启动。
- `domain/ports/outbound/motor/motor_exec_port.h`：查询某码是否需确认。
- `domain/mechanism/patterns/motor_axis.*`：沿用执行器拒令/内清，不另做策略。
- 测试：默认可续动、显式需确认拒令、内清失败保持 FAULT、fatal/hold 覆盖。
- 文档：`机构控制模式模块设计.md`。
- 不改报警系统、`safety_output_hold`、清障协调器。
