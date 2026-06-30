# HAL 适配层单元测试说明

**测试日期**：2026-06-30  
**测试范围**：`adapters/hal/generic/` 与 `adapters/hal/sim_hw/`  
**测试结果**：133 个用例全部通过，0 失败

---

## 测试架构

### 分层覆盖策略

```
adapters/hal/
├── generic/          ← 平台无关通用实现（依赖 hal_io_port 接口）
│   ├── hal_sensor.c      → test_hal_sensor    20 用例
│   ├── hal_motor.c       → test_hal_motor     35 用例
│   └── hal_do_group.c    → test_hal_do_group  13 用例
└── sim_hw/           ← PC 仿真实现（纯内存，无硬件依赖）
    ├── hal_io_sim.c      → test_hal_io_sim    23 用例
    ├── hal_motor_sim.c   → test_hal_motor_sim 21 用例
    └── hal_vfd_sim.c     → test_hal_vfd_sim   21 用例
```

### 测试隔离方式

| 层 | 隔离手段 |
|----|---------|
| generic/ | 注册 Mock `hal_io_ops_t`（只包含测试所需函数指针），隔离底层硬件 |
| sim_hw/ | 直接链接仿真实现，注册后通过 `hal_*_get_ops()` 调用，无额外桩 |

### 构建与运行

```bash
cmake -B build_sim -DBUILD_SIM=ON
cmake --build build_sim -j4
ctest --test-dir build_sim -R 'test_hal' -V
```

---

## hal_sensor（DI 通道防抖滤波）— 20 个用例，全部通过

> 防抖原理：对每个 DI 通道维护独立的稳定计数器，连续采样达到 `trig_count` 次才确认
> 激活，连续采样达到 `release_count` 次才确认释放；`active_low=true` 时极性取反。

### 测试 Mock 设计

```c
static bool s_mock_di = false;
static bool mock_di_read(io_di_t pin) { (void)pin; return s_mock_di; }
static const hal_io_ops_t s_mock_io_ops = { .di_read = mock_di_read };
```

setUp 流程：注册 mock IO → `hal_sensor_generic_register()` → `ops->init()` 重置运行时状态。

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| **A. 绑定参数校验** |||
| `test_bind_null_cfg` | 传入 NULL cfg → SW_ERR_PARAM | ✓ |
| `test_bind_null_pin` | pin = IO_HANDLE_NULL → SW_ERR_PARAM | ✓ |
| `test_bind_zero_trig_count` | trig_count = 0 → SW_ERR_PARAM | ✓ |
| `test_bind_zero_release_count` | release_count = 0 → SW_ERR_PARAM | ✓ |
| `test_bind_valid` | 合法参数返回 SW_OK | ✓ |
| **B. 防抖计数逻辑** |||
| `test_not_active_before_trig_count` | tick 次数 < trig_count 时通道不激活 | ✓ |
| `test_active_after_trig_count` | 连续 tick 达到 trig_count 后通道激活 | ✓ |
| `test_stays_active_before_release_count` | 激活后 tick 次数 < release_count 时保持激活 | ✓ |
| `test_released_after_release_count` | 连续 tick 达到 release_count 后通道释放 | ✓ |
| `test_interrupt_trig_resets_count` | 触发过程中信号抖动：计数器归 1，须重新积累 trig_count | ✓ |
| **C. active_low 极性** |||
| `test_active_low_low_level_is_active` | active_low=true 时低电平触发激活 | ✓ |
| `test_active_low_high_level_is_inactive` | 激活后高电平触发释放 | ✓ |
| **D. 边界与异常** |||
| `test_unbound_channel_returns_false` | 从未绑定的通道 is_active() 返回 false | ✓ |
| `test_init_resets_runtime_state` | init() 重置已激活通道的运行时状态，绑定配置保留 | ✓ |
| `test_no_io_ops_tick_does_not_crash` | hal_io 未注册时 tick() 静默跳过，不崩溃 | ✓ |
| **E. 补充场景** |||
| `test_trig_count_1_single_tick_activates` | trig_count=1 时一次 tick 即激活 | ✓ |
| `test_multiple_channels_are_independent` | ch0（trig=1）激活不影响 ch1（trig=3）的独立计数 | ✓ |
| `test_rebind_overwrites_config` | 对同一通道重新绑定，新 trig_count 立即生效 | ✓ |
| `test_active_low_with_debounce` | active_low=true 且 trig_count=3：低电平须稳定 3 次才激活 | ✓ |
| `test_stable_count_ceiling_does_not_break_filter` | 连续 tick 300 次，stable_count 上限 255 不溢出，通道保持激活 | ✓ |

