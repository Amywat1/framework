# hal_io_sim 单元测试报告

| 属性 | 值 |
|------|-----|
| 测试目标 | `test_hal_io_sim` |
| 源文件 | `tests/adapters/test_hal_io_sim.c` |
| 被测代码 | `adapters/outbound/hal/sim/hal_io_sim.c` |
| 关联端口 | `ports/outbound/hal/hal_io_port.h`、`ports/port_registry.c` |
| 最后执行 | 2026-07-09 |
| 结果 | **23/23 通过** |

## 测试环境

- 每个用例 `setUp()` 调用 `hal_io_sim_register()` 重置全局仿真状态
- 有效句柄约定：`board=1`（范围 1~7），`pin=1`（范围 1~32）
- 无效句柄：`board=0` 或 `board>=8` 或 `pin=0`

## 用例明细

### A. DI 读写（4 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_di_read_default_false` | 初始 DI 为低电平 | `di_read` 返回 false | 通过 |
| `test_set_di_level_true_and_read` | 注入高电平后可读 | set true → read true | 通过 |
| `test_set_di_level_false_and_read` | 注入低电平后可读 | set true → set false → read false | 通过 |
| `test_di_invalid_board_returns_false` | 无效 board 静默忽略 | `board=0` 读写均 false | 通过 |

### B. DO 输出（2 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_do_set_valid_returns_ok` | 有效 DO 写入 | 返回 `SW_OK` | 通过 |
| `test_do_set_invalid_board_returns_err` | 无效 DO 拒绝 | 返回 `SW_ERR_PARAM` | 通过 |

### C. 脉冲计数器（5 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_pulse_read_after_set_counter` | 注入计数后可读 | set 500 → read 500 | 通过 |
| `test_pulse_clear_zeros_counter` | 清零计数器 | clear 后 read 0 | 通过 |
| `test_pulse_read_invalid_pin_returns_negative` | 无效引脚读失败 | 返回值 < 0 | 通过 |
| `test_pulse_clear_invalid_pin_returns_err` | 无效引脚清失败 | 返回 `SW_ERR_PARAM` | 通过 |
| `test_pulse_counter_init_clears_value` | 重新 register 清零 | register 后计数为 0 | 通过 |

### D. 实用接口（5 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_board_is_online_always_true` | 仿真板始终在线 | 返回 true | 通过 |
| `test_flush_outputs_returns_ok` | 刷输出空操作 | 返回 `SW_OK` | 通过 |
| `test_wait_boards_online_returns_ok` | 等待在线空操作 | 返回 `SW_OK` | 通过 |
| `test_get_stats_valid` | 统计接口可用 | `stats.online == true` | 通过 |
| `test_get_stats_null_returns_err` | 空指针保护 | 返回 `SW_ERR_PARAM` | 通过 |

### E. 边界场景（7 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_di_pin_zero_returns_false` | pin=0 无效 | 读写均 false | 通过 |
| `test_di_board_max_boundary` | board 边界 | board=7 有效，board=8 无效 | 通过 |
| `test_do_pin_zero_returns_err` | DO pin=0 无效 | 返回 `SW_ERR_PARAM` | 通过 |
| `test_two_di_pins_independent` | 多 DI 引脚隔离 | pin1/pin2 状态互不影响 | 通过 |
| `test_two_pulse_counters_independent` | 多计数器隔离 | 各引脚计数独立 | 通过 |
| `test_start_returns_ok` | start 空操作 | 返回 `SW_OK` | 通过 |
| `test_pulse_counter_max_value` | 大值存储 | INT_MAX 值原样读出 | 通过 |

## 未覆盖项

| 场景 | 说明 |
|------|------|
| 回调注册 | `register_debug_input_cb` 等为空实现，无测试价值 |
| 引脚名解析 | `try_parse_di/do`、`di_name/do_name` 恒失败/NULL |
| DO 实际电平回读 | 仿真仅写内存，无 DO 读接口 |

## 待补充

> 新增用例时在此追加。

| 用例 | 目的 | 状态 |
|------|------|------|
| — | — | — |
