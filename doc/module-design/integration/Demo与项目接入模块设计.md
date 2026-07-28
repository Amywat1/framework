# Demo 与项目接入模块设计

**版本**：v1.0  
**状态**：已落地（Demo smoke + wiring/project hooks 接入骨架）  
**最后同步代码**：2026-07-14（`demo/`、`runtime/bootstrap/wiring.h`、`runtime/bootstrap/project_hooks.h`、`machine_ops_port`）  
**适用范围**：`demo/`、`runtime/bootstrap/`、`ports/outbound/machine/`、项目 wiring/bindings  
**架构基线**：通用 bootstrap + 项目依赖注入 + 项目 hooks  
**关键词**：demo、wiring、project_hooks、machine_ops、bootstrap_run、smoke、project bring-up

---

## 1. 设计目标与核心理念

Demo 与项目接入层展示如何把通用框架装配成一个可启动设备实例。框架提供 `bootstrap_run()` 和固定生命周期，项目只通过 `wiring()`、`project_hooks`、端口注册和弱符号覆盖接入具体硬件、报警目录、物模型和后台任务。

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
| HAL | `hal_io_sim`、`hal_voice_sim`、`hw_estop_sim` |
| 存储 | `json_param_store`、`json_deploy_store` |
| machine ops | 空操作/成功返回的 `demo_machine_ops` |
| 报警目录 | 两个 demo alarm code |
| wash orchestrator | 使用测试 stub |
| 周期任务 | `alarm_bridge` 50ms drain |
| 验证链路 | STOP_OPERATION、硬件急停事件、报警触发链 |

---

## 2. 接入点总览

### 2.1 必备文件

一个产品项目通常需要提供以下实现：

| 文件/模块 | 职责 |
|-----------|------|
| `app/target/*.cpp` | 真机入口进程级准备与 `bootstrap_run()` 调用 |
| `target runtime glue` | 对外部 Snack runtime API 做项目内封装 |
| `wiring.c` | 注册 HAL、storage、cloud、engine loader 等 provider |
| `project_hooks.c` | 实现 `project_*` 生命周期钩子 |
| `machine_ops.c` | 注册 `machine_ops_t`，承接命令副作用 |
| `alarm_catalog.c` | 加载项目报警目录 |
| `safety_ports.c` | 覆盖急停输入和安全切断弱符号 |
| `hal_bindings.c` | 绑定电机、VFD、sensor、IO、fluid path 等实例 |
| `cloud_model.c` | 注册项目物模型 bundle |
| `engine_io_binding.c` | 注册真实 `engine_io_ops_t` / catalog |
| `config/*.json` | 参数、部署配置、洗车方案及 manifest |

### 2.2 启动阶段与接入点

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

bootstrap_init()
    ├─ project_init_hal()
    ├─ project_init_adapters()
    └─ project_register_runtime_tasks()

bootstrap_start()
    └─ project_start_runtime()
```

---

## 3. Demo 实现说明

### 3.1 `demo_main`

`demo/app/demo_main.c` 调用 `bootstrap_run()` 后执行三段 smoke 检查：

| 检查 | 验证内容 |
|------|----------|
| STOP_OPERATION | `device_command_port.submit()` → command gateway → operational mode |
| HW ESTOP | `hw_estop_sim_set_active(true)` → `EVT_HW_ESTOP_ON` → op mode estop flag |
| Alarm trigger | `alarm_binding.trigger()` → `alarm_event_bridge_drain()` → registry blocking |

Demo 直接订阅 `EVT_HW_ESTOP_ON`，用于确认 event dispatch 线程已工作。

### 3.2 `wiring_sim`

`demo/wiring/wiring_sim.c` 只注册最小 sim adapter：

```text
wiring()
    ├─ hal_io_sim_register()
    ├─ hal_voice_sim_register()
    ├─ json_param_store_register()
    └─ json_deploy_store_register()
