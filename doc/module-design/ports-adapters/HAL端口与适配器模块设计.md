# HAL 端口与适配器模块设计

**版本**：v1.0  
**状态**：已落地（HAL 端口 + 通用组件 + sim 后端 + MCC/Snack 可选 provider）  
**最后同步代码**：2026-07-14（`ports/outbound/hal`、`components/sensor_filter`、`components/vfd_manager`、MCC provider、Snack io_exp/Modbus provider）  
**适用范围**：`ports/outbound/hal/`、`adapters/outbound/hal/`、`CMakeLists.txt` 可选 provider  
**架构基线**：Ports & Adapters + 通用组件组合层 + 项目 wiring 注入  
**关键词**：HAL port、hal_io、hal_sensor、hal_vfd、hal_voice、hal_motor_exec、sensor_filter、vfd_manager、MCC、Snack

---

## 1. 设计目标与核心理念

HAL 层为 domain/application 提供稳定的硬件能力边界。框架内的业务代码只依赖 `ports/outbound/hal/*` 契约；真实 SDK、总线协议、IO 板点位表和串口配置由 adapter/provider 或项目 wiring 处理。

### 1.1 设计目标

- **硬件实现可替换**：同一 port 可由 sim、MCC、Snack io_exp、Snack Modbus 或项目 provider 注册。
- **项目配置外置**：电机实例、IO 名称表、VFD 实例、串口地址、音频曲目语义均由项目侧定义。
- **组件复用**：DI 滤波和 VFD 管理是通用组合层，依赖更底层 provider 的原语能力。
- **真机 provider 可选**：需要外部 SDK 或库的 provider 默认不构建，通过 CMake 开关启用。
- **安全态可插入**：IO panic、VFD stop、voice stop、motor stop 等安全输出策略由项目 hooks 和 machine ops 组合。

### 1.2 分层模型

```text
domain / application / services
        │ hal_*_get_ops() / hal_motor_*()
        ▼
ports/outbound/hal
        ▲
        │ register ops 或端口函数实现
adapters/outbound/hal
        ├─ sim/                         测试与 Demo 后端
        ├─ components/sensor_filter     DI 滤波组合层，依赖 hal_io_port
        ├─ components/vfd_manager       VFD 组合层，依赖 backend ops
        └─ providers/
             ├─ mcc                     电机执行器 provider
             └─ snack
                  ├─ io_exp             CAN IO 子板 provider
                  └─ modbus             voice / VFD backend provider
```

**依赖禁令**：

- `domain/` 不得 include provider SDK 头文件。
- `ports/outbound/hal` 不得 include `adapters/` 实现头文件。
- provider 不得写业务命令裁决、报警等级或项目流程逻辑。
- sim 后端不得成为真机默认 wiring。

---

## 2. 端口契约

### 2.1 数字 IO：`hal_io_port`

`hal_io_ops_t` 是数字 IO 的统一出站端口，覆盖初始化、后台线程启动、DI/DO 读写、子板在线状态、名称解析、统计与硬件脉冲计数器。

| 能力 | API |
|------|-----|
| 生命周期 | `init()`、`start()` |
| 输出 | `do_set(pin, val)`、`flush_outputs_now()` |
| 输入 | `di_read(pin)` |
| 子板状态 | `board_is_online()`、`wait_boards_online()`、`register_board_status_cb()` |
| 安全回调 | `register_panic_cb()` |
| 诊断 | `try_parse_di()`、`try_parse_do()`、`di_name()`、`do_name()`、`get_stats()` |
| 编码器/脉冲 | `pulse_read()`、`pulse_clear()` |

`io_di_t` / `io_do_t` 句柄定义在 `common/io_handle.h`，项目只通过句柄和名称表表达点位，不在业务层使用 provider 原始地址。

### 2.2 DI 滤波：`hal_sensor_port`

`hal_sensor_port` 面向业务暴露稳定逻辑态，不含业务语义。

| 能力 | API |
|------|-----|
| 注册实现 | `hal_sensor_register()` |
| 初始化 | `init()` |
| 启动预热 | `warmup(sample_count)` |
| 查询稳定态 | `is_active(ch)` |

通道绑定不在 port ops 中完成，而由 `hal_sensor_filter_bind()` 在项目 wiring 阶段装配。

### 2.3 VFD：`hal_vfd_port`

