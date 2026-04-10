# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

**M8 龙门式洗车机控制软件**，运行于 RK3399 + Ubuntu Linux，遵循《恒致创造嵌入式软件开发规范 QRS-0002-2026》开发。
- 语言：C99 / C++17 混合（`app_main.cpp` 为 snack SDK 回调入口，其余为 C）
- 构建：CMake 3.10+，目标平台为 Linux；`BUILD_SIM=ON` 构建 PC 仿真可执行文件
- 框架：snack SDK（真机回调入口为 `app_main()`，非 `main()`）；PC 仿真入口为 `tools/simulator/sim_main.c`

规范原文：`恒致创造嵌入式软件开发规范 QRS-0002-2026.pdf`（仓库根目录）。

---

## 构建命令

```bash
# 交叉编译（目标板）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4

# PC 模拟器（不依赖硬件驱动）
# 需要系统安装 libcjson-dev：sudo apt install libcjson-dev
cmake -B build_sim -DBUILD_SIM=ON
cmake --build build_sim -j4

# 发布包（命名：M8_v0.2.0_<git-hash>）
cmake --build build --target release
```

版本号在 `common/sw_version.h` 中定义，禁止多处散写。

---

## 目录架构（六边形架构 / Ports & Adapters）

```
src/
├── core/                   核心基础设施（无业务逻辑）
│   ├── event_bus/          事件总线（FIFO 环形队列，pub/sub）
│   ├── scheduler/          线程注册表 + 统一创建（bootstrap 调用）
│   └── bootstrap/          启动序列（wiring.c 真机 / wiring_sim.c 仿真）
│
├── domain/                 领域核心（纯业务逻辑，零硬件依赖）
│   ├── model/              值对象（alarm_code.h, device_state.h, wash_types.h…）
│   ├── safety/             安全域（alarm_core, safety_fsm, interlock）
│   ├── device/             执行机构（gantry, brush, top_lift, water, gate）
│   └── process/            洗车流程（step_engine, recipe）
│
├── application/            应用编排（订阅事件，协调各域）
│   └── orchestrators/      device_fsm, wash_orchestrator,
│                           safety_supervisor, report_aggregator
│
├── ports/                  端口接口（抽象 HAL / 存储 / 命令 / 云端）
│   ├── hal/                hal_motion_port, hal_sensor_port,
│   │                       hal_water_port, hal_indicator_port, hal_io_port
│   ├── storage/            param_store, deploy_store
│   └── cloud/              report_port, command_port
│
├── adapters/               适配器（实现 ports/ 接口）
│   ├── hal/
│   │   ├── linux_hw/       真机 HAL（Modbus/IO/步进，仅真机构建）
│   │   └── sim_hw/         仿真 HAL（返回 SW_OK，可注入虚拟传感器值）
│   ├── storage/json/       cJSON 存储实现（param_store + deploy_store）
│   ├── cloud/aliyun/       阿里云 MQTT 适配器（仅真机构建）
│   ├── ui/                 command_bridge（命令→event_bus）+ mqtt_command_parser
│   └── machine/m8/         m8_alarm_adapt（IO 轮询 + 急停复位回调注入）
│
├── service/                跨层服务（dev_ctx 状态快照，svc_param 参数管理）
│
├── config/                 只读配置数据（任意层可引用）
│   ├── threading/          thread_config.h（线程栈/优先级/周期）
│   ├── recipes/            standard_wash_recipe.h / quick_wash_recipe.h
│   ├── machine/            m8_machine_config.h（硬件参数：串口/波特率/地址）
│   ├── features/           m8_features.h（功能安装开关）
│   └── deployment/         deploy_store 存储路径等
│
└── tests/
    ├── unit/               单元测试（alarm_core, event_bus, safety_fsm,
    │                       step_engine, dev_ctx）
    └── scenario/           场景集成测试（standard_wash, estop,
                            vfd_fault, limit_error）

common/                     基础层（log, sw_error, event_types, time_util）
tools/simulator/            PC 仿真入口（sim_main.c, sim_console.c）
```

**调用规则（严格单向）**：
- `application/ → domain/ → ports/` → `adapters/` 实现 `ports/`
- `domain/` 与 `application/` 通过 `event_bus` 解耦（事件驱动，不直接调用）
- `service/` 跨层可用（dev_ctx / svc_param），不持有硬件依赖
- `domain/` 和 `application/` 禁止直接引用 `drv_io`, `drv_vfd`, `drv_stepper`（违反端口隔离）
- `config/` 只读，任意层可 `#include`

---

## 各模块职责

### core/event_bus/
事件总线：零动态内存 FIFO 环形队列，`event_publish()` / `event_subscribe()`。
发布方设 type + param，timestamp 由总线自动填入。分发由 `event_dispatch_thread` 驱动，永不在中断上下文调用。

### core/scheduler/
`thread_register()` 登记各模块线程参数，`bootstrap.c` 最后统一调用 `scheduler_start_all()` 创建。

### core/bootstrap/
- `bootstrap.c`：23 步启动序列（见下节）
- `wiring.c`（真机）/ `wiring_sim.c`（仿真）：纯注册，port → adapter 依赖注入，不做硬件操作

