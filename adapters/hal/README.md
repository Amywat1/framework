# adapters/hal — HAL 适配层

## 设计思想

本层的核心目标是**让业务逻辑与硬件实现彻底解耦**。

业务层（domain / service）只通过 `ports/hal/` 下的接口操作硬件，
接口本身是纯函数指针结构体，不包含任何平台名称或机型名称。
具体的 IO 子板型号、VFD 串口参数、电机接线方式，全部集中在
`adapters/machine/<机型>/` 的配置文件里，bootstrap 阶段一次性注入，
运行期不再改变。

这样带来两个直接好处：一是换硬件只改适配层，业务逻辑零修改；
二是仿真构建只需替换 IO/VFD 的"状态数组"实现，其余逻辑原样跑通。

---

## 目录结构与分层

```
adapters/hal/
├── linux_hw/        平台层——依赖真机 SDK（CAN IO 子板库、Modbus 库）
│   ├── drv/             硬件 SDK 的直接封装，仅供本目录内部使用
│   │   ├── drv_io       CAN IO 子板：输入刷新、输出落地、在线检测、全板掉线恢复
│   │   └── drv_vfd      变频器 Modbus RTU：多实例、自动重连、RST 脉冲保护
│   ├── hal_io_linux     将 drv_io 注册为 hal_io_port 的实现
│   └── hal_vfd_linux    将 drv_vfd 注册为 hal_vfd_port 的实现，提供逐实例初始化接口
│
├── generic/         组合层——不依赖任何 SDK，只通过 port 接口操作
│   ├── hal_motor        电机控制：支持 VFD 与纯 DO 两种驱动方式
│   ├── hal_sensor       DI 防抖滤波：计数式确认/释放，逐通道参数化
│   └── hal_do_group     DO 分组输出：group × slot 二维映射
│
└── sim_hw/          仿真层——替换平台层供测试使用
    ├── hal_io_sim       IO 状态数组，外部注入 DI 值以模拟传感器输入
    ├── hal_vfd_sim      VFD 状态机，记录当前转向供场景断言使用
    ├── hal_motor_sim    电机仿真，用 sim_encoder_counter 替代硬件脉冲读取
    └── sim_encoder_counter  场景测试通过此接口注入仿真脉冲
```

**端口定义在 `ports/hal/`（与本目录同级）：**
`hal_io_port.h` / `hal_vfd_port.h` / `hal_motor_port.h` /
`hal_sensor_port.h` / `hal_do_group_port.h` / `hal_motor_bind.h`

端口是唯一的跨层接口，任何层都不得跳过端口直接访问下层实现。

---

## 三层职责边界

| 层 | 知道什么 | 不知道什么 |
|----|----------|-----------|
| **平台层**（linux_hw） | SDK API、串口路径、板卡型号 | 电机叫什么名字、哪条 DI 是限位 |
| **组合层**（generic） | port 接口语义 | SDK、平台、机型拓扑 |
| **仿真层**（sim_hw） | 仿真状态机内部结构 | 真机 SDK、机型配置 |
| **机型层**（machine/m8） | M8 全部硬件拓扑 | port 实现细节 |

组合层是本设计的关键：`hal_motor.c` / `hal_sensor.c` / `hal_do_group.c`
不含任何平台相关代码，因此**同一份代码同时跑在真机和仿真两个构建目标上**，
无需为仿真单独维护一套逻辑副本。

---

## bind 机制

`generic/` 的三个模块都采用**运行时 bind** 而非编译期配置表：

```
bootstrap 阶段
  m8_motor_setup()
    └── hal_motor_bind(motor_id, &cfg)   ← 注入 VFD 实例 id、IO 引脚、编码器参数
  m8_sensor_setup()
    └── hal_sensor_bind(ch, &cfg)        ← 注入 DI 引脚、极性、防抖计数
  m8_water_setup()
    └── hal_do_group_bind(group, slot, pin) ← 注入每路水阀的 DO 引脚

运行期
  motor_move(MOTOR_GANTRY, speed)
    └── hal_motor_port → generic/hal_motor → 查 slot → hal_vfd_port.run_fwd()
```

bind 函数只在 bootstrap 阶段由 `adapters/machine/<机型>/` 调用，
业务层永远不接触 bind 接口。
bind 配置结构体定义在 `ports/hal/hal_motor_bind.h`，
其中 `vfd_backend_id` 的具体取值由机型配置层定义（见 `config/machine/m8_vfd_table.h`），
port 层只使用 `typedef int hal_vfd_id_t`，不含机型名称。

---

## 可靠性设计要点

- **drv_io**：输出写入先存缓冲，后台线程异步落地；子板掉线后恢复时自动重发输出状态；
  全板离线触发 panic 回调，由系统完成安全态落地后 abort。
- **drv_vfd**：Modbus 断连后自动重试；RST 脉冲宽度由 drv 内部 worker 控制，
  不阻塞调用方；故障码和电流值缓存在 drv 内，读取无 Modbus IO。
- **hal_sensor**：计数式防抖，触发/释放分别配置确认次数，
  避免单次毛刺触发告警逻辑。
- **初始化顺序**：bind 必须在 HAL 注册之后、业务 init 之前完成，
  bootstrap.c 中通过 `BOOT_CHECK` 宏强制检查每步返回值。

---

## 移植简要

移植到新平台只需做三件事：

1. **新建 `adapters/hal/<平台>/`**，实现 `hal_io` 和 `hal_vfd` 两个适配文件，
   将新平台的 SDK 注册到对应 port。`generic/` 三个文件无需修改，直接复用。

2. **新建 `adapters/machine/<机型>/`**，编写 setup 文件描述该机型的硬件拓扑
   （哪台电机用哪个 VFD、哪条 DI 接哪个通道、水阀 DO 引脚分配）。

3. **更新 `wiring.c`**，注册新平台的 io/vfd 实现和 generic 的 motor/sensor/do_group 实现。

仿真构建只需替换 io 和 vfd 为 sim 版本，其余不动。
