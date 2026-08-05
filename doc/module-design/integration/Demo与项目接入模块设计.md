# Demo 与项目接入模块设计

**版本**：v1.0  
**状态**：已落地（Demo smoke + wiring/project hooks 接入骨架）  
**最后同步代码**：2026-07-14（`demo/`、`runtime/bootstrap/wiring.h`、`runtime/bootstrap/project_hooks.h`、`machine_ops_port`）  
**适用范围**：`demo/`、`runtime/bootstrap/`、`ports/outbound/machine/`、项目 wiring/bindings  
**架构基线**：通用 bootstrap + 项目依赖注入 + 项目 hooks  
**关键词**：demo、wiring、project_hooks、machine_ops、bootstrap_run、smoke、project bring-up

---

## 1. 设计目标与核心理念

Demo 与项目接入层展示如何把通用框架装配成一个可启动设备实例。框架提供 `bootstrap_run()` 和固定生命周期，项目只通过 `wiring()`、`project_hooks` 和端口注册接入具体硬件、报警目录、物模型和后台任务。

### 1.1 设计目标

- **给新项目一个入口**：明确需要实现哪些文件、注册哪些 port、在哪个 hook 做什么。
- **保持框架通用性**：项目逻辑不写进 `runtime/bootstrap/bootstrap.c`。
- **区分 Demo 与产品**：Demo 是 smoke 集成，不是真机项目模板的完整实现。
- **端口优先**：业务能力通过 machine/cloud/HAL/storage/safety ports 注入。
- **启动顺序可预测**：所有项目接入点服从 `bootstrap_run()` 阶段顺序。

### 1.2 Demo 当前定位

当前 `demo/` 不是完整洗车机产品示例，而是最小集成验证：

| 能力 | Demo 实现 |
|------|-----------|
| 启动入口 | `demo/app/demo_main.c` |
| HAL | `hal_io_sim`、`hal_voice_sim` |
| 安全端口 | `safety_sim` 注册 `safety_ops_t`，急停状态由 `hw_estop_sim` 维护 |
| 存储 | `json_param_store`、`json_deploy_store` |
| machine ops | 空操作/成功返回的 `demo_machine_ops` |
| 报警目录 | 两个 demo alarm code |
| 洗车编排 | 无（`machine_ops` 空实现返回成功） |
| 周期任务 | 无项目任务；`alarm_bridge` 由框架 `alarm_event_bridge_init()` 自行登记 |
| 验证链路 | STOP_OPERATION、硬件急停事件、报警触发链 |

---

## 2. 框架侧文件清单

Demo 是框架自带的最小接入实例，可直接作为新项目 wiring 的对照物。

| 文件 | 职责 |
|------|------|
| `runtime/bootstrap/bootstrap.h` | `bootstrap_run()` 与启动失败后的进程约束 |
| `runtime/bootstrap/project_hooks.h` | `project_hooks_t` 15 个必填钩子 |
| `runtime/bootstrap/wiring.h` | `wiring()` 声明 |
| `ports/outbound/machine/machine_ops_port.h` | 命令副作用对应的项目动作契约 |
| `ports/port_contract.h` | 必需端口的启动期集中校验（`PORT_REQ_*`） |
| `application/asset_contract.h` | 必需资产的启动期集中校验（`ASSET_REQ_*`） |
| `demo/app/demo_main.c` | 最小 main：调 `bootstrap_run()` 后驱动 smoke 场景 |
| `demo/wiring/wiring_sim.c` | 最小 provider 注册（sim HAL、storage、safety） |
| `demo/wiring/project_hooks_sim.c` | 15 个钩子的最小实现 |
| `demo/wiring/demo_machine_ops.c` | 最小 `machine_ops_t` |
| `demo/wiring/demo_alarm_catalog.c` | 最小报警目录 |
| `demo/config/demo_config.h` | Demo 资产路径与常量 |

---

## 3. 接入点总览

### 3.1 必备文件

一个产品项目通常需要提供以下实现：

| 文件/模块 | 职责 |
|-----------|------|
| 项目进程入口 | 进程级准备与 `bootstrap_run()` 调用（目录布局由项目自定） |
| 运行时 glue | 对外部平台 SDK 做项目内封装 |
| `wiring.c` | 注册 HAL、storage、cloud、engine loader 等 provider |
| `project_hooks.c` | 实现 `project_*` 生命周期钩子 |
| `machine_ops.c` | 注册 `machine_ops_t`，承接命令副作用 |
| `alarm_catalog.c` | 加载项目报警目录 |
| `safety_ports.c` | 经 `safety_port_register()` 注册急停输入、安全切断与急停码判定 |
| `hal_bindings.c` | 绑定电机、VFD、sensor、IO、fluid path 等实例 |
| `cloud_model.c` | 注册项目物模型 bundle |
| `engine_io_binding.c` | 注册真实 `engine_io_ops_t` / catalog |
| `config/*.json` | 参数、部署配置、洗车方案及 manifest |