---

## hal_motor（通用电机 HAL）— 35 个用例，全部通过

> 通用电机适配层：依赖 `hal_io_ops_t`（DO 方向控制、DI 限位读取、脉冲计数器），
> VFD 速度控制及诊断通过绑定时注入的回调实现（`set_speed` / `read_current` /
> `read_status` / `fault_reset`），无 VFD 时回调为 NULL，退化为纯 DO 控制。

### 测试 Mock 设计

```c
/* DO 写入状态（pin 编号为索引）*/
static bool s_do_state[4];
static sw_err_t mock_do_set(io_do_t pin, bool val) { s_do_state[pin_no] = val; return SW_OK; }

/* DI 读取状态 */
static bool s_di_state[4];
static bool mock_di_read(io_di_t pin) { return s_di_state[pin_no]; }

/* 脉冲计数器 */
static int s_pulse_val = 100;
static int mock_pulse_read(io_di_t pin) { (void)pin; return s_pulse_val; }
static sw_err_t mock_pulse_clear(io_di_t pin) { (void)pin; return SW_OK; }
```

setUp 流程：重置 mock 状态 → 注册 mock IO → `hal_motor_generic_register()` → `hal_motor_bind(0, &k_base_cfg)`。

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| **A. 绑定参数校验** |||
| `test_bind_null_cfg` | NULL cfg → SW_ERR_PARAM | ✓ |
| `test_bind_negative_id` | motor_id < 0 → SW_ERR_PARAM | ✓ |
| `test_bind_id_out_of_range` | motor_id ≥ HAL_MOTOR_BIND_SLOT_MAX → SW_ERR_PARAM | ✓ |
| **B. DO 方向输出** |||
| `test_set_output_cw` | speed_ref > 0：CW=true，CCW=false | ✓ |
| `test_set_output_ccw` | speed_ref < 0：CCW=true，CW=false | ✓ |
| `test_set_output_stop` | speed_ref = 0：CW=false，CCW=false，STOP=true | ✓ |
| **C. 速度回调模式（VFD 注入）** |||
| `test_set_speed_callback_invoked` | set_speed 回调被调用，speed_ref 正确传入 | ✓ |
| `test_set_speed_callback_stop` | speed_ref=0 时回调传入 0 | ✓ |
| **D. 限位检测** |||
| `test_at_fwd_limit_true` | CW 限位 DI=true → at_fwd_limit 返回 true | ✓ |
| `test_at_fwd_limit_false` | CW 限位 DI=false → at_fwd_limit 返回 false | ✓ |
| `test_at_rev_limit_true` | CCW 限位 DI=true → at_rev_limit 返回 true | ✓ |
| `test_at_fwd_limit_no_di` | limit_io_cw = IO_HANDLE_NULL → 始终返回 false | ✓ |
| **E. 脉冲计数器** |||
| `test_read_hw_pulse_ok` | pulse_read 返回 100 → SW_OK，值正确 | ✓ |
| `test_read_hw_pulse_comm_negative` | pulse_read 返回负值 → SW_ERR_COMM | ✓ |
| `test_read_hw_pulse_comm_sentinel` | pulse_read 返回 0x0FFFFFFF → SW_ERR_COMM | ✓ |
| `test_read_hw_pulse_null_ptr` | NULL 输出指针 → SW_ERR_PARAM | ✓ |
| `test_read_hw_pulse_no_encoder` | has_encoder=false → SW_ERR_PARAM | ✓ |
| `test_clear_hw_pulse_ok` | pulse_clear 成功 → SW_OK | ✓ |
| `test_clear_hw_pulse_no_encoder` | has_encoder=false → SW_ERR_PARAM | ✓ |
| **F. 电流 / 状态 / 故障复位回调** |||
| `test_read_current_no_callback` | read_current = NULL → SW_ERR_NOT_SUPPORT | ✓ |
| `test_read_current_with_callback` | 回调返回 1234，SW_OK | ✓ |
| `test_read_status_no_callback` | read_status = NULL → SW_ERR_NOT_SUPPORT | ✓ |
| `test_read_status_with_callback` | 回调返回 5678，SW_OK | ✓ |
| `test_fault_reset_no_callback` | fault_reset = NULL → SW_ERR_NOT_SUPPORT | ✓ |
| `test_fault_reset_with_callback` | 回调被调用，SW_OK | ✓ |
| **G. 无效 ID** |||
| `test_set_output_invalid_id` | id = HAL_MOTOR_BIND_SLOT_MAX → SW_ERR_NOT_INIT | ✓ |
| `test_at_fwd_limit_invalid_id` | id 越界 → 返回 false | ✓ |
| **H. 补充场景** |||
| `test_at_rev_limit_false` | CCW 限位 DI=false → at_rev_limit 返回 false | ✓ |
| `test_read_current_null_ptr` | 有回调但输出指针为 NULL → SW_ERR_PARAM | ✓ |
| `test_read_status_null_ptr` | 有回调但输出指针为 NULL → SW_ERR_PARAM | ✓ |
| `test_set_output_no_io_do_set` | io_ops 中 do_set=NULL → SW_ERR_NOT_INIT | ✓ |
| `test_read_hw_pulse_no_pulse_read_op` | io_ops 中 pulse_read=NULL → SW_ERR_NOT_INIT | ✓ |
| `test_clear_hw_pulse_no_pulse_clear_op` | io_ops 中 pulse_clear=NULL → SW_ERR_NOT_INIT | ✓ |
| `test_set_speed_callback_error_propagates` | set_speed 回调返回 SW_ERR_COMM，set_output 向上传播 | ✓ |
| `test_set_output_partial_do_config` | 仅配置 CW 引脚，CCW/STOP=NULL：set_output 跳过无效引脚，返回 SW_OK | ✓ |

