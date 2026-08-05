# pulse_out 单元测试报告

| 属性 | 值 |
|------|-----|
| 测试目标 | `test_pulse_out` |
| 源文件 | `tests/common/test_pulse_out.c` |
| 被测代码 | `common/pulse_out.c` |
| 关联代码 | `common/time_util.h`（`time_elapsed_ms`） |

> 用例数与通过情况以 `scripts/check_all.sh` 生成的 `build-check/test-results/report.html` 为准。本文只记录测试设计（环境、用例意图、边界），不记录动态结论——写死的数字必然滞后于测试本身。

## 测试环境

- 使用 mock `set_level` 回调记录调用次数和电平值
- 时间戳由测试代码传入固定 `uint64_t` 值，不依赖真实时钟
- 脉宽判断依赖 `time_elapsed_ms()` 无符号减法

## 用例明细

### 参数校验（3 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_start_null_slot_returns_param_err` | 空槽保护 | 返回 `SW_ERR_PARAM` | 通过 |
| `test_start_null_callback_returns_param_err` | 空回调保护 | 返回 `SW_ERR_PARAM` | 通过 |
| `test_start_zero_pulse_ms_returns_param_err` | 零脉宽拒绝 | 返回 `SW_ERR_PARAM` | 通过 |

### 启动与激活（1 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_start_sets_active_and_pulls_high` | 启动脉冲 | `active=true`；回调拉高 1 次；`pulse_ms=50` | 通过 |

### 计时与到期（2 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_tick_before_expiry_stays_active` | 未到期保持高 | 1099ms 时仍 active，仅 1 次拉高 | 通过 |
| `test_tick_at_expiry_pulls_low` | 到期自动拉低 | 1100ms 时 active=false，回调拉低 | 通过 |

### 取消（2 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_cancel_during_active_pulls_low` | 中途取消 | 立即拉低并清除 active | 通过 |
| `test_cancel_when_inactive_is_noop` | 空闲取消无副作用 | 回调未被调用 | 通过 |

### 空指针安全（2 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_is_active_null_returns_false` | 查询空槽 | 返回 false | 通过 |
| `test_tick_null_slot_is_safe` | tick 空槽 | 不崩溃 | 通过 |

### 时间工具（2 项）

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_elapsed_ms_basic` | 常规时间差 | 1100 - 1000 = 100 | 通过 |
| `test_elapsed_ms_wraparound` | uint64 回绕 | `UINT64_MAX-99` 到 `0` 差值为 100 | 通过 |

## 未覆盖项

| 场景 | 说明 |
|------|------|
| `set_level` 返回非 SW_OK | 当前实现不检查回调返回值 |
| 多槽并发 | 单槽测试，未验证多路脉冲并行 |
| `time_util_get_ms` 真实时钟 | 依赖系统时钟，未做确定性单测 |

## 待补充

> 新增用例时在此追加。若 `time_util.c` 测试增多，可拆分为独立报告 time_util.md 并更新索引。

| 用例 | 目的 | 状态 |
|------|------|------|
| — | — | — |