### 3.2 启动阶段与接入点

```text
target entry
    ├─ 配置 Snack 进程级参数
    └─ 调用 bootstrap_run()

bootstrap_register()
    └─ wiring()
         ├─ 注册 storage adapter
         ├─ 注册 HAL provider
         ├─ 注册 cloud provider
         └─ 注册 engine program loader

bootstrap_configure_storage()
    └─ project_configure_storage()

bootstrap_load_storage()
    ├─ svc_param_init()
    └─ deploy_store.load()

bootstrap_configure()
    ├─ project_configure_hal()
    ├─ project_configure_safety()
    └─ project_configure_adapters()

bootstrap_bind()
    ├─ project_bind_hal()
    ├─ project_bind_machine()
    └─ project_bind_alarm_catalog()

bootstrap_validate()
    └─ project_validate()

bootstrap_init_hal()
    └─ project_init_hal()

bootstrap_init_machine()
    └─ project_init_machine()

bootstrap_init_safety()
    └─ project_init_safety()

bootstrap_init_services()
    ├─ project_init_adapters()
    └─ project_register_runtime_tasks()

bootstrap_start()
    └─ project_start_runtime()
```

完整阶段序列（含框架自身的 init 调用）见 `doc/module-design/runtime/Runtime模块设计.md` §2。

---

## 4. Demo 实现说明

### 4.1 `demo_main`

`demo/app/demo_main.c` 调用 `bootstrap_run()` 后执行三段 smoke 检查：

| 检查 | 验证内容 |
|------|----------|
| STOP_OPERATION | `device_command_port.submit()` → command gateway → operational mode |
| HW ESTOP | `hw_estop_sim_set_active(true)` → `EVT_HW_ESTOP_ON` → op mode estop flag |
| Alarm trigger | `alarm_binding.trigger()` → `alarm_event_bridge_drain()` → registry blocking |

Demo 直接订阅 `EVT_HW_ESTOP_ON`，用于确认 event dispatch 线程已工作。该事件需要有采集方发布，见 §9.1。

### 4.2 `wiring_sim`

`demo/wiring/wiring_sim.c` 只注册最小 sim adapter：

```text
wiring()
    ├─ hal_io_sim_register()
    ├─ hal_voice_sim_register()
    ├─ json_param_store_register()
    └─ json_deploy_store_register()
```

它不注册真实 HAL、cloud provider、engine program loader 或完整物模型。JSON 文件路径不在 `wiring()` 中注入，而由 `project_configure_storage()` 调用 `json_param_store_configure()` / `json_deploy_store_configure()` 完成。

### 4.3 `project_hooks_sim`

| Hook | Demo 行为 |
|------|-----------|
| `project_configure_storage()` | 注入 Demo 参数/部署 JSON 路径 |
| `project_configure_hal()` | 空实现 |
| `project_bind_hal()` | 空实现 |
| `project_init_hal()` | 空实现 |
| `project_configure_safety()` | 空实现 |
| `project_init_safety()` | 空实现 |
| `project_configure_adapters()` | 空实现 |
| `project_bind_machine()` | `demo_machine_ops_register()` |
| `project_init_machine()` | 空实现 |
| `project_bind_alarm_catalog()` | `demo_alarm_catalog_load()` |
| `project_validate()` | `port_contract_validate()` 校验必需端口 |
| `project_init_adapters()` | 空实现（不接入急停轮询与观测桥） |
| `project_register_runtime_tasks()` | 空实现 |
| `project_start_runtime()` | 空实现 |
| `assert_safe_outputs`（hook 字段） | 空实现 |

### 4.4 `demo_machine_ops`

Demo 注册 `machine_ops_t`，所有动作为空或返回 `SW_OK`。这只验证 `side_effect_router` 能调用到项目端口，不验证真实机构动作。

### 4.5 `demo_alarm_catalog`

Demo 加载两个报警：

| Code | 等级 | 说明 |
|------|------|------|
| `201101` | MAJOR | blocking |
| `201709` | CRITICAL | LOCKOUT（AUTO_STATIC，Recover 不会清除） |