`hal_vfd_port` 统一变频器控制和诊断读取。

| 能力 | API |
|------|-----|
| 生命周期 | `init()` |
| 控制 | `run(id, gear)`、`set_freq(id, hz)`、`stop(id)`、`fault_reset(id)` |
| 状态 | `get_state(id)` |
| 寄存器读 | `read(id, reg, val)`、`get_cached(id, reg, val)` |
| 事件 | `register_event_cb(id, cb)` |

`hal_vfd_gear_t` 使用正负号表达方向，绝对值表达挡位。项目定义 `hal_vfd_id_t` 的具体实例含义。

### 2.4 语音：`hal_voice_port`

语音模块是单实例设备。端口只传透曲目编号和音量原始值，业务语义由项目层映射。

| 能力 | API |
|------|-----|
| 生命周期 | `init()` |
| 播放控制 | `play(track)`、`stop()`、`pause()` |
| 音量 | `set_volume(vol)`、`volume_up()`、`volume_down()` |
| 通信状态 | `register_event_cb(cb)` |

### 2.5 电机执行器：`hal_motor_exec_port`

电机执行器采用不透明句柄 `hal_motor_exec_t`，不是 register/get ops 单例。项目 bindings 静态持有真实执行器对象，并把 `hal_motor_exec_t *` 注入设备控制模式。

| 能力 | API |
|------|-----|
| 连续运行 | `hal_motor_run_continuous(exec, motor, speed, dir)` |
| 到位运动 | `hal_motor_move_to(exec, motor, speed, dir, spec)` |
| 停止/调速 | `hal_motor_stop()`、`hal_motor_set_speed()` |
| 回原/恢复 | `hal_motor_home()`、`hal_motor_recover()` |
| 查询 | `hal_motor_phase()`、`hal_motor_position()`、`hal_motor_direction()`、`hal_motor_fault_code()` |

命令返回 `hal_motor_cmd_result_t`，`ACCEPTED` / `QUEUED` 均视为非拒绝；领域模式通过 `hal_motor_cmd_ok()` 判断是否可继续。

---

## 3. 通用组件

### 3.1 `hal_sensor_filter`

`hal_sensor_filter` 是 DI 滤波组合层，依赖已注册的 `hal_io_port`。

```text
project wiring
    ├─ hal_io provider register
    ├─ hal_sensor_filter_register()
    ├─ hal_sensor_filter_bind(ch, cfg)
    └─ hal_sensor_poll_register_task()

scheduler_start_all()
    └─ sensor poll task
          ├─ hal_io.di_read(pin)
          ├─ active_low 极性转换
          └─ trig/release 计数防抖
```

| 配置 | 含义 |
|------|------|
| `pin` | 绑定到具体 DI 句柄；`IO_HANDLE_NULL` 表示未安装 |
| `active_low` | 是否低电平有效 |
| `trig_count` | 触发确认连续采样次数，必须大于 0 |
| `release_count` | 释放确认连续采样次数，必须大于 0 |

### 3.2 `hal_vfd_manager`

`hal_vfd_manager` 是 VFD 组合层，向上注册 `hal_vfd_port`，向下绑定 provider backend。

```text
provider backend ops
        │ apply_gear / stop_outputs / read / write / set_rst / get_state
        ▼
hal_vfd_manager
        ├─ 实例槽位绑定
        ├─ RST 脉冲编排
        ├─ fault/current 慢速监测缓存
        └─ register_event_cb 事件通知
```

| 能力 | 接口 |
|------|------|
| 注册端口 | `hal_vfd_manager_register()` |
| 绑定实例 | `hal_vfd_manager_bind(id, cfg)` |
| 注册周期任务 | `hal_vfd_manager_poll_register_task()` |
| 更新监测项 | `hal_vfd_manager_set_monitor_mask(id, mask)` |

默认容量 `HAL_VFD_MANAGER_SLOT_MAX = 8`。`monitor_mask` 可组合 `HAL_VFD_MON_FAULT` 与 `HAL_VFD_MON_CURRENT`。

---

## 4. Provider 与 Sim 后端

### 4.1 Sim 后端

