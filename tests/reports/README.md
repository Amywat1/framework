# wash-device-framework 单元测试报告索引

> 最后更新：2026-07-12  
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
| — | `test_sim_encoder_counter` | 10 | 通过 | `adapters/outbound/hal/sim/` |
| — | `test_hal_voice_sim` | 5 | 通过 | `adapters/outbound/hal/sim/` |
| — | `test_machine_ops_port` | 3 | 通过 | `ports/outbound/machine/` |
| — | `test_op_mode_alarm_port` | 1 | 通过 | `ports/outbound/safety/` |
| — | `test_cloud_ports` | 4 | 通过 | `ports/inbound/cloud/`、`ports/outbound/cloud/` |
| — | `test_cloud_point_validate` | 9 | 通过 | `cloud/` |
| — | `test_cloud_point_dispatch` | 8 | 通过 | `cloud/` |
| — | `test_cloud_point_watcher` | 3 | 通过 | `cloud/` |
| — | `test_operational_mode` | 11 | 通过 | `domain/command_gateway/` |
| — | `test_side_effect_router` | 9 | 通过 | `application/side_effect_router.c` |
| — | `test_command_gateway` | 6 | 通过 | `application/`、`domain/command_gateway/` |
| [pulse_out.md](pulse_out.md) | `test_pulse_out` | 12 | 通过 | `common/pulse_out.c`、`common/time_util.h` |
| [io_handle.md](io_handle.md) | `test_io_handle` | 6 | 通过 | `common/io_handle.h` |
| — | `test_util_crc` | 5 | 通过 | `common/util_crc.c` |
| — | `test_util_fifo` | 6 | 通过 | `common/util_fifo.c` |
| — | `test_point_table` | 5 | 通过 | `common/point_table/` |
| [json_param_store.md](json_param_store.md) | `test_json_param_store` | 12 | 通过 | `adapters/outbound/storage/json/` |
| — | `test_json_deploy_store` | 9 | 通过 | `adapters/outbound/storage/json/` |
| — | `test_scheduler` | 9 | 通过 | `runtime/scheduler/` |
| — | `test_svc_param` | 9 | 通过 | `services/param/` |
| — | `test_safety_thread` | 2 | 通过 | `runtime/platform/` |

**合计：23 个测试目标，175 个用例，全部通过。**

## 本次 git 变更测试映射

| 变更模块 | 测试目标 | 覆盖要点 |
|----------|----------|----------|
| `cloud/cloud_point_*` | `test_cloud_point_validate` | 登记期校验、语义/access 约束 |
| `cloud/cloud_point_dispatch.c` | `test_cloud_point_dispatch` | JSON 上下行、DEVICE_CMD/MANUAL_ACT 分流、只读拒写 |
| `cloud/cloud_point_watcher.c` | `test_cloud_point_watcher` | ON_CHANGE shadow、`EVT_CLOUD_POINT_DIRTY` |
| `ports/**/cloud/`、`command_port` | `test_cloud_ports` | register/get 契约 |
| `domain/command_gateway/operational_mode.*` | `test_operational_mode` | 命令矩阵、模式转移、急停/报警/service 条件 |
| `application/side_effect_router.*` | `test_side_effect_router` | 各 effect 路由、RESET_FAULT 前置条件 |
| `application/command_gateway.*` | `test_command_gateway` | 跨线程 submit/drain、端到端 ACCEPTED/REJECTED |
| `common/point_table/*`（变更） | `test_point_table` | `point_apply_result`、只读拒写（已有） |

## 目录约定

- 每个模块一份独立报告，新增功能时在对应报告末尾的「待补充」章节追加条目。
- 新增测试目标时：创建 `tests/reports/<模块名>.md`，并更新本索引表。
- 报告字段统一为：用例名、测试目的、前置条件、验证点、结果。

## 未覆盖模块（待后续补充报告）

| 模块 | 说明 |
|------|------|
| `self_check_service` | 逻辑薄，经 `side_effect_router` 间接覆盖 START_SELF_CHECK 路径时可再补 |
| `bootstrap` / MQTT 适配器 | 尚未迁入 |