---

## 5. Machine Ops 接入

`machine_ops_port` 是 framework application 到项目机构能力的出站端口。

```c
typedef struct {
    void (*deferred_stop_all)(void);
    void (*abort_home)(void);
    sw_err_t (*start_wash)(wash_mode_t mode);
    void (*abort_wash)(wash_abort_cause_t cause);
    sw_err_t (*home_device)(void);
    sw_err_t (*execute_manual_actuator)(uint32_t act_id, int32_t param);
    sw_err_t (*stop_all_outputs)(void);
    bool (*is_wash_entry_ready)(void);
} machine_ops_t;
```

### 5.1 调用方

| machine op | 典型调用方 |
|------------|------------|
| `deferred_stop_all` | 安全延后停机路径 |
| `abort_home` | 启动中止归位清障（异步）；完成后须发 `EVT_ABORT_HOME_DONE` |
| `start_wash` | `DEV_CMD_START_WASH` 副作用：项目选方案并启动会话 |
| `abort_wash` | `DEV_CMD_STOP_WASH` 副作用，以及急停/LOCKOUT 切断路径 |
| `home_device` | `DEV_CMD_RECOVER` 在 STOPPED 下的内部归位副作用，以及故障恢复服务 |
| `execute_manual_actuator` | `DEV_CMD_MANUAL_ACTUATOR` 副作用 |
| `stop_all_outputs` | `DEV_CMD_STOP_ALL_OUTPUTS` 副作用 |
| `is_wash_entry_ready` | `START_WASH` 附加门禁；未注册（NULL）时框架不拦截 |

`side_effect_router` 不做权限判断，只执行 `operational_mode` 已裁决允许的副作用。未注册或函数缺失时返回 `SW_ERR_NOT_INIT`。

### 5.2 项目实现要求

- `project_bind_machine()` 中调用 `machine_ops_register()`。
- `execute_manual_actuator` 的 `act_id` 和 `param` 由项目定义，并在云端/CLI 命令映射中保持一致。
- `stop_all_outputs` 必须能落到安全输出态。
- `home_device` 和 `abort_home` 应处理执行中冲突和硬件故障，并返回明确错误码。

---

## 6. 真机项目接入 Checklist

### 6.1 Storage

- 在 `wiring()` 注册 `json_param_store_register()` 或替代后端。
- 在 `wiring()` 注册 `json_deploy_store_register()` 或替代后端。
- 在 `project_configure_storage()` 注入 `PARAM_STORE_JSON_FILE_PATH`、`DEPLOY_STORE_JSON_FILE_PATH`。
- 若使用洗车 engine，注册 `engine_program_json_register_loader()`。
- 部署方案 JSON 与对应 `*.manifest.json`。

### 6.2 HAL

- 选择 sim 或真机 provider。
- 在 `wiring()` 注册 `hal_io` provider、`hal_sensor_filter`、`hal_vfd_manager` / provider backend、`hal_voice` provider。
- 在 `project_configure_hal()` 下发 IO 名称表、串口、地址、点位等配置。
- 在 `project_bind_hal()` 绑定传感器通道、VFD 实例、backend 与事件回调。
- 在 `project_init_hal()` 执行传感器预热等依赖 HAL init 后的项目初始化。
- 创建并注入 `hal_motor_exec_t` 给设备控制模式。
- 经 `safety_port_register()` 注册 `safety_ops_t`，提供急停输入与安全切断实现。

### 6.3 Domain / Application

- 在 `project_bind_machine()` 注册 `machine_ops_t`。
- 在 `project_bind_alarm_catalog()` 加载项目报警目录。
- 初始化洗车 orchestrator 所需的 engine IO 后端和方案 loader。
- 遥测投影由 `bootstrap_init_services()` 自动接入，项目无需初始化；读侧用 `device_snapshot_get()`。
- 在 `project_validate()` 校验 cloud model，在 `project_register_runtime_tasks()` 注册 `report_scheduler`。

### 6.4 Cloud / Inbound

- 注册 `cloud_link_port`、`cloud_report_port`、`cloud_property_port` provider。
- 注册项目物模型 `cloud_model_bundle_t`。
- 将 `DEVICE_CMD` 点位映射到 `device_command_port`。
- 在 `project_configure_adapters()` 配置云端或 CLI 入站适配器。
- 在 `project_init_adapters()` 初始化云端或 CLI 入站适配器。

### 6.5 Runtime

