## Why

`vfd_manager_poll` 把 RST 脉冲与全部到期的 Modbus 读叠在同一 20ms 回调里，一拍可连续打多台变频器，拖垮脉冲节拍并刷 skip 警告。监测只有开/关和两个独立周期，无法表达「不读 / 后台 / 快采」，也无法在多机构同时要快采时轮询让出 CPU。

## What Changes

- 每个实例的故障码、电流各有服务等级：`OFF` / `BACKGROUND` / `FAST`。`FAST` 默认周期 20ms，允许任意多个 FAST 通道同时存在，到期后轮询，每拍只读一个通道。
- 监测与 RST 脉冲拆成两个周期任务：脉冲线程禁止发总线；监测线程每拍最多一笔事务后返回睡眠，禁止忙等。
- 实例处于运行且电流为 FAST 时，跳过该实例故障码通道；通信健康由电流读写成败承担。连续 8 笔快采后若有到期后台通道则穿插一笔。
- 快采实际间隔达不到目标周期时告警并继续运行；同类告警按 2s 节流。
- **BREAKING（仅新绑定字段）**：`hal_vfd_manager_bind_cfg_t` 增加通道策略。未填策略（全 `OFF`）时仍由原 `monitor_mask` + 周期字段映射，项目层本轮不改。

## Capabilities

### New Capabilities

- `vfd-channel-sample`：通道服务等级、每拍一通道、多 FAST 轮询、脉冲/监测隔离、同实例快采让路故障码、后台保活、快采间隔告警节流。

### Modified Capabilities

- 无。

## Impact

- `adapters/outbound/hal/components/vfd_manager/`：绑定解析、双任务、调度器。
- `providers/snack/modbus/snack_vfd_backend`：把旧 mask/周期映射到新策略（`HAL_VFD_FAST_CURRENT_PERIOD_MS` 视为 FAST）。
- 框架测试：`test_hal_vfd_manager`、`test_snack_vfd_backend`、`test_tick_no_alloc`。
- 文档：`HAL端口与适配器模块设计.md`、`Runtime模块设计.md`、模块设计总览。
- 本轮不改项目层（刷子/龙门/风机语义仍由项目绑定表达）。
