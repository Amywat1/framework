# motor_control_core SDK 移植说明

版本：1.0.0

---

## 一、SDK 目录结构

```
motor_control_core-1.0.0-<arch>/
├── include/                            # 公开头文件
│   └── motor/                          # 按模块组织的头文件
│       └── motor_version.h             # 版本宏（构建时自动生成）
├── lib/
│   └── libmotor_control_core.a         # 静态库
└── README.md                           # 本文档
```

---

## 二、系统要求

| 项目 | 最低要求 |
|------|---------|
| C 编译器 | 支持 C11 标准（GCC 4.9+ 或 arm-none-eabi-gcc 等交叉编译工具链） |
| 操作系统 | Linux / RTOS / 裸机（库本身不依赖操作系统） |

---

## 三、集成方式

### 方式一：CMake 工程

将 SDK 解压后，通过变量指定根路径：

```cmake
set(MOTOR_ROOT /path/to/motor_control_core-1.0.0-<arch>)

target_include_directories(your_app PRIVATE ${MOTOR_ROOT}/include)
target_link_libraries(your_app PRIVATE ${MOTOR_ROOT}/lib/libmotor_control_core.a)
```

完整示例：

```cmake
cmake_minimum_required(VERSION 3.10)
project(your_project C)

set(MOTOR_ROOT /opt/sdk/motor_control_core-1.0.0-aarch64)

add_executable(your_app main.c)
target_include_directories(your_app PRIVATE ${MOTOR_ROOT}/include)
target_link_libraries(your_app PRIVATE ${MOTOR_ROOT}/lib/libmotor_control_core.a)
```

**交叉编译时**额外指定工具链文件：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/arm-toolchain.cmake
cmake --build build
```

---

### 方式二：Makefile / IDE（Keil、IAR 等）

**步骤 1**：记录 SDK 解压路径，例如 `SDK_DIR = /opt/sdk/motor_control_core-1.0.0-aarch64`。

**步骤 2**：在编译命令中添加头文件路径：

```
-I$(SDK_DIR)/include
```

**步骤 3**：在链接命令中添加库文件：

```
-L$(SDK_DIR)/lib -lmotor_control_core
```

Keil / IAR 中在工程属性的"Include Paths"和"Libraries"中填写对应路径即可。

---

## 四、版本检查

SDK 提供版本宏，可在编译期检查版本兼容性：

```c
#include "motor/motor_version.h"

