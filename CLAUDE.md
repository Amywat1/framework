# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

**M8 龙门式洗车机控制软件**，运行于 RK3399 + Ubuntu Linux，遵循《恒致创造嵌入式软件开发规范 QRS-0002-2026》开发。
- 语言：C99 / C++17 混合（`app_main.cpp` 为 C++ 入口，其余为 C）
- 构建：CMake 3.10+，目标平台为 Linux
- 框架：snack SDK（回调入口为 `app_main()`，非 `main()`）

规范原文：`恒致创造嵌入式软件开发规范 QRS-0002-2026.pdf`（仓库根目录）。

---

## 构建命令

```bash
# 交叉编译（目标板）
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4

# PC 模拟器（不依赖硬件驱动）
cmake -B build_sim -DBUILD_SIM=ON
cmake --build build_sim -j4

# 发布包（命名：M8_v0.1.0_<git-hash>）
cmake --build build --target release
```

版本号在 `common/sw_version.h` 中定义，禁止多处散写。

---

## 目录架构与调用关系

```
┌─────────────────────────────────────────────────────────┐
│                      app/（业务层）                       │
│  app_main.cpp  app_fsm.c  app_cloud.c                    │
│  app_wash_engine.c  app_wash_steps.c                     │
├─────────────────────────────────────────────────────────┤
│                   component/（组件层）                    │
│  comp_brush  comp_gantry  comp_top_lift  comp_water      │
├─────────────────────────────────────────────────────────┤
│                    bsp/（板级层）                         │
│           bsp_hal.c  bsp_alarm.c  bsp_init.c              │
├─────────────────────────────────────────────────────────┤
│                   driver/（驱动层）                       │
│       drv_io  drv_vfd  drv_stepper                       │
├─────────────────────────────────────────────────────────┤
│                   common/（基础层）                       │
└─────────────────────────────────────────────────────────┘
     以上各层严格向下调用，禁止反向或跨层调用

═══════════════════════════════════════════════════════════
          service/（跨层服务，所有层均可调用）
          svc_alarm.c          svc_param.c
═══════════════════════════════════════════════════════════
```

**调用规则**：
- 主体层级严格从上到下：`app → component → bsp → driver → common`
- `service/` 是跨层服务（报警引擎、参数管理），`app`/`component`/`bsp` 层均可调用
- `config/` 是只读配置数据，任何层可引用

---

## 各模块职责

### app/（业务层）
| 文件 | 职责 |
|------|------|
| `app_main.cpp` | 程序入口，初始化序列，注册 CLI 调试命令（app/bsp/alarm/param） |
| `app_fsm.c/h` | 设备 FSM（INIT→IDLE→RUN→COMPLETE→FAULT→STOP），初始化所有组件 |
| `app_cloud.c/h` | 阿里云 MQTT 初始化、状态上报、下行消息解析 |
| `app_wash_engine.c/h` | 洗车流程引擎，非阻塞 pthread，按步骤表驱动执行机构 |
| `app_wash_steps.c/h` | 洗车步骤表数据（标准洗 8 步、快洗 6 步） |

### component/（组件层）
| 文件 | 职责 |
|------|------|
| `comp_brush.c/h` | 刷子管理，决定何时切换顶刷/侧刷，调用 `hal_brush_select()` 执行接触器切换 |
| `comp_gantry.c/h` | 龙门行走，原子位置计数，阻塞式归位 |
| `comp_top_lift.c/h` | 顶刷升降，批量脉冲（50 个/批）+ 批间限位检查 |
| `comp_water.c/h` | 水路组合控制（预洗/刷洗/高压） |

### bsp/（板级层）
| 文件 | 职责 |
|------|------|
| `bsp_init.c/h` | 系统初始化序列（顺序固定，见下节） |
| `bsp_hal.c/h` | 硬件统一入口（`hal_*` 接口），持有两个 `drv_vfd_t` 实例，接触器切换逻辑在此层 |
| `bsp_alarm.c/h` | M8 报警适配，向 svc_alarm 注入 IO 轮询/驱动事件/急停复位回调 |

### service/（跨层服务）
| 文件 | 职责 |
|------|------|
| `svc_alarm.c/h` | 报警引擎（防抖、等级、多种恢复策略），配置表驱动 |
| `svc_param.c/h` | 参数管理（cJSON，线程安全，持久化到 JSON 文件） |

### driver/（驱动层）
| 文件 | 职责 |
|------|------|
| `drv_io.c/h` | CAN IO 子板，封装 io_exp SDK |
| `drv_vfd.c/h` | 士林变频器驱动（handle 参数化：串口、Modbus 地址、IO 引脚），不含业务名称 |
| `drv_stepper.c/h` | 雷赛步进电机驱动（IO 脉冲控制） |

---

## 关键硬件设计

### IO 子板（CAN 总线）
- SDK：`io_exp`，初始化：`io_init("can0", 1000000, 0x10, 6)`，上电后必须等待 2s
- IO 引脚编号直接使用图纸 DO/DI 编号（见 `driver/drv_io.h`）
- 输入通过回调更新缓存（`drv_io_register_input_cb`）；输出通过 `drv_io_do_set()`

