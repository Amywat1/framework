# wash-device-framework 单元测试报告索引

> 最后更新：2026-07-11  
> 框架版本：v0.1.0  
> 测试框架：Unity 2.6 + CTest

## 运行方式

```bash
cmake -B build -DWDF_BUILD_TESTS=ON
cmake --build build -j4
ctest --test-dir build -V
```

## 汇总

| 报告 | 测试目标 | 用例数 | 结果 | 被测模块 |
|------|----------|--------|------|----------|
| [event_bus.md](event_bus.md) | `test_event_bus` | 8 | 通过 | `runtime/event_bus/` |
| [hal_io_sim.md](hal_io_sim.md) | `test_hal_io_sim` | 23 | 通过 | `adapters/outbound/hal/sim/` |
| [pulse_out.md](pulse_out.md) | `test_pulse_out` | 12 | 通过 | `common/pulse_out.c`、`common/time_util.h` |
| [io_handle.md](io_handle.md) | `test_io_handle` | 6 | 通过 | `common/io_handle.h` |
| [json_param_store.md](json_param_store.md) | `test_json_param_store` | 12 | 通过 | `adapters/outbound/storage/json/` |
| — | `test_scheduler` | 9 | 通过 | `runtime/scheduler/` |
| — | `test_svc_param` | 9 | 通过 | `services/param/` |
| — | `test_safety_thread` | 2 | 通过 | `runtime/platform/` |

**合计：8 个测试目标，81 个用例，全部通过。**

## 目录约定

- 每个模块一份独立报告，新增功能时在对应报告末尾的「待补充」章节追加条目。
- 新增测试目标时：创建 `tests/reports/<模块名>.md`，并更新本索引表。
- 报告字段统一为：用例名、测试目的、前置条件、验证点、结果。

## 未覆盖模块（待后续补充报告）

| 模块 | 说明 |
|------|------|
| `json_deploy_store` | 需临时文件，待补充 |
| `port_registry` | 逻辑极薄，优先级低 |
| `domain` / `bootstrap` | 尚未迁入 |
