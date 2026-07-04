# HAL 出站适配层说明

本目录实现 `framework/ports/outbound/hal/` 中定义的出站端口。业务层只依赖端口，不直接依赖具体 SDK、板卡、串口路径或仿真状态。

## 目录职责

```text
framework/adapters/outbound/hal/
  components/   可复用 HAL 组合组件，不绑定具体 SDK
  providers/    具体供应商、协议或 SDK 的真实硬件实现
  sim/          仿真实现
```

`components/` 放可以跨真实硬件和仿真复用的组合逻辑，例如：

- `motor_io/`：基于 IO 和注入回调的电机 HAL 实现。
- `sensor_filter/`：DI 通道极性转换和计数防抖。
- `do_group_mapper/`：DO group/slot 到实际 DO 引脚的映射。
- `vfd_manager/`：VFD 实例槽位、RST 脉冲、通信监测、缓存和事件。
- `pulse_out/`：HAL 内部非阻塞脉冲工具。

`providers/` 放具体依赖来源。当前 `providers/snack/` 包含 Snack IO 扩展板、Modbus VFD、Modbus 语音模块等真实硬件适配。

`sim/` 放仿真后端，用于 `BUILD_SIM=ON`、单元测试和场景测试。

## 边界规则

- `ports/` 只定义业务可见端口契约。
- `components/` 可以维护运行时状态、bind 配置、tick 和缓存，但不得依赖具体 SDK。
- `providers/` 可以依赖具体 SDK、协议和驱动，但不得承载业务流程语义。
- `projects/<project>/bindings/` 负责把项目配置注入到 components 或 providers。
- `projects/<project>/wiring/` 只负责注册本项目选择的端口实现。

## 典型接线

真实硬件路径：

```text
ports/outbound/hal
  -> components/vfd_manager
  -> providers/snack/modbus/snack_vfd_backend
  -> providers/snack/modbus/drv_vfd
```

仿真路径：

```text
ports/outbound/hal
  -> components/vfd_manager
  -> projects/m8/adapters/hal/hal_vfd_sim_m8
  -> sim 内存状态
```

新增硬件供应商时，优先在 `providers/<vendor>/` 新增 provider，并在项目 wiring/bindings 中选择它；不要把供应商选择隐藏到通用 `hw` 目录中。