#if MOTOR_VERSION_MAJOR != 1
#error "需要 motor_control_core 1.x 版本"
#endif
```

---

## 五、模块功能

### 5.1 运动控制

| 功能 | API |
|---|---|
| 持续运行 | `motor_run_continuous` |
| 运动到位（限位/位置/软限位/时间，可组合） | `motor_move_to` |
| 减速停止 | `motor_stop` |
| 暂停 / 恢复 | `motor_pause` / `motor_resume` |
| 运行中调速或改向 | `motor_set_speed` |

### 5.2 位置管理

| 功能 | API |
|---|---|
| 回原点（建立可信基准） | `motor_home` |
| 手动清零编码器 | `motor_zero_encoder` |
| 上层显式确认基准可信 | `motor_confirm_baseline` |
| 查询累计位置（脉冲） | `motor_position` |
| 查询基准是否可信 | `motor_baseline_trusted` |

### 5.3 安全机制

- **急停**：硬件急停信号触发后立即切断全部电机输出，须调用 `motor_reset_estop` 解锁。
- **看门狗**：`motor_tick` 缺拍超过 `watchdog_ms` 自动切断全部输出，须调用 `motor_reset_watchdog` 解锁。
- **停机冷却期**：停止后在 `cooldown_ms` 内禁止再次启动，保护机械和驱动器。
- **换向安全停止**：改向时自动经过 `reversal_stop_ms` 的零速等待。
- **互锁**：支持互斥（`MOTOR_INTERLOCK_MUTEX`）和前置位置（`MOTOR_INTERLOCK_PREREQ_POSITION`）两种规则，最多 8 条。
- **共享驱动器联动**：多台电机共用同一 `driver_index` 时，任一台故障后其余台自动切断（故障码 `MOTOR_FAULT_SHARED_DRIVER`）。

### 5.4 运行监测

各项监测均可独立启用，超限持续超过确认门限才判定故障：

| 监测项 | 配置字段 | 故障码 |
|---|---|---|
| 负载电流（过载/空转） | `monitor_current` | `MOTOR_FAULT_OVERCURRENT` / `MOTOR_FAULT_UNDERCURRENT` |
| 驱动器运行反馈 | `monitor_feedback` | `MOTOR_FAULT_DRIVER_FEEDBACK` |
| 过温 | `monitor_temp` | `MOTOR_FAULT_OVERTEMP` |
| 欠压 | `monitor_voltage` | `MOTOR_FAULT_UNDERVOLTAGE` |
| 编码器信号质量 | `enc_stall_ticks` / `enc_jump_max` | `MOTOR_FAULT_ENCODER_SIGNAL` |

### 5.5 故障与恢复

故障分三级：告警（不切断）→ 故障（切断锁定，可三步恢复）→ 致命（须 `motor_reinit`）。

三步恢复流程：
1. `motor_recover(exec, motor, MOTOR_RECOVERY_DRIVER_RESET)` — 复位驱动器
2. `motor_recover(exec, motor, MOTOR_RECOVERY_MODULE_STOP)` — 解锁模块
3. 重新下发运动指令

### 5.6 事件系统

支持两种消费方式，可同时使用：

- **回调**：`motor_set_event_callback`，运行于 `motor_tick` 上下文，回调内不得下发运动指令。
- **轮询**：`motor_pop_event`，在主循环中逐条取出。

事件载荷（`motor_event_t`）包含：电机编号、事件类型、触发条件、最终位置、耗时、故障码及故障分级。

### 5.7 状态查询

| API | 返回 |
|---|---|
| `motor_phase` | 当前状态（8 态枚举） |
| `motor_current_freq` | 当前输出频率（厘赫） |
| `motor_direction` | 当前方向 |
| `motor_fault_code` | 当前故障码 |
| `motor_in_safe_state` | 是否处于看门狗安全态 |

---

## 六、快速上手示例

### 示例 1：单电机持续运行

```c
#include "motor/motor_executor.h"

static motor_executor_t s_exec;
static motor_driver_t   s_drv;
static motor_driver_t  *s_drivers[1] = { &s_drv };
static motor_encoder_t *s_encoders[1] = { NULL };

static motor_clock_t  s_clock   = { .now_ms = sys_get_ms };
static motor_sensors_t s_sensors = { .limit = hw_read_limit };
static motor_estop_t  s_estop   = { .active = hw_estop_active };

void app_init(void)
{
    /* 填写驱动器端口 */
    s_drv.set_output = drv_set_output;
    s_drv.cutoff     = drv_cutoff;
    s_drv.reset      = drv_reset;
    s_drv.is_running = drv_is_running;
    s_drv.current    = drv_current;
    s_drv.ctx        = &s_hw;

    motor_config_t cfg = {0};
    cfg.motor_count              = 1;
    cfg.driver_count             = 1;
    cfg.tick_ms                  = 10;
    cfg.watchdog_ms              = 100;
    cfg.motors[0].driver_index   = 0;
    cfg.motors[0].cooldown_ms    = 300;
    cfg.motors[0].accel_ms       = 800;
    cfg.motors[0].decel_ms       = 800;
    cfg.motors[0].default_max_move_ms = 60000;

    motor_ports_t ports = {
        .clock    = &s_clock,
        .drivers  = (motor_driver_t *const *)s_drivers,
        .encoders = s_encoders,
        .sensors  = &s_sensors,
        .estop    = &s_estop,
    };

    motor_init_result_t r = motor_init(&s_exec, &cfg, &ports);
    if (!r.ok) { /* 记录 r.error，停机 */ return; }
}