---

## hal_do_group（DO 组×槽位控制）— 13 个用例，全部通过

> 二维绑定表（最大 8 组 × 4 槽），槽位与 DO 物理引脚在 bootstrap 阶段绑定；
> 业务层通过 `slot_set()` 控制单个槽位，`all_off()` 关闭所有已绑定 DO。

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| **A. 绑定参数校验** |||
| `test_bind_invalid_group` | group ≥ HAL_DO_GROUP_MAX → SW_ERR_PARAM | ✓ |
| `test_bind_invalid_slot` | slot ≥ HAL_DO_SLOT_MAX → SW_ERR_PARAM | ✓ |
| `test_bind_null_pin_marks_unbound` | 绑定 IO_HANDLE_NULL：返回 SW_OK，但该槽位 slot_set 返回 SW_ERR_NOT_INIT | ✓ |
| `test_bind_valid` | 合法参数绑定成功 | ✓ |
| **B. 槽位输出控制** |||
| `test_slot_set_on` | slot_set(on=true)：mock do_set 接收 true | ✓ |
| `test_slot_set_off` | slot_set(on=false)：mock do_set 接收 false | ✓ |
| `test_slot_set_unbound_returns_not_init` | 未绑定有效引脚的槽位 → SW_ERR_NOT_INIT | ✓ |
| `test_slot_set_invalid_group_returns_not_init` | group 越界 → SW_ERR_NOT_INIT | ✓ |
| **C. 全部关闭** |||
| `test_all_off_clears_all_bound` | 已置高的多个槽位经 all_off 后全部置低 | ✓ |
| **D. 补充场景** |||
| `test_max_valid_group_and_slot` | 最大合法下标（group=7, slot=3）绑定和输出正常 | ✓ |
| `test_rebind_slot_changes_pin` | 同一槽位二次绑定后，slot_set 作用于新引脚，旧引脚不受影响 | ✓ |
| `test_slot_set_no_io_ops_returns_err` | hal_io 未注册时 slot_set → SW_ERR_NOT_INIT | ✓ |
| `test_all_off_no_io_ops_returns_err` | hal_io 未注册时 all_off 遍历全部绑定槽位，返回首个 SW_ERR_NOT_INIT | ✓ |

---

## hal_io_sim（数字 IO 仿真后端）— 23 个用例，全部通过