- 项目进程入口只做进程级运行时准备，不直接初始化某个 HAL SDK。
- 在 `project_register_runtime_tasks()` 注册项目周期任务。
- 只有无法纳入 scheduler 的项目线程才放在 `project_start_runtime()`。
- 不直接修改 `bootstrap_run()` 顺序。
- 长耗时任务不要放在 event handler 中。
- fatal/panic 路径必须能调用 `project_hooks_t.assert_safe_outputs` 落安全态。

---

## 7. 构建接入

框架源清单由 `cmake/wdf_targets.cmake` 以 INTERFACE 库形式导出，项目不再手抄路径：

```cmake
include(${FW_ROOT}/cmake/wdf_targets.cmake)
target_link_libraries(my_app PRIVATE wdf_application wdf_services wdf_storage_json wdf_hal_sim pthread)
```

选 INTERFACE 而非 STATIC，是因为同一份框架源在不同目标下需要不同编译定义（例如测试目标为 `fluid_path.c` 定义 `FLUID_PATH_UNIT_TEST`）。收益不是少编译一次，而是把源清单维护权收回框架内部：框架增删文件时项目只需重新配置。

可用分层目标（依赖逐层向下传递，link 上层自动带入下层）：

| 目标 | 内容 |
|------|------|
| `wdf_common` | 错误码、日志、时间、追踪上下文、点表模型、基础工具 |
| `wdf_ports` | 端口注册表与端口内自带实现 |
| `wdf_runtime` | bootstrap、事件总线、调度器 |
| `wdf_domain` | 运行模式、报警注册表、设备快照 |
| `wdf_application` | 命令网关、副作用路由、自检、恢复、遥测投影、报警与模式桥接 |
| `wdf_device_control` | 单轴运动、流体路径（需项目提供 motor provider） |
| `wdf_program_engine` | 方案引擎模型、表达式、tick 运行时 |
| `wdf_engine_session` | 方案会话 worker |
| `wdf_cloud` / `wdf_cloud_json` | 云点位模型 / 属性 JSON 与 property_port 安装 |
| `wdf_report_scheduler` | 云端上报调度 |
| `wdf_asset_contract` | 必需资产启动期校验 |
| `wdf_observability` / `wdf_observation_bridge` | 观测记录与黑匣子 / 框架事件转观测记录 |
| `wdf_services` | `svc_param` |
| `wdf_storage_json` / `wdf_storage_program_json` | 参数与部署存储 / 方案资产加载 |
| `wdf_hal_sim` / `wdf_hal_engine_sim` / `wdf_hal_components` | 仿真后端 / 引擎仿真后端 / HAL 组合件 |
| `wdf_point_table_json` / `wdf_cjson` | 点位表 JSON 编解码 / 随框架分发的 cJSON |

`wdf_domain` 刻意不含设备控制与方案引擎：两者分别要求项目提供 motor provider 与方案资产，最小接入（如框架自带 demo）并不需要，捆绑进核心会造成链接期缺符号。同理 `wdf_asset_contract`、`wdf_report_scheduler`、`wdf_observation_bridge` 各自独立，不接入的项目不必被迫链接 cloud、program_engine 或 observability。

Demo 的 `demo/CMakeLists.txt` 即按此方式装配，只额外补 `safety_sim` 与 `hw_estop_sim` 两个 sim 装配选择。

vendor provider 仍由根 CMake 开关以 STATIC 库提供，它们有外部 SDK 依赖，不适合无条件导出：

| 开关 | 用途 |
|------|------|
| `WDF_ENABLE_MCC_PROVIDER` | MCC 电机执行器 |
| `WDF_ENABLE_SNACK_IO_EXP_PROVIDER` | Snack io_exp IO 子板 |
| `WDF_ENABLE_SNACK_MODBUS_PROVIDER` | Snack voice / VFD Modbus |
| `WDF_ENABLE_SNACK_CLOUD_PROVIDER` | Snack MQTT cloud provider |

---

## 8. 测试覆盖

| 测试 | 覆盖点 |
|------|--------|
| `test_bootstrap_hooks` | 15 个钩子的 NULL 校验、启动阶段顺序、失败即返回 |
| `test_port_contract` | `PORT_REQ_*` 位掩码校验、缺失端口一次报全 |
| `test_asset_contract` | `ASSET_REQ_*` 校验：报警目录为空、点位表缺失、IO 目录空 |
| `test_machine_ops_port` | `machine_ops_t` 注册与命令副作用转发 |