/* 在 10ms 定时中断中调用 */
void timer_10ms_isr(void)
{
    motor_tick(&s_exec);
}

/* 业务层：启动电机 */
void app_start_motor(void)
{
    motor_run_continuous(&s_exec, 0,
                         motor_speed_freq(3000),   /* 30.00 Hz */
                         MOTOR_DIR_FORWARD);
}
```

---

### 示例 2：运动到位（按限位停止）

```c
void app_move_to_limit(void)
{
    motor_move_spec_t spec = {0};
    spec.use_limit  = true;
    spec.limit      = MOTOR_LIMIT_POS;   /* 触碰正向限位时停止 */
    spec.max_time_ms = 20000;            /* 20 秒超时兜底 */

    motor_move_to(&s_exec, 0,
                  motor_speed_freq(2000),
                  MOTOR_DIR_FORWARD,
                  &spec);
}

/* 在主循环中轮询事件 */
void app_poll_events(void)
{
    motor_event_t ev;
    while (motor_pop_event(&s_exec, &ev)) {
        if (ev.type == MOTOR_EVENT_ARRIVED) {
            /* 正常到位，ev.trigger == MOTOR_END_LIMIT */
        } else if (ev.type == MOTOR_EVENT_TIMEOUT) {
            /* 超时兜底，检查机械状态 */
        } else if (ev.type == MOTOR_EVENT_FAULT) {
            /* 故障，ev.fault 记录原因，ev.level 记录分级 */
        }
    }
}
```

---

### 示例 3：故障三步恢复

```c
void app_recover_motor(int motor)
{
    /* 第一步：驱动器复位 */
    motor_recover(&s_exec, motor, MOTOR_RECOVERY_DRIVER_RESET);

    /* 等待驱动器复位完成（按驱动器手册，通常数百毫秒）
     * 等待期间继续调用 motor_tick                         */

    /* 第二步：解锁模块 */
    motor_recover(&s_exec, motor, MOTOR_RECOVERY_MODULE_STOP);

    /* 之后可重新下发运动指令 */
}
```

---

### 示例 4：两电机共用一台变频器（接触器切换）

适用场景：一台变频器通过两个接触器分别控制电机 A 和电机 B，同一时刻只有一台电机运行。

#### 适配层（接触器切换逻辑放在 prepare 中）

```c
typedef struct {
    vfd_hw_t *hw;
    int       motor_id;   /* 0=电机A，1=电机B */
} drv_ctx_t;

static drv_ctx_t s_ctx_a = { .hw = &s_vfd, .motor_id = 0 };
static drv_ctx_t s_ctx_b = { .hw = &s_vfd, .motor_id = 1 };

/* set_output / cutoff / reset / is_running / current 操作同一台变频器 */
static void drv_set_output(void *ctx, int freq_centi_hz, motor_direction_t dir)
{
    drv_ctx_t *c = ctx;
    vfd_set_freq_dir(c->hw, freq_centi_hz, dir == MOTOR_DIR_FORWARD);
}

static void drv_cutoff(void *ctx)
{
    drv_ctx_t *c = ctx;
    vfd_stop_output(c->hw);
    /* 不操作接触器；接触器由 prepare 统一管理 */
}

/* prepare：切换接触器到本电机 */
static bool drv_prepare(void *ctx)
{
    drv_ctx_t *c = ctx;
    /* 断开另一侧，闭合本侧 */
    contactor_open(c->motor_id == 0 ? CONTACTOR_B : CONTACTOR_A);
    contactor_close(c->motor_id == 0 ? CONTACTOR_A : CONTACTOR_B);
    hw_delay_ms(20);                        /* 等接触器稳定 */
    return contactor_feedback_ok(c->motor_id);
}

