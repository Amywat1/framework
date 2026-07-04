# adapters/hal — HAL 适配层

## 设计思想

本层的核心目标是**让业务逻辑与硬件实现彻底解耦**。

业务层（domain / application / services）只通过 `framework/ports/outbound/hal/` 下的接口操作硬件，
接口本身是纯函数指针结构体，不包含任何平台名称或机型名称。
具体的 IO 子板型号、VFD 串口参数、电机接线方式，全部集中在
`projects/<project>/config/`、`projects/<project>/bindings/` 和
`projects/<project>/adapters/` 中，bootstrap 阶段一次性注入，
运行期不再改变。

这样带来两个直接好处：一是换硬件只改适配层，业务逻辑零修改；
二是仿真构建只需替换 IO/VFD 的"状态数组"实现，其余逻辑原样跑通。

---

## 目录结构与分层

```
adapters/hal/
├── providers/       可选 SDK provider，真机目标按需选择，仿真目标不拉入
│   ├── mcc/         Motor Control Core provider 适配器
│   └── snack/       Snack SDK provider 集合
│       ├── io_exp/
│       │   └── io_exp_driver  CAN IO 子板：输入刷新、输出落地、上线/下线对称防抖、
│       │                       全板离线触发 panic（写安全态后 abort）；配置通过 drv_io_cfg_t 注入；
│       │                       透出 pulse_read / pulse_clear 供编码器硬件脉冲计数使用
│       └── modbus/
│           ├── drv_vfd      变频器 Modbus RTU：多实例、自动重连、方向切换延时保护、
│           │                RST 脉冲或 Modbus 写寄存器两路故障复位
│           └── drv_voice    语音模块 Modbus RTU：播放/暂停/音量控制、通信失败自动重连
│
├── linux_hw/        Linux 真机平台层（不直接包含 Snack SDK 头）
│   ├── hal_io_linux     用 X-macro 展开 m8_io_table.h 构建 IO 名称表，通过 drv_io_cfg_t
│   │                    注入 io_exp_driver，并将其注册为 hal_io_port 的实现
│   ├── hal_vfd_linux    将 drv_vfd 实例数组注册为 hal_vfd_port 的实现
│   └── hal_voice_linux  将 drv_voice 实例注册为 hal_voice_port 的实现
│
├── generic/         组合层——不依赖任何 SDK，只通过 port 接口操作
│   ├── hal_motor        电机控制：set_speed / read_current / read_status / fault_reset
│   │                    通过绑定时注入的回调实现，无 VFD 项目将回调置 NULL 退化为纯 DO 控制
│   ├── hal_sensor       DI 防抖滤波：计数式确认/释放，逐通道参数化
│   └── hal_do_group     DO 分组输出：group × slot 二维映射
│
└── sim_hw/          仿真层——替换平台层供 sim 构建和单元测试使用
    ├── hal_io_sim       IO 状态数组，外部注入 DI 值以模拟传感器输入；
    │                    包含独立脉冲计数器数组，实现 pulse_read / pulse_clear 仿真
    ├── hal_motor_sim    电机仿真，用 sim_encoder_counter 替代硬件脉冲读取
    ├── hal_voice_sim    语音模块仿真，指令静默丢弃（无音频输出），返回 SW_OK
    ├── engine_io_sim    引擎通用 IO 仿真后端，供 engine_runtime 单元测试和场景测试使用
    └── sim_encoder_counter  仿真编码器脉冲源；测试通过 sim_encoder_counter_add_pulse 注入脉冲
```

项目级 VFD 仿真（例如 `projects/m8/adapters/hal/hal_vfd_sim_m8.c`）保留在项目目录，
因为它包含机型 VFD 实例数量、反转能力等拓扑差异。

**端口定义在 `framework/ports/outbound/hal/`：**
`hal_io_port.h` / `hal_vfd_port.h` / `hal_motor_port.h` /
`hal_sensor_port.h` / `hal_do_group_port.h` / `hal_motor_bind.h`

端口是唯一的跨层接口，任何层都不得跳过端口直接访问下层实现。

---

## 三层职责边界