| 文件 | 职责 |
|------|------|
| `adapters/outbound/hal/sim/hal_io_sim.*` | 注册内存 DI/DO 后端，支持测试注入 DI 与脉冲值 |
| `adapters/outbound/hal/sim/hal_voice_sim.c` | 注册语音仿真 ops |
| `adapters/outbound/hal/sim/sim_encoder_counter.*` | 仿真编码器累计计数源 |
| `adapters/outbound/hal/sim/engine_io_sim.*` | 洗车 engine 专用 IO 仿真后端 |

Sim 后端用于 demo 与单元测试，不表达真实设备时序保证。

### 4.2 MCC 电机 provider

`adapters/outbound/hal/providers/mcc/hal_motor_exec_adapter.c` 将 `hal_motor_exec_port` 映射到 Motor Control Core。

| CMake 开关 | 依赖 |
|------------|------|
| `WDF_ENABLE_MCC_PROVIDER=ON` | `WDF_MCC_ROOT/include/motor/motor_executor.h`、`WDF_MCC_ROOT/lib/libmotor_control_core.a` |

项目 bindings 负责创建真实 MCC executor，并以 `hal_motor_exec_t *` 注入 `domain/device_control/patterns`。

### 4.3 Snack io_exp provider

Snack io_exp provider 提供 CAN IO 子板访问，并注册到 `hal_io_port`。

| 文件 | 职责 |
|------|------|
| `io_exp_driver.*` | IO 子板驱动、名称解析、后台线程、在线检测、脉冲计数 |
| `snack_io_adapter.*` | 注册 `hal_io_ops_t`，配置并启动 `drv_io_*` |

| CMake 开关 | 依赖 |
|------------|------|
| `WDF_ENABLE_SNACK_IO_EXP_PROVIDER=ON` | `WDF_IO_EXP_ROOT`、`WDF_IO_EXP_LIBRARY` |

`drv_io_cfg_t` 由项目侧提供，包含子板数量、每板点数、DI/DO 名称表。

### 4.4 Snack Modbus voice provider

Snack voice provider 通过 Modbus RTU 对接语音模块，并注册到 `hal_voice_port`。

| 文件 | 职责 |
|------|------|
| `drv_modbus_link.*` | Modbus RTU 连接封装 |
| `drv_voice.*` | 语音模块寄存器协议 |
| `snack_voice_adapter.*` | 注册 `hal_voice_ops_t`，配置串口、波特率、从站地址 |

| CMake 开关 | 依赖 |
|------------|------|
| `WDF_ENABLE_SNACK_MODBUS_PROVIDER=ON` | `libmodbus` |

### 4.5 Snack Modbus VFD backend

Snack VFD backend 通过 Modbus + IO 输出组合驱动变频器，向 `hal_vfd_manager` 提供 backend ops。

| 文件 | 职责 |
|------|------|
| `drv_vfd.*` | VFD 寄存器协议 |
| `snack_vfd_backend.*` | 每实例串口/DO/RST/速度挡位配置，绑定 manager backend |
| `hal_vfd_manager.*` | 对外注册 `hal_vfd_port` |

| CMake 开关 | 依赖 |
|------------|------|
| `WDF_ENABLE_SNACK_MODBUS_PROVIDER=ON` + `WDF_ENABLE_SNACK_IO_EXP_PROVIDER=ON` | `libmodbus` + io_exp provider |

`snack_vfd_backend_instance_cfg_t` 中的 `pin_fwd`、`pin_rev`、`pin_rst`、`pin_spd1`、`pin_spd2` 均由项目点位表提供。

---

## 5. 启动与装配顺序

推荐项目装配顺序：

```text
wiring()
    ├─ 注册 hal_io provider
    │    ├─ sim: hal_io_sim_register()
    │    └─ snack: snack_io_adapter_register()
    ├─ 注册 hal_sensor_filter
    ├─ 注册 hal_vfd_manager 或 snack_vfd_backend
    ├─ 注册 hal_voice provider
    ├─ 注册 machine_ops / engine_io / 其他端口
    └─ 返回 bootstrap

project_configure_hal()
    ├─ snack_io_adapter_configure(cfg)
    ├─ snack_vfd_backend_instance_configure(id, cfg)
    └─ snack_voice_adapter_configure(cfg)

project_bind_hal()
    ├─ hal_sensor_filter_bind(ch, cfg)
    ├─ snack_vfd_backend_instance_bind(id)
    └─ hal_vfd_manager_bind(id, cfg)

bootstrap_init()
    ├─ hal_io.init()
    ├─ hal_vfd.init()
    ├─ hal_voice.init()
    └─ project_init_hal()
         ├─ hal_sensor.init/warmup
         └─ 其他依赖 HAL init 后的项目初始化

project_register_runtime_tasks()
    ├─ hal_sensor_poll_register_task()
    └─ hal_vfd_manager_poll_register_task()

bootstrap_start()
    ├─ hal_io.start()
    └─ scheduler_start_all()
```

