# HAL 适配层说明

本目录承接 `framework/ports/outbound/hal/` 定义的硬件端口，将业务可见的 HAL 能力适配到具体硬件、第三方 SDK 或仿真实现。

## 目录结构

```text
framework/adapters/outbound/hal/
  components/   可复用的 HAL 组合逻辑，不依赖具体 SDK
  providers/    具体厂商、芯片或外部 SDK 适配
  sim/          仿真实现
```

`components/` 放可复用的 HAL 组合逻辑：

- `sensor_filter/`：DI 传感器滤波与状态缓存。
- `do_group_mapper/`：DO group/slot 到 DO 端口的映射。
- `vfd_manager/`：VFD 方向、RST 与命令调度。

通用非阻塞脉冲时序原语 `pulse_out`（供 `vfd_manager` 等内部复用）不依赖任何 HAL
类型，已迁移到 `framework/common/pulse_out.*`，不属于本目录。

`providers/` 放具体外部依赖适配。目前 `providers/snack/` 包含 Snack IO、Modbus VFD、Modbus 语音等适配。

`sim/` 放仿真侧实现，主要用于 `BUILD_SIM=ON` 的本地运行和测试。

## 边界规则

- `ports/` 只放业务可见的硬件端口声明。
- `components/` 可以维护 bind、内部 tick、缓存和组合逻辑，但不能依赖具体 SDK；
  若 bind 或 tick 属于装配/内部推进能力，不应通过业务可见 port 暴露给项目层。
- `providers/` 可以依赖具体 SDK，但不承载业务语义。
- `projects/<project>/bindings/` 负责把项目配置连接到 components 和 providers。
- `projects/<project>/wiring/` 负责选择真实硬件或仿真实现。

## 典型路径

真实硬件：

```text
ports/outbound/hal
  -> components/vfd_manager
  -> providers/snack/modbus/snack_vfd_backend
  -> providers/snack/modbus/drv_vfd
```

仿真运行：

```text
ports/outbound/hal
  -> components/vfd_manager
  -> projects/m8/adapters/hal/hal_vfd_sim_m8
  -> sim 实现
```

后续接入其它硬件时，优先新增 `providers/<vendor>/` 或 provider 文件，并在项目 wiring/bindings 中完成装配；不要再使用 `hw` 这类含义过宽的目录名。