### domain/safety/
- `alarm_core`：防抖 / 等级 / 恢复策略引擎。两路触发：
  - `alarm_core_set_raw_trigger()` — IO 轮询类（经防抖计时后激活）
  - `alarm_core_set_state()` — 驱动事件直报（立即激活）
  - 激活/清除时发布 `EVT_ALARM_TRIGGERED` / `EVT_ALARM_CLEARED`
- `safety_fsm`：订阅报警事件，OK / WARNING / LOCKOUT 状态机，发布 `EVT_SAFETY_LOCKOUT` 等
- `interlock`：运动互锁检查（有 ERROR 报警时禁止运动指令）

### domain/device/
`gantry`, `brush`, `top_lift`, `water`, `gate`：通过 `ports/hal/` 接口控制执行机构，含 interlock 检查。

### domain/process/
- `step_engine`：同步执行单步（apply_step → wait_exit），在 wash_worker_thread 中调用
- `recipe`：按模式返回步骤表（standard_wash / quick_wash）

### application/orchestrators/
| 文件 | 职责 |
|------|------|
| `device_fsm` | 设备 FSM（IDLE/RUN/FAULT/STOP），订阅命令/安全/流程事件 |
| `wash_orchestrator` | 洗车编排，持有信号量驱动 wash_worker_thread |
| `safety_supervisor` | 订阅安全事件 → 更新 dev_ctx 安全状态 |
| `report_aggregator` | 云端周期上报，cloud_thread 持续轮询 is_connected() |

### service/
| 文件 | 职责 |
|------|------|
| `dev_ctx` | 设备状态只读快照（pthread mutex 保护），分片写入 |
| `svc_param` | 参数管理，委托 `param_store_ops` 读写；未注册时返回默认值 |

### adapters/machine/m8/
- `m8_alarm_adapt`：注册 IO 轮询回调（每 tick 读取 DI 状态调用 alarm_core）和急停复位回调
- `m8_linux_hw_init`（真机）：VFD Modbus 通道初始化 + 步进驱动初始化

---

## 关键硬件设计

### IO 子板（CAN 总线）
- SDK：`io_exp`，初始化：`io_init("can0", 1000000, 0x10, 6)`，上电后必须等待 2s
- IO 引脚编号直接使用图纸 DO/DI 编号（见 `adapters/hal/linux_hw/` 中的引脚映射）
- 输入通过回调更新缓存；输出通过 `drv_io_do_set()`

### VFD 变频器（士林）
- **双模控制**：IO 数字输出控制启/停/方向，Modbus RTU 设置频率
- 刷子 VFD（地址 1）与龙门 VFD（地址 2）共享 `/dev/ttyS1`，由 `m8_hal_ctx.c` 持有实例
- 三路刷子共用一台 VFD，通过接触器切换（`hal_motion_ops_t.brush_select()`）：
  - 顶刷：HAL_BRUSH_TOP，侧刷：HAL_BRUSH_SIDE
  - 切换时必须先停 VFD，等待 200ms 后再闭合目标接触器
- 士林 Modbus 寄存器：0x2001（频率，0.01Hz）、0x2102（故障码）

### 顶刷升降（雷赛步进电机）
- IO 脉冲控制：脉宽由 `CFG_STEPPER_PULSE_US`（100µs）决定
- 批量 50 脉冲为一组、组间检查限位（`hal_sensor_ops_t.lift_at_bottom/top()`）

### 龙门位置
- 前/后限位通过 `hal_sensor_ops_t.gantry_at_fwd/rev_limit()` 查询
- 位置计数委托给 `hal_sensor_ops_t.get_gantry_pos()`（HAL 层原子累加码盘脉冲）
- 急停：常闭接法，`hal_sensor_ops_t.is_estop_active()` 返回 `!DI_ESTOP`

---

## 系统启动序列（`bootstrap_run()`，共 23 步）

1. `time_util_init()` — 时间戳基准
2. `event_bus_init()` — 事件总线
3. `drv_io_init()` — IO 子板 CAN（**仿真跳过**）
4. `wiring()` / `wiring_sim()` — port → adapter 注册
5. `m8_linux_hw_init()` — VFD/步进初始化（**仿真跳过**）
6. `svc_param_init()` — 加载参数（文件缺失时降级使用默认值）
7. `dev_ctx_init()` — 状态快照清零
8. `m8_boot_profile_init()` — 等待 IO 子板 + DO 置安全态（**仿真跳过**）
9. `alarm_core_init()` — 报警引擎清零
10. `m8_alarm_adapt_init()` — 注册 IO 轮询 + 急停复位回调
11. `safety_fsm_init()` — 安全状态机（订阅报警事件）
12. 设备组件初始化：brush / gantry / top_lift / water / gate
13. `safety_supervisor_init()` — 订阅安全事件 → 更新 dev_ctx
14. `device_fsm_init()` — 设备 FSM（订阅命令/安全/流程事件）
15. `wash_orchestrator_init()` — 注册 wash_worker_thread
16. `report_aggregator_init()` — 注册 cloud_thread
17. 加载部署配置（SN、MQTT 凭证；文件缺失时跳过）
18. `aliyun_command_adapter_init()` — 连接 MQTT（**仿真跳过**）
19. `cli_adapter_init()` — 注册 CLI 命令域（**仿真跳过**）
20-21. 注册 event_dispatch_thread + io_poll_thread
22. `scheduler_start_all()` — 创建所有线程