原则是：`wiring()` 只注册 port/provider；`configure` 注入参数；`bind` 绑定实例和通道；`init` 初始化状态和预热；`register_runtime_tasks` 登记周期任务；`start` 只启动必须进入运行态的线程或 provider。

---

## 6. 线程与安全约束

| 模块 | 线程模型 |
|------|----------|
| `event_bus` | 独立 `event_dispatch` 线程，HAL 不应在回调中长时间阻塞 |
| `hal_sensor_filter` | 通过 `periodic_task` 周期推进 |
| `hal_vfd_manager` | 通过 `periodic_task` 周期监测 fault/current 和 RST 脉冲 |
| `io_exp_driver` | provider 内部自建 IO 后台线程 |
| `hal_motor_exec_port` | 由底层 MCC executor 管理异步运动状态 |

安全约束：

- panic 回调应只做安全输出落地，不做复杂业务决策。
- `flush_outputs_now()` 用于 panic 或启动安全态，正常路径由后台线程/周期任务推进。
- `register_event_cb` 回调只传递硬件事件，不直接改变运行模式。
- 急停热路径应通过 `safety_cutout_execute()`（弱符号，项目可覆盖）或 `machine_ops.stop_all_outputs()` 统一收敛。

---

## 7. 测试覆盖

| 测试 | 覆盖 |
|------|------|
| `test_hal_io_sim` | IO sim 注册、DI/DO、脉冲计数 |
| `test_hal_sensor_filter` | DI 滤波绑定、预热、周期采样 |
| `test_hal_vfd_manager` | VFD manager 绑定、run/stop/reset、monitor |
| `test_hal_motor_exec_mcc` | MCC adapter 到 `hal_motor_exec_port` 的映射 |
| `test_snack_io_exp_adapter` | Snack io_exp driver/adapter 行为 |
| `test_snack_vfd_backend` | Snack VFD backend 与 manager 联动 |
| `test_snack_voice_adapter` | Snack voice Modbus adapter |
| `test_hal_voice_sim` | 语音 sim 注册和操作 |
| `test_sim_encoder_counter` | 仿真编码器计数 |

验证命令：

```bash
cmake --build build-native -j4
ctest --test-dir build-native --output-on-failure
```

---

## 8. 当前边界与扩展指南

### 8.1 框架已提供

- HAL 端口契约：IO、sensor、VFD、voice、motor executor。
- 通用组件：DI 滤波、VFD manager。
- 仿真后端：IO、voice、encoder、engine IO。
- 可选真机 provider：MCC、Snack io_exp、Snack Modbus voice/VFD。

### 8.2 项目侧负责

- 选择并启用 provider，提供外部 SDK 路径、库路径、串口和总线配置。
- 定义 IO 名称表、VFD 实例 ID、电机索引、语音曲目编号语义。
- 在 `wiring()` 注册 provider，在 project hooks 中完成配置、绑定、初始化和周期任务注册。
- 将硬件事件映射到报警、运行模式或 machine ops。

### 8.3 新增 provider

1. 先确认是否已有 port 能表达所需能力。
2. 若能表达，新增 `adapters/outbound/hal/providers/<vendor>/...` 并注册既有 port。
3. 若能力不足，先扩展 port 契约和测试，再实现 provider。
4. provider 只做协议/SDK 适配，不写项目报警码和业务流程。

---

## 9. 相关文档

- `doc/module-design/domain/设备控制模式模块设计.md` — 电机执行端口在领域模式中的使用边界
- `doc/module-design/domain/方案引擎模块设计.md` — engine IO 与 HAL/sim 的边界
- `doc/module-design/runtime/EventBus模块设计.md` — HAL 边沿事件与异步分发
- `doc/module-design/domain/报警系统模块设计.md` — HAL 事件到报警 registry 的项目侧映射边界