`test_port_contract` 与 `test_asset_contract` 是接入契约的核心防线：两者都注入过
「探测函数恒返回 true」验证检查确有约束力。

用例数与通过情况以 `scripts/check_all.sh` 生成的 `build-check/test-results/report.html`
为准，本文不记录动态结论（原则见 `tests/reports/README.md`）。`wdf_smoke` 不在
`ctest` 内，其现状见第 9.1 节。

---

## 9. 验证策略

### 9.1 Demo Smoke

`wdf_smoke` 目标依次检查 bootstrap 完成、命令网关处理命令、急停边沿事件、报警链路联通，全部通过时输出 `[Demo] All checks passed.`。

**当前状态：该目标运行失败，退出码 1**（`ctest` 不包含它，因此门禁不覆盖）。两处与框架现状不一致，都在 demo 侧：

| 检查 | 失败原因 |
|------|----------|
| STOP_OPERATION | bootstrap 后模式为 `OP_MODE_STOPPED`，而命令矩阵中 `STOP_OPERATION` 在 STOPPED 下为 DENIED（仅 IDLE / WASH_DONE 允许），`submit()` 返回 `SW_ERR_STATE` |
| HW ESTOP | demo 未接入急停轮询适配器，且 `hw_estop_sim` 只维护状态、不发布事件，因此无人发出 `EVT_HW_ESTOP_ON` |

两者都是 demo 用例与当前框架语义脱节，不是框架缺陷：前者需改用 STOPPED 下允许的命令（或先 RECOVER 进 IDLE 再停运），后者需在 `project_init_adapters()` 调 `estop_poll_thread_init()`。修复 demo 属独立改动，本文只记录现状，不假称通过。

### 9.2 项目 Bring-up

建议按以下顺序逐步验证：

1. 只启 storage + sim HAL，确认 bootstrap。
2. 接入 machine ops 空实现，确认 command gateway。
3. 接入报警目录和 detector，确认 alarm bridge。
4. 接入真实 IO/sensor，确认安全输入。
5. 接入 VFD/motor/voice，确认 HAL 诊断。
6. 接入 engine IO 和方案资产，确认洗车 worker。
7. 接入 cloud model/provider，确认上下行与 report scheduler。

---

## 10. 常见错误

| 问题 | 结果 |
|------|------|
| 在 `wiring()` 中只注册 provider，不在 configure/bind/init hook 中注入参数和绑定实例 | port 已存在但运行期返回 `SW_ERR_NOT_INIT` |
| 忘记注册 `machine_ops` | HOME/MANUAL/STOP_ALL_OUTPUTS 命令副作用失败 |
| 报警目录晚于 detector 启动 | detector 触发未知报警码 |
| `project_start_runtime()` 后再注册周期任务 | 任务不会被当前 `scheduler_start_all()` 启动 |
| event handler 中执行阻塞 IO | 阻塞全局 event dispatch |
| 跳过 `project_hooks_t.assert_safe_outputs` 实现 | event bus fatal / IO panic 时无法兜底切断 |
| Demo stub 被误用于产品 | 命令看似成功但没有真实机构动作 |

---

## 11. 当前落地状态

| 能力 | 状态 |
|------|------|
| `bootstrap_run()` 11 阶段编排 | ✅ |
| `project_hooks_t` 15 钩子契约与 NULL 校验 | ✅ |
| 端口契约校验（`PORT_REQ_*`，14 位） | ✅ |
| 资产契约校验（`ASSET_REQ_*`，3 位） | ✅ |
| Demo 最小 wiring（sim HAL / storage / safety） | ✅ |
| `wdf_smoke` 端到端场景 | ⚠️ 退出码 1，两处 demo 侧用例与框架语义脱节，见第 9.1 节 |
| 真机项目 wiring 参考实现 | 项目侧职责，框架只提供 Demo 作为对照 |

---

## 12. 相关文档

- `doc/module-design/runtime/Runtime模块设计.md` — bootstrap 和 project hooks 顺序
- `doc/module-design/ports-adapters/HAL端口与适配器模块设计.md` — HAL provider 与实例绑定
- `doc/module-design/ports-adapters/Storage端口与方案资产模块设计.md` — 参数、部署配置和方案资产
- `doc/module-design/domain/命令网关模块设计.md` — command gateway 与 side effect router
- `doc/module-design/domain/报警系统模块设计.md` — alarm catalog、binding、bridge
- `doc/module-design/ports-adapters/CloudModel模块设计.md` — cloud model/provider 接入