```

它不注册真实 HAL、cloud provider、engine program loader 或完整物模型。JSON 文件路径不在 `wiring()` 中注入，而由 `project_configure_storage()` 调用 `json_param_store_configure()` / `json_deploy_store_configure()` 完成。

### 3.3 `project_hooks_sim`

| Hook | Demo 行为 |
|------|-----------|
| `project_configure_storage()` | 注入 Demo 参数/部署 JSON 路径 |
| `project_configure_hal()` | 空实现 |
| `project_bind_hal()` | 空实现 |
| `project_init_hal()` | 空实现 |
| `project_configure_safety()` | 空实现 |
| `project_configure_adapters()` | 空实现 |
| `project_bind_machine()` | `demo_machine_ops_register()` |
| `project_bind_alarm_catalog()` | `demo_alarm_catalog_load()` |
| `project_validate()` | 空实现 |
| `project_init_adapters()` | 空实现 |
| `project_register_runtime_tasks()` | 注册 `alarm_bridge` 50ms 周期任务 |
| `project_start_runtime()` | 空实现 |
| `project_assert_safe_outputs()` | 空实现 |

### 3.4 `demo_machine_ops`

Demo 注册 `machine_ops_t`，所有动作为空或返回 `SW_OK`。这只验证 `side_effect_router` 能调用到项目端口，不验证真实机构动作。

### 3.5 `demo_alarm_catalog`

Demo 加载两个报警：

| Code | 等级 | 说明 |
|------|------|------|
| `201101` | MAJOR | blocking |
| `201709` | CRITICAL | LOCKOUT（AUTO_STATIC，Recover 不会清除） |

---

## 4. Machine Ops 接入

`machine_ops_port` 是 framework application 到项目机构能力的出站端口。

```c
typedef struct {
    void (*deferred_stop_all)(void);
    void (*abort_home)(void);
    sw_err_t (*home_device)(void);
    sw_err_t (*execute_manual_actuator)(uint32_t act_id, int32_t param);
    sw_err_t (*stop_all_outputs)(void);
} machine_ops_t;
```

### 4.1 调用方

| machine op | 典型调用方 |
|------------|------------|
| `deferred_stop_all` | safety deferred stop / emergency completion path |
| `abort_home` | 启动中止归位清障（异步）；完成后须发 `EVT_ABORT_HOME_DONE` |
| `home_device` | `DEV_CMD_RECOVER` 在 STOPPED 下的内部归位副作用，以及故障恢复服务 |
| `execute_manual_actuator` | `DEV_CMD_MANUAL_ACTUATOR` 副作用 |
| `stop_all_outputs` | `DEV_CMD_STOP_ALL_OUTPUTS` 副作用 |

`side_effect_router` 不做权限判断，只执行 `operational_mode` 已裁决允许的副作用。未注册或函数缺失时返回 `SW_ERR_NOT_INIT`。

### 4.2 项目实现要求

- `project_bind_machine()` 中调用 `machine_ops_register()`。
- `execute_manual_actuator` 的 `act_id` 和 `param` 由项目定义，并在云端/CLI 命令映射中保持一致。
- `stop_all_outputs` 必须能落到安全输出态。
- `home_device` 和 `abort_home` 应处理执行中冲突和硬件故障，并返回明确错误码。

---

## 5. 真机项目接入 Checklist

### 5.1 Storage

- 在 `wiring()` 注册 `json_param_store_register()` 或替代后端。
- 在 `wiring()` 注册 `json_deploy_store_register()` 或替代后端。
- 在 `project_configure_storage()` 注入 `PARAM_STORE_JSON_FILE_PATH`、`DEPLOY_STORE_JSON_FILE_PATH`。
- 若使用洗车 engine，注册 `engine_program_json_register_loader()`。
- 部署方案 JSON 与对应 `*.manifest.json`。

### 5.2 HAL

- 选择 sim 或真机 provider。
- 在 `wiring()` 注册 `hal_io` provider、`hal_sensor_filter`、`hal_vfd_manager` / provider backend、`hal_voice` provider。
- 在 `project_configure_hal()` 下发 IO 名称表、串口、地址、点位等配置。
- 在 `project_bind_hal()` 绑定传感器通道、VFD 实例、backend 与事件回调。
- 在 `project_init_hal()` 执行传感器预热等依赖 HAL init 后的项目初始化。
- 创建并注入 `hal_motor_exec_t` 给设备控制模式。
- 覆盖 `hw_estop_port_is_active()` 和 `safety_cutout_execute()`。

### 5.3 Domain / Application

- 在 `project_bind_machine()` 注册 `machine_ops_t`。
- 在 `project_bind_alarm_catalog()` 加载项目报警目录。
- 初始化洗车 orchestrator 所需的 engine IO 后端和方案 loader。
- 根据需要初始化 telemetry projection 和 `dev_ctx`。
- 在 `project_validate()` 校验 cloud model，在 `project_register_runtime_tasks()` 注册 `report_scheduler`。

### 5.4 Cloud / Inbound

- 注册 `cloud_link_port`、`cloud_report_port`、`cloud_property_port` provider。
- 注册项目物模型 `cloud_model_bundle_t`。
- 将 `DEVICE_CMD` 点位映射到 `device_command_port`。
- 在 `project_configure_adapters()` 配置云端或 CLI 入站适配器。
- 在 `project_init_adapters()` 初始化云端或 CLI 入站适配器。

### 5.5 Runtime

- `app/target/` 只做进程级运行时准备，不直接初始化某个 HAL SDK。
- 在 `project_register_runtime_tasks()` 注册项目周期任务。
- 只有无法纳入 scheduler 的项目线程才放在 `project_start_runtime()`。
- 不直接修改 `bootstrap_run()` 顺序。
- 长耗时任务不要放在 event handler 中。
- fatal/panic 路径必须能调用 `project_assert_safe_outputs()` 落安全态。

---

## 6. 构建接入

Demo 的 `demo/CMakeLists.txt` 展示最小 smoke target：

| 分组 | 内容 |
|------|------|
| `_demo_app` | `demo/app/*.c` |
| `_demo_wiring` | wiring、hooks、machine ops、alarm catalog |
| `_fw_runtime_src` | bootstrap、event bus、scheduler、safety thread |
| `_fw_application_src` | gateway、bridge、mode、router 等 |
| `_fw_domain_src` | operational mode、alarm registry、safety posture |
| `_fw_ports_src` | port registry、safety/machine ports |
| `_fw_services_src` | `svc_param` |
| `_fw_adapters_src` | sim HAL、storage JSON |

产品构建不必照搬 demo 的源列表，但必须满足链接闭包：项目提供的 wiring/hooks 与所需框架模块、provider、third_party 一起进入目标。

可选真机 provider 通过根 CMake 开关启用：

| 开关 | 用途 |
|------|------|
| `WDF_ENABLE_MCC_PROVIDER` | MCC 电机执行器 |
| `WDF_ENABLE_SNACK_IO_EXP_PROVIDER` | Snack io_exp IO 子板 |
| `WDF_ENABLE_SNACK_MODBUS_PROVIDER` | Snack voice / VFD Modbus |
| `WDF_ENABLE_SNACK_CLOUD_PROVIDER` | Snack MQTT cloud provider |

---

## 7. 验证策略

### 7.1 Demo Smoke

构建 `wdf_smoke` 后运行，期望输出 `[Demo] All checks passed.`。它验证：

- bootstrap 能完成。
- command gateway 能处理命令。
- safety thread 能检测急停 sim 边沿。
- alarm binding / bridge / registry 能联通。

### 7.2 项目 Bring-up

建议按以下顺序逐步验证：

1. 只启 storage + sim HAL，确认 bootstrap。
2. 接入 machine ops 空实现，确认 command gateway。
3. 接入报警目录和 detector，确认 alarm bridge。
4. 接入真实 IO/sensor，确认安全输入。
5. 接入 VFD/motor/voice，确认 HAL 诊断。
6. 接入 engine IO 和方案资产，确认洗车 worker。
7. 接入 cloud model/provider，确认上下行与 report scheduler。

---

## 8. 常见错误

| 问题 | 结果 |
|------|------|
| 在 `wiring()` 中只注册 provider，不在 configure/bind/init hook 中注入参数和绑定实例 | port 已存在但运行期返回 `SW_ERR_NOT_INIT` |
| 忘记注册 `machine_ops` | HOME/MANUAL/STOP_ALL_OUTPUTS 命令副作用失败 |
| 报警目录晚于 detector 启动 | detector 触发未知报警码 |
| `project_start_runtime()` 后再注册周期任务 | 任务不会被当前 `scheduler_start_all()` 启动 |
| event handler 中执行阻塞 IO | 阻塞全局 event dispatch |
| 跳过 `project_assert_safe_outputs()` 实现 | event bus fatal / IO panic 时无法兜底切断 |
| Demo stub 被误用于产品 | 命令看似成功但没有真实机构动作 |

---

## 9. 相关文档

- `doc/module-design/runtime/Runtime模块设计.md` — bootstrap 和 project hooks 顺序
- `doc/module-design/ports-adapters/HAL端口与适配器模块设计.md` — HAL provider 与实例绑定
- `doc/module-design/ports-adapters/Storage端口与方案资产模块设计.md` — 参数、部署配置和方案资产
- `doc/module-design/domain/命令网关模块设计.md` — command gateway 与 side effect router
- `doc/module-design/domain/报警系统模块设计.md` — alarm catalog、binding、bridge
- `doc/module-design/ports-adapters/CloudModel模块设计.md` — cloud model/provider 接入