static motor_driver_t s_drv_a = {
    .set_output = drv_set_output, .cutoff = drv_cutoff,
    .reset = drv_reset, .prepare = drv_prepare,
    .is_running = drv_is_running, .current = drv_current,
    .ctx = &s_ctx_a,
};
static motor_driver_t s_drv_b = {
    .set_output = drv_set_output, .cutoff = drv_cutoff,
    .reset = drv_reset, .prepare = drv_prepare,
    .is_running = drv_is_running, .current = drv_current,
    .ctx = &s_ctx_b,
};
static motor_driver_t *s_drivers2[2] = { &s_drv_a, &s_drv_b };
```

#### 配置（互斥互锁 + prep_required）

```c
motor_config_t cfg = {0};
cfg.motor_count  = 2;
cfg.driver_count = 2;
cfg.tick_ms      = 10;
cfg.watchdog_ms  = 100;

/* 电机A：driver_index=0，启动前切换接触器 */
cfg.motors[0].driver_index  = 0;
cfg.motors[0].prep_required = true;
cfg.motors[0].cooldown_ms   = 500;   /* 给接触器分断留裕量 */
cfg.motors[0].accel_ms      = 1000;
cfg.motors[0].decel_ms      = 1000;
cfg.motors[0].default_max_move_ms = 30000;

/* 电机B：driver_index=1，启动前切换接触器 */
cfg.motors[1].driver_index  = 1;
cfg.motors[1].prep_required = true;
cfg.motors[1].cooldown_ms   = 500;
cfg.motors[1].accel_ms      = 1000;
cfg.motors[1].decel_ms      = 1000;
cfg.motors[1].default_max_move_ms = 30000;

/* 互斥互锁：两台电机不能同时处于运行相关态 */
cfg.interlocks[0] = (motor_interlock_t){ MOTOR_INTERLOCK_MUTEX, 0, 1 };
cfg.interlocks[1] = (motor_interlock_t){ MOTOR_INTERLOCK_MUTEX, 1, 0 };
cfg.interlock_count = 2;
```

#### 运行控制

```c
/* 启动电机A：MCC 自动调用 prepare 切换接触器后开始运行 */
motor_run_continuous(&s_exec, 0, motor_speed_freq(3000), MOTOR_DIR_FORWARD);

/* 切换到电机B：先停A，再启B；MCC 等冷却期和互锁通过后自动切换接触器 */
motor_stop(&s_exec, 0);
motor_run_continuous(&s_exec, 1, motor_speed_freq(2500), MOTOR_DIR_FORWARD);
```

> **接触器切换时序保证**：`cooldown_ms` 内变频器输出已关断，接触器可安全分断；之后 MCC 调用 `prepare` 完成闭合，再驱动变频器输出。`prepare` 返回 `false` 时进入 `MOTOR_FAULT_PREPARE_FAILED` 故障。

---

## 七、注意事项

1. **静态库，无运行时依赖**：所有代码内嵌到目标可执行文件，无需在目标设备上额外部署文件。

2. **C11 标准**：目标工程编译器须支持 C11（`-std=c11`），否则可能出现编译错误。

3. **中断上下文**：SDK 中的 API 不得在中断服务程序（ISR）中直接调用，除非头文件注释中明确声明支持中断上下文。

4. **线程安全**：各模块线程安全性在对应头文件中注明，未注明的接口默认不支持并发调用。

5. **架构匹配**：包名中的 `<arch>` 须与目标平台一致（如 `aarch64`、`armv7l`）。架构不匹配时链接报错，需使用对应架构的 SDK 包或重新交叉编译。

---

## 八、常见问题

**Q：编译报 "No such file or directory: motor/motor_executor.h"？**

A：检查 `-I` 或 `target_include_directories` 指向的是 SDK 的 `include/` 目录（而非 `include/motor/`）。

**Q：链接报 "undefined reference to `motor_executor_xxx`"？**

A：确认链接命令中 `-lmotor_control_core` 位于目标文件之后，或 CMake 中 `target_link_libraries` 正确指向 `.a` 的绝对路径。

**Q：链接报架构不匹配？**

A：SDK 中的 `.a` 须与目标架构一致。请使用包名中架构字段对应的 SDK 包，或联系 SDK 维护方获取对应架构的预编译包。