### VFD 变频器（士林）
- **双模控制**：IO 数字输出控制启/停/方向，Modbus RTU 设置频率
- 刷子 VFD（地址 1）与龙门 VFD（地址 2）共享 `/dev/ttyS1`，由 `bsp_hal` 分别持有两个 `drv_vfd_t` 实例
- 三路刷子共用一台 VFD，通过接触器切换（逻辑在 `bsp_hal.c` 的 `hal_brush_select()`）：
  - H29（DO_TOP_BRUSH_ACT）= 顶刷，H28（DO_SIDE_BRUSH_ACT）= 侧刷
  - 切换时必须先停 VFD，等待 200ms 后再闭合目标接触器
- 士林 Modbus 寄存器：0x2001（频率，0.01Hz）、0x2102（故障码）

### 顶刷升降（雷赛步进电机）
- IO 脉冲控制：H26（DO_TOP_LIFT_PUL）、DO8（DO_TOP_LIFT_DIR）、DO7（DO_TOP_LIFT_ENA）
- 脉宽由 `CFG_STEPPER_PULSE_US`（100µs）决定，批量 50 脉冲为一组、组间检查限位

### 龙门位置检测
- DI3（后限位）、DI4（前限位）
- DI9（码盘脉冲）：IO 回调触发，`comp_gantry_encoder_tick()` 更新原子计数器
- 急停：DI13，常闭接法，`hal_is_estop_active()` 返回 `!drv_io_di_read(DI_ESTOP)`

---

## 系统初始化顺序（`bsp_system_init()`）

1. `svc_param_init()` — 先加载参数（后续硬件初始化可使用参数值）
2. `io_init()` — IO 子板 CAN 初始化
3. `sleep(2)` — 等待 IO 子板稳定（**不可省略**）
4. `hal_init()` — Modbus 连接、VFD 复位
5. `svc_alarm_init()` — 报警引擎
6. `bsp_alarm_init()` — 注册 M8 报警回调（依赖 hal 和 svc_alarm 就绪）

之后 `app_main()` 调用 `app_fsm_init()`（组件 + FSM 线程）和 `app_cloud_init()`（MQTT）。

---

## 洗车流程引擎

- **非阻塞**：`wash_engine_start(mode)` 创建独立 pthread
- 步骤由 `app_wash_steps.h` 中的 `WashStepConfig_t` 表驱动，每步描述：龙门频率/方向、刷子/水路开关、退出条件（限位/脉冲位置/超时）
- 单步最长超时：`STEP_TIMEOUT_MS = 120000`（2 分钟）
- 紧急停止：`wash_engine_emergency_stop()` 设置原子标志 + 立即停所有执行机构

---

## 报警系统（数据/引擎分离）

- **配置表**：`config/alarm_config.h` — 只读 const 表，急停必须在第 0 行（`ALARM_EMC_TABLE_IDX = 0`）
- **引擎**：`service/svc_alarm.c` — 防抖、等级判断、恢复逻辑
- **M8 回调**：`bsp/bsp_alarm.c` — IO 轮询、驱动事件直报、急停复位、VFD 故障读取
- 两种触发路径：IO 轮询类用 `svc_alarm_set_raw_trigger()`，驱动事件类用 `svc_alarm_set_state()`
- 新增报警：在 `alarm_config.h` 追加行 → 在 `bsp_alarm.c` 添加触发逻辑

---

## 参数管理

- 持久化文件：`/home/neardi/m8/params.json`（cJSON，pthread mutex 保护）
- 常用接口：`svc_param_get_int(key, default)` / `svc_param_set_int(key, val)` / `svc_param_save()`
- 参数键名统一定义在 `service/svc_param.h`（`PARAM_KEY_*`），禁止在模块内硬写字符串

---

## CLI 调试命令

| 命令域 | 示例 | 说明 |
|--------|------|------|
| `app` | `app status` / `app order` / `app reset` | FSM 状态查询、命令下发 |
| `bsp` | `bsp do 16 1` / `bsp di 4` / `bsp gantry_fwd 2500` | 硬件直控 |
| `alarm` | `alarm status` / `alarm reset` | 报警查询与复位 |
| `param` | `param get washMode` / `param set washMode 1` / `param save` | 参数读写 |

---

## 命名规范（§4）

| 类型 | 规则 | 示例 |
|------|------|------|
| 宏/常量 | 全大写+下划线 | `MOTOR_MAX_RPM` |
| 函数 | 小写+下划线，`模块_动词_对象` | `motor_set_speed()` |
| 全局变量 | `g_` 前缀 | `g_system_state` |
| 文件内静态变量 | `s_` 前缀 | `s_init_flag` |
| 类型定义 | 小写+`_t` 后缀 | `motor_state_t` |
| 文件名 | 全小写+下划线，体现层级前缀 | `app_fsm.c` / `drv_motor.c` |

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
 * @file    app_fsm.c
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

**分支模型**：`dev`（主开发）/ `feature/<功能名>` / `release/vX.Y.Z` / `hotfix/<描述>`

---

## 安全原则（§3.2）

涉及电机、水泵、电磁阀等执行机构时，必须：
1. 上电默认所有输出为安全状态（关断）
2. 关键输出具备超时强切断机制
3. 通信中断后设备能进入预设安全状态
4. 掉电恢复后关键参数一致、可控