---

## 洗车流程

- **event_driven**：`EVT_CMD_ORDER` → `device_fsm` → `wash_orchestrator_start()` → 唤醒 `wash_worker_thread`
- 步骤表在 `config/recipes/standard_wash_recipe.h` / `quick_wash_recipe.h`（8 步 / 6 步）
- 每步结构：`wash_step_config_t`（龙门频率/方向、刷子/水路开关、升降、退出条件）
- 单步最长超时：`WASH_STEP_TIMEOUT_MS = 120000ms`（2 分钟）
- 中止：`wash_orchestrator_abort()` → `step_engine_abort()` → 原子标志，wait_exit 检测

---

## 报警系统（数据/引擎分离）

- **报警码**：`domain/model/alarm_code.h`（纯常量，无逻辑）
- **引擎**：`domain/safety/alarm_core.c` — 防抖、等级、恢复策略（配置表内嵌）
- **M8 回调**：`adapters/machine/m8/m8_alarm_adapt.c` — 注册 IO 轮询 / 急停复位
- 两种触发路径：IO 轮询类用 `alarm_core_set_raw_trigger()`，驱动事件类用 `alarm_core_set_state()`
- 新增报警：在 `alarm_core.c` 配置表追加行 → 在 `m8_alarm_adapt.c` 添加触发逻辑

---

## 参数管理

- 持久化文件：`/home/neardi/m8/params.json`（路径在 `adapters/storage/json/json_param_store_cfg.h`）
- 接口：`svc_param_get_int(key, default)` / `svc_param_set_int(key, val)` / `svc_param_save()`
- 参数键名统一定义在 `service/svc_param/svc_param.h`（`PARAM_KEY_*`），禁止在模块内硬写字符串
- 未注册 param_store 时，读取返回 default_val，写入/保存静默失败（降级安全）

---

## CLI 调试命令（真机构建）

| 命令域 | 示例 | 说明 |
|--------|------|------|
| `device` | `device status` / `device order 0` / `device reset` | FSM 状态查询、命令下发 |
| `safety` | `safety status` / `safety reset` | 安全状态与报警查询 |
| `param` | `param get washMode` / `param set washMode 1` / `param save` | 参数读写 |
| `diag` | `diag io` / `diag vfd` | 硬件诊断直读 |

---

## 命名规范（§4）

| 类型 | 规则 | 示例 |
|------|------|------|
| 宏/常量 | 全大写+下划线 | `MOTOR_MAX_RPM` |
| 函数 | 小写+下划线，`模块_动词_对象` | `motor_set_speed()` |
| 全局变量 | `g_` 前缀 | `g_system_state` |
| 文件内静态变量 | `s_` 前缀 | `s_init_flag` |
| 类型定义 | 小写+`_t` 后缀 | `motor_state_t` |
| 文件名 | 全小写+下划线，体现层级前缀 | `device_fsm.c` / `hal_motion_port.h` |

---

## 代码硬性要求（§5）

- **缩进**：4 个空格，禁用 Tab（`.clang-format` 已配置）
- **花括号**：不得省略，即使单行控制语句
- **头文件**：必须有 `#ifndef` 包含保护；被 C++ 文件包含时加 `#ifdef __cplusplus extern "C"` 保护
- **魔法数字**：禁止裸数值，必须用宏/常量/枚举
- **返回值**：关键函数返回值不得忽略；硬件/通信/存储操作须有明确错误返回
- **超时**：所有等待硬件状态、通信响应必须设置超时上限
- **动态内存**：产品固件禁止运行期使用 `malloc/free`（cJSON 内部除外）
- **编译告警**：CMake 已配置 `-Werror`，新增代码零 warning

---

## 注释规范（§6）

每个 `.c` / `.h` 文件必须有文件头注释：
```c
/**
 * @file    device_fsm.c
 * @brief   设备顶层有限状态机
 * @author  <姓名>
 * @date    YYYY-MM-DD
 */
```

对外公开接口必须有 Doxygen 注释（`@brief @param @retval`）。

---

## 版本与 Git（§7 §8）

**Commit 格式**：`<类型>: <简短描述>`
- 类型：`feat` / `fix` / `docs` / `refactor` / `test` / `chore`
- 禁止：`update`、`修改`、`fix bug`、`111` 等无意义描述

**分支模型**：`main`（主干）/ `feature/<功能名>` / `release/vX.Y.Z` / `hotfix/<描述>`

---

## 安全原则（§3.2）

涉及电机、水泵、电磁阀等执行机构时，必须：
1. 上电默认所有输出为安全状态（关断）
2. 关键输出具备超时强切断机制
3. 通信中断后设备能进入预设安全状态
4. 掉电恢复后关键参数一致、可控
