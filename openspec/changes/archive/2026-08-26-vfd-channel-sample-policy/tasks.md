## 1. 绑定与常量

- [x] 1.1 在 `hal_vfd_manager_bind.h` 增加通道策略类型、FAST 20ms / BACKGROUND 2000ms / 保活 8 / 告警节流常量；`HAL_VFD_FAST_CURRENT_PERIOD_MS` 与默认 FAST 对齐；保留旧 mask 与周期字段
- [x] 1.2 bind 校验新策略；全 OFF 时走 mask 映射；非法策略返回 `SW_ERR_PARAM`

## 2. 调度

- [x] 2.1 拆 `vfd_pulse_poll` 与 `vfd_monitor_poll`；`hal_vfd_manager_poll_register_task` 登记二者；测试入口拆 pulse/monitor
- [x] 2.2 监测每拍最多 1 次 `read`；多 FAST 轮询；空闲不发总线
- [x] 2.3 运行中 FAST 电流跳过同实例故障；停机电流不占总线；每 8 笔 FAST 穿插到期 BACKGROUND
- [x] 2.4 FAST 采样间隔大于 period 时 `LOG_WARN` 且 2000ms 节流；通讯 3 次失败仍发 `COMM_LOST`

## 3. Snack 映射与测试

- [x] 3.1 snack 绑定把旧 mask/周期交给 manager 映射，不在 provider 写机型名
- [x] 3.2 更新 `test_hal_vfd_manager`：一拍一通道、双 FAST 轮询、FAST 让路故障、保活穿插、bind 拒绝 FAST 周期 0、脉冲不读总线
- [x] 3.3 更新 `test_snack_vfd_backend` 与 `test_tick_no_alloc` 使与新字段/双 tick 兼容

## 4. 文档与门禁

- [x] 4.1 更新 HAL/Runtime/总览中 `vfd_manager_poll` 与监测策略描述
- [x] 4.2 运行 `python3 scripts/gen_doc_index.py` 后执行框架检查（有 cmake 则带测）
