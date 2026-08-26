## Context

当前 `hal_vfd_manager` 用 `monitor_mask` + `fault_period_ms` / `current_period_ms` 驱动同一 20ms 线程：到期通道在一拍内全部读完，RST 脉冲也在该回调里推进。三台共 9600 总线时一拍可超过 100ms，脉冲沿和 skip 警告一起坏掉。

项目需要：运行中尽可能密地采某些电流，其它通道后台或关闭；多个机构可以同时要快采。急停切断仍走 DO。本轮只改框架，机型（刷子/风机）语义仍由项目绑定表达。

## Goals / Non-Goals

**Goals:**

- 通道三级：`OFF` / `BACKGROUND` / `FAST`；FAST 默认 20ms。
- 允许多个 FAST；每拍只读一个通道，到期 FAST 轮询。
- 脉冲与监测拆线程；监测禁止忙等，每拍最多一笔总线后睡眠。
- 运行中 FAST 电流压制同实例故障码读；后台靠每 8 笔快采穿插保活。
- 快采实际间隔达不到目标时告警并继续跑，同类告警 ≥2s 一次。
- 旧 `monitor_mask` + 周期在策略全 `OFF` 时仍可映射，项目源码本轮不改。

**Non-Goals:**

- 不改波特率、不拆 485、不上异步 libmodbus。
- 不在框架里写刷子/龙门/风机名字或为风机单独 `OFF`。
- 不改 `motor_tick`、急停切断路径。
- 不把 `periodic_task` 的 skip 警告做成按任务可关（若监测 skip 过密，用本能力自己的节流告警表达快采未达标）。

## Decisions

### D1. 绑定同时保留旧字段和新策略

```c
typedef enum {
    HAL_VFD_SAMPLE_OFF = 0,
    HAL_VFD_SAMPLE_BACKGROUND,
    HAL_VFD_SAMPLE_FAST,
} hal_vfd_sample_class_t;

typedef struct {
    hal_vfd_sample_class_t class;
    uint32_t period_ms; /* OFF 必须 0；其余必须 > 0 */
} hal_vfd_channel_policy_t;
```

解析：任一通道 `class != OFF` 则整份绑定走新策略（未写的通道保持 OFF）。否则按 mask 映射：无位 → OFF；`HAL_VFD_FAST_CURRENT_PERIOD_MS` 的电流 → `FAST` 且周期 `HAL_VFD_DEFAULT_FAST_PERIOD_MS`（20ms）；其余有位 → `BACKGROUND`，周期 0 则用 2000ms。

**备选：** 删除旧字段。项目 sim/真机绑定会立刻编不过，违反「本轮只改框架」。

### D2. 两个 periodic 任务，监测每拍一笔

| 任务 | 周期 | 职责 |
|------|------|------|
| `vfd_pulse_poll` | 20ms | 只 `pulse_out_tick` |
| `vfd_monitor_poll` | 20ms | 选 1 个到期工作项，最多 1 次 `ops->read`，然后返回 |

空闲拍立即返回，由 `periodic_task` 睡满切片。禁止 `for(;;){ read(); }`。

选路顺序：若距上次后台穿插已满 8 笔 FAST 且存在到期 BACKGROUND → 做后台并清计数；否则到期 FAST 轮询；否则到期 BACKGROUND 轮询。电流通道仅在 `get_state` 为 FWD/REV 时占用总线。运行中且电流为 FAST 时，该实例故障通道不进入候选。

**备选：** 独占者吃满总线。与「多 FAST 轮询、让出 CPU」冲突。

### D3. 快采未达标：允许运行 + 2s 节流告警

一笔事务耗时可能大于 20ms，或多个 FAST 轮询导致单通道间隔变长。这不是配置错误。当某 FAST 通道两次成功采样间隔 > 其 `period_ms` 时打 `LOG_WARN`，同一 manager **全局** 距上次该告警不足 2000ms 则静默。不拒绝 bind、不停止采样。

**备选：** 绑定时按「FAST 个数 × 20ms」拒绝。框架不知道真实事务时长，误伤多总线项目。

### D4. 常量

- `HAL_VFD_DEFAULT_FAST_PERIOD_MS = 20`
- `HAL_VFD_DEFAULT_BACKGROUND_PERIOD_MS = 2000`（可与旧 `HAL_VFD_DEFAULT_MONITOR_PERIOD_MS` 同值并存）
- `HAL_VFD_FAST_CURRENT_PERIOD_MS` 改为等于默认 FAST 周期，旧「填这个常数即要快采」的绑定仍然成立
- `HAL_VFD_FAST_KEEPALIVE_EVERY = 8`
- 告警节流 2000ms

## Risks / Trade-offs

- [单笔 Modbus 常 >20ms，监测任务仍会 skip] → 脉冲已隔离；快采未达标用 2s 节流告警，避免与 skip 叠成刷屏。不把事务拆异步。
- [旧绑定电流周期 150 被宏改成 20 后变成 FAST] → 有意：该宏本意就是快采；项目未改源码即可吃到 20ms FAST。
- [多 FAST 时每通道变稀] → 预期；告警提醒，不停止。
- [保活 8 笔约 160ms+ 才点名后台] → 已拍板；通讯丢失仍靠电流/后台读失败累加 3 次。

## Migration Plan

- 框架先落地；未填新策略的绑定走映射。
- 项目后续把机型表改成显式 `fault`/`current` 策略，去掉 mask。
- 回滚：恢复单线程到期全读与旧字段。

## Open Questions

- 无。