> 纯内存实现，内部维护三张二维数组（DO 状态 / DI 状态 / 脉冲计数器），
> 板号范围 1~7，引脚范围 1~32；注册时自动调用 `sim_io_init()` 清空全部状态。

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| **A. DI 读写** |||
| `test_di_read_default_false` | 初始化后所有 DI 默认为 false | ✓ |
| `test_set_di_level_true_and_read` | `hal_io_sim_set_di_level(pin, true)` → di_read 返回 true | ✓ |
| `test_set_di_level_false_and_read` | 设为 true 后再设为 false → di_read 返回 false | ✓ |
| `test_di_invalid_board_returns_false` | board=0（无效）→ set 静默忽略，di_read 返回 false | ✓ |
| **B. DO 输出** |||
| `test_do_set_valid_returns_ok` | 合法 DO 句柄 → SW_OK | ✓ |
| `test_do_set_invalid_board_returns_err` | board=0 → SW_ERR_PARAM | ✓ |
| **C. 脉冲计数器** |||
| `test_pulse_read_after_set_counter` | `set_pulse_counter(pin, 500)` → pulse_read 返回 500 | ✓ |
| `test_pulse_clear_zeros_counter` | pulse_clear 后 pulse_read 返回 0 | ✓ |
| `test_pulse_read_invalid_pin_returns_negative` | 无效句柄 → pulse_read 返回负值 | ✓ |
| `test_pulse_clear_invalid_pin_returns_err` | 无效句柄 → pulse_clear 返回 SW_ERR_PARAM | ✓ |
| `test_pulse_counter_init_clears_value` | 重新 register（内部调用 init）后计数器归零 | ✓ |
| **D. 实用接口** |||
| `test_board_is_online_always_true` | 仿真模式始终在线 | ✓ |
| `test_flush_outputs_returns_ok` | 空操作，返回 SW_OK | ✓ |
| `test_wait_boards_online_returns_ok` | 仿真无需等待，立即返回 SW_OK | ✓ |
| `test_get_stats_valid` | 返回 SW_OK，online=true | ✓ |
| `test_get_stats_null_returns_err` | NULL 输出指针 → SW_ERR_PARAM | ✓ |
| **E. 补充边界场景** |||
| `test_di_pin_zero_returns_false` | pin=0（无效，须 > 0）→ di_read 返回 false | ✓ |
| `test_di_board_max_boundary` | board=7（合法最大）正常读写；board=8（越界）静默忽略 | ✓ |
| `test_do_pin_zero_returns_err` | pin=0 → do_set 返回 SW_ERR_PARAM | ✓ |
| `test_two_di_pins_independent` | 同一板上不同引脚状态互不干扰 | ✓ |
| `test_two_pulse_counters_independent` | 不同引脚的脉冲计数器相互独立 | ✓ |
| `test_start_returns_ok` | start() 空操作，返回 SW_OK | ✓ |
| `test_pulse_counter_max_value` | 写入 INT_MAX（0x7FFFFFFF）后可原值读回，无截断崩溃 | ✓ |

---

## hal_motor_sim（电机仿真 HAL）— 21 个用例，全部通过

> 仿真实现依赖 `hal_io_sim`（限位 DI 读取）和 `sim_encoder_counter`（编码器计数）。
> `hal_motor_sim_register()` 在注册时清空所有槽位并重置编码器计数器。

### 测试前提

setUp 顺序：`hal_io_sim_register()` → `hal_motor_sim_register()` → `hal_motor_sim_bind(0, &cfg)`。

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| **A. 绑定参数校验** |||
| `test_bind_null_cfg` | NULL cfg → SW_ERR_PARAM | ✓ |
| `test_bind_negative_id` | motor_id < 0 → SW_ERR_PARAM | ✓ |
| `test_bind_id_out_of_range` | motor_id ≥ HAL_MOTOR_BIND_SLOT_MAX → SW_ERR_PARAM | ✓ |
| **B. 速度输出** |||
| `test_set_output_no_callback_returns_ok` | 无 set_speed 回调时返回 SW_OK（速度记录到 slot->speed_ref）| ✓ |
| `test_set_output_with_callback` | set_speed 回调被调用，speed_ref 正确传入 | ✓ |
| `test_set_output_invalid_id` | 越界 id → SW_ERR_NOT_INIT | ✓ |
| **C. 限位检测（依赖 hal_io_sim DI）** |||
| `test_at_fwd_limit_true` | 通过 `hal_io_sim_set_di_level` 置高 → at_fwd_limit 返回 true | ✓ |
| `test_at_fwd_limit_false` | DI=false → at_fwd_limit 返回 false | ✓ |
| `test_at_rev_limit_true` | DI=true → at_rev_limit 返回 true | ✓ |
| **D. 编码器脉冲计数器** |||
| `test_read_hw_pulse_ok` | `sim_encoder_counter_add_pulse(0, 300)` → read_hw_pulse 返回 300 | ✓ |
| `test_clear_hw_pulse_ok` | clear_hw_pulse 后 read_hw_pulse 返回 0 | ✓ |
| `test_read_hw_pulse_no_encoder` | has_encoder=false → SW_ERR_PARAM | ✓ |
| **E. 回调（电流 / 故障复位）** |||
| `test_read_current_no_callback` | read_current = NULL → SW_ERR_NOT_SUPPORT | ✓ |
| `test_read_current_with_callback` | 回调返回 2000，SW_OK | ✓ |
| `test_fault_reset_no_callback` | fault_reset = NULL → SW_ERR_NOT_SUPPORT | ✓ |
| `test_fault_reset_with_callback` | 回调被调用，SW_OK | ✓ |
| **F. 补充场景** |||
| `test_at_rev_limit_false` | CCW 限位 DI=false → at_rev_limit 返回 false | ✓ |
| `test_at_fwd_limit_null_di_returns_false` | limit_io_cw=IO_HANDLE_NULL，DI 置高也返回 false | ✓ |
| `test_read_hw_pulse_null_ptr` | NULL 输出指针 → SW_ERR_PARAM | ✓ |
| `test_encoder_accumulates_multiple_adds` | 两次 add_pulse（100 + 250）→ 读取 350 | ✓ |
| `test_read_status_no_callback` | read_status = NULL → SW_ERR_NOT_SUPPORT | ✓ |

