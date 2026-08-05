# 模块设计基线

本目录是 `wash-device-framework` 当前代码实现的设计基线。文档面向开发维护，允许包含代码路径、接口契约、数据模型、启动顺序、错误边界和测试覆盖。

## 模块地图

| 代码范围 | 基线文档 |
|----------|----------|
| `common/`、整体分层 | `00-模块设计总览.md` |
| `runtime/bootstrap/`、`runtime/scheduler/`、`runtime/config/thread_config.h` | `runtime/Runtime模块设计.md` |
| `runtime/event_bus/`、`common/event_types.h` | `runtime/EventBus模块设计.md` |
| `domain/program_engine/`、`ports/outbound/storage/engine_program_loader_port.*`、`application/engine_session/`、`engine_io_sim` | `domain/方案引擎模块设计.md` |
| `domain/op_mode/`、`application/command_gateway.*`、`application/side_effect_router.*`、`application/bridges/op_mode_bridge.*` | `domain/命令网关模块设计.md` |
| `domain/safety/`、`application/bridges/alarm_*`、`ports/inbound/safety/`、`ports/outbound/safety/` | `domain/报警系统模块设计.md` |
| `domain/device_control/`、`ports/outbound/hal/motor/hal_motor_exec_port.h` | `domain/设备控制模式模块设计.md` |
| `domain/telemetry/`、`application/telemetry_projection.*` | `domain/状态投影与设备快照模块设计.md` |
| `ports/outbound/hal/`、`adapters/outbound/hal/` | `ports-adapters/HAL端口与适配器模块设计.md` |
| `ports/outbound/storage/`、`adapters/outbound/storage/json/` | `ports-adapters/Storage端口与方案资产模块设计.md` |
| `domain/cloud/`、`ports/**/cloud/`、`adapters/**/cloud/`、`application/orchestrators/report_scheduler.*` | `ports-adapters/CloudModel模块设计.md` |
| `demo/`、`runtime/bootstrap/project_hooks.*`、`ports/outbound/machine/`、项目 wiring | `integration/Demo与项目接入模块设计.md` |

## 尚未收录的代码

以下代码目前没有对应模块设计文档。它们的设计意图记录在 `architecture/` 中，需要代码级基线时应在本目录补文档，而不是把说明散落到架构篇：

| 代码范围 | 现有说明位置 |
|----------|--------------|
| `observability/core/`、`observability/recorder/`、`application/bridges/observation_event_bridge.*` | `architecture/00-通用框架架构说明.md` §3、`architecture/01-分层架构与依赖边界.md` §2 |
| `ports/port_contract.*`、`application/asset_contract.*` | `architecture/08-框架优化方向.md` |
| `common/trace_context.*`、`common/asset_version.*` | `architecture/08-框架优化方向.md` |
| `adapters/outbound/hal/components/adc_gate/`、`adapters/outbound/hal/sim/engine_actuator_sim.*` | 无 |

## 单篇文档建议结构

模块设计文档优先按以下结构维护：

1. 模块定位
2. 适用代码范围
3. 分层归属与依赖方向
4. 文件清单
5. 数据模型
6. 核心行为契约
7. 启动、注册或调用顺序
8. 错误处理与边界
9. 测试覆盖
10. 扩展指南
11. 当前落地状态

## 与架构说明的关系

`architecture/` 说明为什么这样划分模块，以及项目接入时应遵守哪些边界；本目录说明当前代码具体如何实现这些设计。若两者出现不一致，以当前代码为准修正模块设计，再评估是否需要调整架构说明。
