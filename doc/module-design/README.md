# 模块设计基线

本目录是 `wash-device-framework` 当前代码实现的设计基线。文档面向开发维护，允许包含代码路径、接口契约、数据模型、启动顺序、错误边界和测试覆盖。

## 模块地图

| 代码范围 | 基线文档 |
|----------|----------|
| `common/`、整体分层 | `00-模块设计总览.md` |
| `runtime/bootstrap/`、`runtime/scheduler/`、`runtime/config/thread_config.h` | `runtime/Runtime模块设计.md` |
| `runtime/event_bus/`、`common/event_types.h` | `runtime/EventBus模块设计.md` |
| `domain/program_engine/`、`domain/ports/outbound/storage/engine_program_loader_port.*`、`application/engine_session/`、`engine_io_sim` | `domain/方案引擎模块设计.md` |
| `domain/op_mode/`、`application/command_gateway.*`、`application/side_effect_router.*`、`application/bridges/op_mode_bridge.*` | `domain/命令网关模块设计.md` |
| `domain/safety/`、`application/bridges/alarm_*`、`application/ports/inbound/safety/`、`domain/ports/outbound/safety/` | `domain/报警系统模块设计.md` |
| `domain/mechanism/`、`domain/ports/outbound/motor/` | `domain/机构控制模式模块设计.md` |
| `domain/telemetry/`、`application/telemetry_projection.*` | `domain/状态投影与设备快照模块设计.md` |
| `domain/ports/outbound/hal/`、`adapters/outbound/hal/` | `ports-adapters/HAL端口与适配器模块设计.md` |
| `domain/ports/outbound/storage/`、`adapters/outbound/storage/json/` | `ports-adapters/Storage端口与方案资产模块设计.md` |
| `domain/cloud/`、`application/ports/outbound/cloud/`、`adapters/outbound/cloud/`、`application/orchestrators/report_scheduler.*` | `ports-adapters/CloudModel模块设计.md` |
| `demo/`、`runtime/bootstrap/project_hooks.*`、`domain/ports/outbound/device/`、项目 wiring | `integration/Demo与项目接入模块设计.md` |
| `observability/core/`、`observability/recorder/`、`application/bridges/observation_event_bridge.*` | `observability/可观测性模块设计.md` |
| `runtime/ports/port_contract.*`、`application/asset_contract.*` | `runtime/启动契约校验模块设计.md` |
| `common/trace_context.*`、`common/asset_version.*` | `common/横切基础设施模块设计.md` |

## 尚未收录的代码

当前无待补的框架模块设计缺口。下列项已在既有专篇覆盖，不单独立篇：

| 代码范围 | 说明位置 |
|----------|----------|
| `adapters/outbound/hal/components/adc_gate/` | `ports-adapters/HAL端口与适配器模块设计.md` §4.3 |
| `adapters/outbound/hal/sim/engine_actuator_sim.*` | `ports-adapters/HAL端口与适配器模块设计.md` §5.1 |

vendor/provider、入站 safety adapter、点表 JSON 序列化等由 HAL / Cloud / Demo 专篇或项目侧文档覆盖，不在本目录为可选 provider 单独立篇。

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

**必备四节**：文件清单、核心行为契约、测试覆盖、当前落地状态。前三节回答"代码在哪、
承诺什么、谁在验"，缺任一则文档无法作为基线使用；落地状态回答"哪些还没做"，缺了
就会被读成"全都做完了"。

其余各节按模块实际情况取舍：纯端口层没有数据模型，无状态模块没有启动顺序，
硬凑章节只会产生空话。「当前落地状态」与「扩展指南」合并为一节
（如"当前边界与扩展指南"）也可接受——两者本就常常连着写。

**测试覆盖只列测试名与覆盖点，不写用例数和通过数。**动态结论由
`scripts/check_all.sh` 每次生成的报告承担；写进文档必然失同步，本仓
`tests/reports/` 已因此出过事故（手写覆盖表声称 event_bus 8 例、实际 13 例）。
文档里的测试名由 `scripts/check_doc_refs.sh` 的 D6 校验存在性。

## 与架构说明的关系

`architecture/` 说明为什么这样划分模块，以及项目接入时应遵守哪些边界；本目录说明当前代码具体如何实现这些设计。若两者出现不一致，以当前代码为准修正模块设计，再评估是否需要调整架构说明。