| 层 | 知道什么 | 不知道什么 |
|----|----------|-----------|
| **provider 层**（providers/<sdk>） | SDK API、板卡型号 | 电机叫什么名字、哪条 DI 是限位 |
| **平台层**（linux_hw） | Linux 真机外设组合、串口路径 | 业务流程语义 |
| **组合层**（generic） | port 接口语义 | SDK、平台、机型拓扑 |
| **仿真层**（sim_hw） | 仿真状态机内部结构 | 真机 SDK、机型配置 |
| **项目层**（projects/<project>/config/bindings/adapters） | 项目硬件拓扑、点表、实例配置 | framework 端口实现细节 |

组合层是本设计的关键：`hal_motor.c` / `hal_sensor.c` / `hal_do_group.c`
不含任何平台相关代码，因此**同一份代码同时跑在真机和仿真两个构建目标上**，
无需为仿真单独维护一套逻辑副本。

---

## bind 机制

`generic/` 的三个模块都采用**运行时 bind** 而非编译期配置表：

```
bootstrap 阶段
  m8_motor_setup()
    └── hal_motor_bind(motor_id, &cfg)   ← 注入 VFD 回调（set_speed / read_current 等）、
                                            drv_ctx（如 vfd_id）、IO 引脚、编码器参数
  m8_sensor_setup()
    └── hal_sensor_bind(ch, &cfg)        ← 注入 DI 引脚、极性、防抖计数
  m8_water_setup()
    └── hal_do_group_bind(group, slot, pin) ← 注入每路水阀的 DO 引脚

运行期
  motor_move(MOTOR_GANTRY, speed)
    └── hal_motor_port → generic/hal_motor → 查 slot → cfg.set_speed(speed, drv_ctx)
                                                          （set_speed 为 NULL 时走 DO 方向控制）
```

bind 函数只在 bootstrap 阶段由 `projects/<project>/bindings/` 或项目 wiring 调用，
业务层永远不接触 bind 接口。
bind 配置结构体定义在 `framework/ports/outbound/hal/hal_motor_bind.h`；
VFD 操作（速度设定、电流读取、故障复位等）通过 `hal_motor_bind_cfg_t` 中的四个函数指针
（`set_speed` / `read_current` / `read_status` / `fault_reset`）和透传上下文 `drv_ctx` 注入，
`generic/hal_motor` 本身不依赖 `hal_vfd_port`，实现与 VFD 的彻底解耦。

---

## 可靠性设计要点

- **io_exp_driver**：输出写入先存缓冲，后台线程异步落地；上线和下线均采用对称防抖
  （各需连续 N 次检测才确认状态变化），防止 CAN 总线抖动误触发；子板恢复上线后
  自动重发输出状态；全板离线属于硬件全失联 panic 例外路径：先调用 panic_cb
  （由调用方将输出缓冲置安全态），再无条件写入全部子板（不依赖在线状态），最后
  abort 由进程管理器拉起。panic_cb 必须幂等、短路径、不可阻塞。
- **drv_vfd / hal_vfd**：Modbus 断连后自动重试；RST 脉冲宽度由 generic/hal_vfd
  的 tick 推进；故障码和电流值缓存读取不发起 Modbus IO。VFD tick 应注册为
  runtime periodic task，而不是挂靠在业务线程中。
- **hal_sensor**：计数式防抖，触发/释放分别配置确认次数，
  避免单次毛刺触发告警逻辑。
- **初始化顺序**：bind 必须在 HAL 注册之后、业务 init 之前完成，
  bootstrap.c 中通过 `BOOT_CHECK` 宏强制检查每步返回值。

---

## 移植简要

移植到新平台只需做三件事：

1. **新建 `framework/adapters/outbound/hal/<平台>/`**，实现 `hal_io` 和 `hal_vfd` 两个适配文件，
   将新平台的 SDK 注册到对应 port。`generic/` 三个文件无需修改，直接复用。

2. **新建或扩展 `projects/<project>/config/` 与 `projects/<project>/bindings/`**，
   编写 setup 文件描述该机型的硬件拓扑
   （哪台电机用哪个 VFD、哪条 DI 接哪个通道、水阀 DO 引脚分配）。

3. **更新 `wiring.c`**，注册新平台的 io/vfd 实现和 generic 的 motor/sensor/do_group 实现。

仿真构建只需替换 io 和 vfd 为 sim 版本，其余不动。
