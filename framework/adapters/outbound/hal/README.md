# HAL 适配层说明

本目录承接 `framework/ports/outbound/hal/` 定义的硬件端口，将业务可见的 HAL 能力适配到具体硬件、第三方 SDK 或仿真实现。

## 目录结构

```text
framework/adapters/outbound/hal/
  components/         可复用的 HAL 组合逻辑，不依赖具体 SDK
  providers/
    snack/modbus/     Modbus RTU 通用链路 + VFD/语音驱动与框架适配
    snack/io_exp/     io_exp CAN IO 子板驱动与框架适配
    mcc/              电机执行器出站端口的 MCC provider 适配
  sim/                仿真实现
```

`components/` 放可复用的 HAL 组合逻辑：

- `sensor_filter/`：DI 传感器滤波与状态缓存。
- `do_group_mapper/`：DO group/slot 到 DO 端口的映射。
- `vfd_manager/`：VFD 方向、RST 与命令调度，通过 `hal_vfd_backend_ops_t` 注入具体 backend。

通用非阻塞脉冲时序原语 `pulse_out`（供 `vfd_manager` 等内部复用）不依赖任何 HAL
类型，已迁移到 `framework/common/pulse_out.*`，不属于本目录。

`providers/` 放具体外部依赖适配：

- `snack/modbus/`：
  - `drv_modbus_link.*`：Modbus RTU 链路层（连接管理、同串口多实例共享总线锁、
    失败计数与自动重连、通用读写寄存器），与具体设备无关，`drv_vfd`/`drv_voice`
    共用。
  - `drv_vfd.*`：VFD 驱动（挡位/速度 IO 组合、寄存器地址映射），只认设备协议，
    不含通信监测或事件上报语义。
  - `snack_vfd_backend.*`：把 `drv_vfd` 适配成 `hal_vfd_backend_ops_t`，供
    `components/vfd_manager` 注入使用（backend 命名见下方"命名约定"）。
  - `drv_voice.*`：语音模块驱动（曲目/音量寄存器读写），自行维护 comm_ok/事件
    通知语义（语音没有对应的组合层）。
  - `snack_voice_adapter.*`：把 `drv_voice` 直接适配成 `hal_voice_ops_t` 并注册到
    `hal_voice_port`。
- `snack/io_exp/`：
  - `io_exp_driver.*`：io_exp CAN IO 子板 SDK 封装，按名称表解析 DI/DO 句柄。
  - `snack_io_adapter.*`：把 `io_exp_driver` 直接适配成 `hal_io_ops_t` 并注册到
    `hal_io_port`；项目专属的引脚名称表（如 `projects/m8/adapters/hal/m8_io_adapter.c`）
    只负责组装 `drv_io_cfg_t` 传给 `snack_io_adapter_register()`。
- `mcc/hal_motor_exec_adapter.c`：电机执行器出站端口的 MCC provider 适配。

`sim/` 放仿真侧实现，主要用于 `BUILD_SIM=ON` 的本地运行和测试：

- `hal_io_sim.*`：数字 IO 仿真（内存态 DI/DO、脉冲计数器）。
- `hal_voice_sim.*`：语音模块仿真（无硬件，指令静默丢弃）。
- `engine_io_sim.*`：引擎 IO 后端的仿真内存实现。
- `sim_encoder_counter.*`：仿真编码器累计计数源。

## 命名约定：`adapter` vs `backend`

provider 文件命名区分两种接线方式，不是随意选词：

- **`*_adapter`**（`snack_io_adapter`、`snack_voice_adapter`）：直接实现对应 port
  的 `*_ops_t` 并调用 `*_register()` 注册到 port——port 前面没有 framework 组合层，
  provider 就是 port 的唯一实现。
- **`*_backend`**（`snack_vfd_backend`）：实现的是一个更内层的 `*_backend_ops_t`
  契约，被注入给 framework 组合层（`components/vfd_manager`），由组合层实现并
  注册对应 port。

`vfd` 之所以是 `backend` 而不是 `adapter`，是因为它前面有 `hal_vfd_manager` 这层
组合层；`io`/`voice` 目前没有对应的组合层，所以是 `adapter`。以后如果 `io`/`voice`
也长出组合层，新的注入层应该跟进叫 `backend`，而不是反过来把 `vfd` 改成
`adapter` 抹掉这个区分。

## 边界规则

- `ports/` 只放业务可见的硬件端口声明。
- `components/` 可以维护 bind、内部 tick、缓存和组合逻辑，但不能依赖具体 SDK；
  若 bind 或 tick 属于装配/内部推进能力，不应通过业务可见 port 暴露给项目层。
- `providers/` 可以依赖具体 SDK，但不承载业务语义。
- `projects/<project>/bindings/` 负责把项目配置连接到 components 和 providers。
- `projects/<project>/wiring/` 负责选择真实硬件或仿真实现。

## 典型路径

VFD 真实硬件：

```text
ports/outbound/hal
  -> components/vfd_manager
  -> providers/snack/modbus/snack_vfd_backend
  -> providers/snack/modbus/drv_vfd
  -> providers/snack/modbus/drv_modbus_link
```

VFD 仿真运行：

```text
ports/outbound/hal
  -> components/vfd_manager
  -> projects/m8/adapters/hal/hal_vfd_sim_m8
  -> sim 实现
```

语音真实硬件（无 components 组合层，provider 直接实现 port）：

```text
ports/outbound/hal
  -> providers/snack/modbus/snack_voice_adapter
  -> providers/snack/modbus/drv_voice
  -> providers/snack/modbus/drv_modbus_link
```

语音仿真运行：

```text
ports/outbound/hal
  -> sim/hal_voice_sim
```

数字 IO（无 components 组合层，provider 直接实现 port）：

```text
ports/outbound/hal
  -> providers/snack/io_exp/snack_io_adapter
  -> providers/snack/io_exp/io_exp_driver
```

项目专属的引脚名称表（如 `projects/m8/adapters/hal/m8_io_adapter.c`）只负责把
机型点表组装成 `drv_io_cfg_t` 传给 `snack_io_adapter_register()`，不重复实现
`hal_io_ops_t`。

后续接入其它硬件时，优先新增 `providers/<vendor>/` 或 provider 文件，并在项目 wiring/bindings 中完成装配；不要再使用 `hw` 这类含义过宽的目录名。