---

## hal_vfd_sim（变频器仿真 HAL）— 21 个用例，全部通过

> M8 机型配置两个 VFD 实例：HAL_VFD_GANTRY（id=0，支持正反转）和
> HAL_VFD_BRUSH（id=1，仅正转）。内部以 `s_state[2]` 维护运行状态。

| 测试用例 | 验证内容 | 结果 |
|---------|---------|------|
| **A. 初始化** |||
| `test_init_gantry_stopped` | init 后龙门 VFD 状态为 STOPPED | ✓ |
| `test_init_brush_stopped` | init 后刷子 VFD 状态为 STOPPED | ✓ |
| **B. 运行 / 停止 / 故障复位** |||
| `test_run_fwd_gantry` | run(GANTRY, gear=+1) → state = FWD | ✓ |
| `test_run_rev_gantry` | run(GANTRY, gear=-1) → state = REV | ✓ |
| `test_run_gear_zero_stops` | run(GANTRY, gear=0) → state = STOPPED | ✓ |
| `test_stop_returns_stopped` | 运行中调用 stop → state = STOPPED | ✓ |
| `test_fault_reset_returns_stopped` | 运行中调用 fault_reset → state = STOPPED | ✓ |
| `test_set_freq_returns_ok` | set_freq 为无副作用操作，返回 SW_OK | ✓ |
| **C. 读取接口** |||
| `test_read_returns_ok_and_zero` | read(STATE) → SW_OK，值为 0（仿真固定返回 0）| ✓ |
| `test_get_cached_returns_ok_and_zero` | get_cached(CURRENT) → SW_OK，值为 0 | ✓ |
| `test_read_null_ptr_returns_err` | NULL 输出指针 → SW_ERR_PARAM | ✓ |
| **D. 参数校验（无效 ID）** |||
| `test_run_invalid_id_returns_err` | 无效 id → SW_ERR_PARAM | ✓ |
| `test_brush_rev_invalid` | 刷子不支持反转（gear < 0）→ SW_ERR_PARAM | ✓ |
| `test_stop_invalid_id_returns_err` | 无效 id → SW_ERR_PARAM | ✓ |
| `test_fault_reset_invalid_id_returns_err` | 无效 id → SW_ERR_PARAM | ✓ |
| **E. 补充场景** |||
| `test_get_state_invalid_id_returns_stopped` | 无效 id 的 get_state → 返回安全默认值 STOPPED | ✓ |
| `test_brush_run_fwd` | run(BRUSH, gear=+1) → state = FWD | ✓ |
| `test_set_freq_invalid_id_returns_err` | 无效 id → SW_ERR_PARAM | ✓ |
| `test_get_cached_null_ptr_returns_err` | NULL 输出指针 → SW_ERR_PARAM | ✓ |
| `test_register_event_cb_does_not_crash` | 传入 NULL 回调不崩溃（仿真回调为空操作）| ✓ |
| `test_state_unchanged_after_set_freq` | 运行中调用 set_freq 不改变运行状态 | ✓ |

---

## 测试中发现的代码问题

| 问题 | 文件 | 描述 | 修复 |
|------|------|------|------|
| `hal_io_sim_register` 未在头文件中声明 | `hal_io_sim.h` | 函数在 `.c` 中定义但头文件只声明了 `set_di_level` 和 `set_pulse_counter`，测试文件 `#include` 头文件后编译报 implicit declaration | 在 `hal_io_sim.h` 补充 `void hal_io_sim_register(void)` 声明 |
