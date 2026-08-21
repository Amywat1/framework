# Demo 与项目接入模块设计

**版本**：v1.0  
**状态**：已落地（Demo smoke + wiring/project hooks 接入骨架）  
**最后同步代码**：2026-07-14（`demo/`、`runtime/bootstrap/wiring.h`、`runtime/bootstrap/project_hooks.h`、`device_ops_port`）  
**适用范围**：`demo/`、`runtime/bootstrap/`、`domain/ports/outbound/device/`、项目 wiring/bindings  
**架构基线**：通用 bootstrap + 项目依赖注入 + 项目 hooks  
**关键词**：demo、wiring、project_hooks、device_ops、bootstrap_run、smoke、project bring-up

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
| device ops | 空操作/成功返回的 `demo_device_ops` |
| 报警目录 | 两个 demo alarm code |
| 洗车编排 | 无（`device_ops` 空实现返回成功） |
| 周期任务 | 无项目任务；`alarm_bridge` 由框架 `alarm_bridge_init()` 自行登记 |
| 验证链路 | STOP_OPERATION、硬件急停事件、报警触发链 |

---

## 2. 框架侧文件清单

Demo 是框架自带的最小接入实例，可直接作为新项目 wiring 的对照物。

| 文件 | 职责 |
|------|------|
| `runtime/bootstrap/bootstrap.h` | `bootstrap_run()` 与启动失败后的进程约束 |
| `runtime/bootstrap/project_hooks.h` | `project_hooks_t` 15 个必填钩子 |
| `runtime/bootstrap/wiring.h` | `wiring()` 声明 |
| `domain/ports/outbound/device/device_ops_port.h` | 命令副作用对应的项目动作契约 |
| `runtime/ports/port_contract.h` | 必需端口的启动期集中校验（`PORT_REQ_*`） |
| `application/asset_contract.h` | 必需资产的启动期集中校验（`ASSET_REQ_*`） |
| `demo/app/demo_main.c` | 最小 main：调 `bootstrap_run()` 后驱动 smoke 场景 |
| `demo/wiring/wiring_sim.c` | 最小 provider 注册（sim HAL、storage、safety） |
| `demo/wiring/project_hooks_sim.c` | 15 个钩子的最小实现 |
| `demo/wiring/demo_device_ops.c` | 最小 `device_ops_t` |
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
| `device_ops.c` | 注册 `device_ops_t`，承接命令副作用 |
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
    └─ wiring() + project_hooks_register()
         ├─ 注册 storage adapter
         ├─ 注册 HAL provider
         ├─ 注册 cloud provider
         └─ 注册 engine program loader

bootstrap_load_storage()
    ├─ project_configure_storage()
    ├─ svc_param_init()
    └─ deploy_store.load()

bootstrap_configure()
    ├─ project_configure_hal()
    ├─ project_configure_safety()
    └─ project_configure_adapters()

bootstrap_bind()
    ├─ project_bind_hal()
    ├─ project_bind_device()
    ├─ project_bind_alarm_catalog()
    └─ project_validate()

bootstrap_init_hal()
    ├─ project_init_hal()
    ├─ project_init_device()
    └─ project_init_safety()

bootstrap_init_services()
    ├─ project_init_adapters()
    └─ project_register_runtime_tasks()

bootstrap_start()
    └─ project_start_runtime()
```

完整阶段序列（含框架自身的 init 调用）见 `doc/module-design/runtime/Runtime模块设计.md` §3。

---

## 4. Demo 实现说明

### 4.1 `demo_main`

`demo/app/demo_main.c` 调用 `bootstrap_run()` 后执行三段 smoke 检查：

| 检查 | 验证内容 |
|------|----------|
| STOP_OPERATION | `device_command_port.submit_sync/async` → command gateway → operational mode |
| HW ESTOP | `hw_estop_sim_set_active(true)` → `EVT_HW_ESTOP_ON` → op mode estop flag |
| Alarm trigger | `alarm_binding.trigger()` → `alarm_bridge_drain()` → registry blocking |

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
| `project_bind_device()` | `demo_device_ops_register()` |
| `project_init_device()` | 空实现 |
| `project_bind_alarm_catalog()` | `demo_alarm_catalog_load()` |
| `project_validate()` | `port_contract_validate()` 校验必需端口 |
| `project_init_adapters()` | 接入急停轮询适配器；不接观测桥（观测设施由项目显式启用） |
| `project_register_runtime_tasks()` | 空实现 |
| `project_start_runtime()` | 空实现 |
| `assert_safe_outputs`（hook 字段） | 空实现（仿真无真实输出可切断） |

### 4.4 `demo_device_ops`

Demo 注册 `device_ops_t`，所有动作为空或返回 `SW_OK`。这只验证 `side_effect_router` 能调用到项目端口，不验证真实机构动作。

### 4.5 `demo_alarm_catalog`

Demo 加载两个报警：

| Code | 等级 | 说明 |
|------|------|------|
| `201101` | MAJOR | blocking |
| `201709` | CRITICAL | LOCKOUT（AUTO_STATIC，Recover 不会清除） |

---

## 5. Machine Ops 接入

`device_ops_port` 是 framework application 到项目机构能力的出站端口。

```c
typedef struct {
    /* deferred_stop 在 safety_ops，不在 device_ops */
    void (*abort_home)(void);
    sw_err_t (*start_wash)(wash_mode_t mode);
    void (*abort_wash)(wash_abort_cause_t cause);
    sw_err_t (*home_device)(void);
    sw_err_t (*execute_manual_actuator)(uint32_t act_id, int32_t param);
    sw_err_t (*stop_all_outputs)(void);
    bool (*is_wash_entry_ready)(void);
} device_ops_t;
```

### 5.1 调用方

| machine op | 典型调用方 |
|------------|------------|
| （停机） | 延后完备停机走 `safety_ops.deferred_stop` |
| `abort_home` | 启动中止归位清障（异步）；完成后须发 `EVT_ABORT_HOME_DONE` |
| `start_wash` | `DEV_CMD_START_WASH`：选方案并**启动**会话后返回；禁止在调用内跑完整洗车 |
| `abort_wash` | `DEV_CMD_STOP_WASH` 副作用，以及急停/LOCKOUT 切断路径 |
| `home_device` | `recovery_service` 在 Recover 路径中启动的异步全机归位 |
| `execute_manual_actuator` | `DEV_CMD_MANUAL_ACTUATOR`：只下发/启定时后立即返回；禁止等待点动时长 |
| `stop_all_outputs` | `DEV_CMD_STOP_ALL_OUTPUTS` 副作用：切断输出；前态 WASHING 时 router 另调 `abort_wash(STOP_ALL)` |
| `is_wash_entry_ready` | `START_WASH` 附加门禁；未注册（NULL）时框架不拦截 |

`side_effect_router` 不做权限判断，只执行 `operational_mode` 已裁决允许的副作用。未注册或函数缺失时返回 `SW_ERR_NOT_INIT`。  
上述回调均在 **cmd_control** 线程经网关调用：任一实现内长时间阻塞，都会拖住后续命令（含软件停止）。硬急停 cutout 仍在 `estop_poll`，不受此影响。

### 5.2 项目实现要求

- `project_bind_device()` 中调用 `device_ops_register()`。
- `execute_manual_actuator` 的 `act_id` / `param` 由项目定义，并在云端/CLI 映射中保持一致；**须非阻塞**——下发动作或启动定时/运动后立即返回，点动时长与到位由项目侧自行管理，勿在回调内 `sleep`/轮询等待。
- `start_wash` / `home_device` / `abort_home` 同样只启动异步流程；完成后分别靠洗车事件、`EVT_OP_MODE_HOME_COMPLETED`、`EVT_ABORT_HOME_DONE` 收口。
- 发布 `EVT_OP_MODE_HOME_COMPLETED(成功)` 前须按目标姿态证明 ON_MOTION（只 `clear` 条件；过流等 MANUAL_RESET 不得在此清除）。
- `stop_all_outputs` 必须能落到与 cutout 等价的安全输出态；无活跃洗车会话时 router 不会调用 `abort_wash(STOP_ALL)`。
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
- 通过 `motor_executor_bind()` 绑定项目选定的静态槽位，并把返回的 `hal_motor_exec_t *` 注入机构控制模式。
- 经 `safety_port_register()` 注册 `safety_ops_t`，提供急停输入与安全切断实现。

### 6.3 Domain / Application

- 在 `project_bind_device()` 注册 `device_ops_t`。
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

选 INTERFACE 而非 STATIC，是因为同一份框架源在不同目标下需要不同编译定义（例如测试目标为 `hal_io_sim.c` 定义 `HAL_IO_SIM_UNIT_TEST`）。收益不是少编译一次，而是把源清单维护权收回框架内部：框架增删文件时项目只需重新配置。

可用分层目标（依赖逐层向下传递，link 上层自动带入下层）：

| 目标 | 内容 |
|------|------|
| `wdf_common` | 错误码、日志、时间、追踪上下文、点表模型、基础工具 |
| `wdf_ports` | 端口注册表与端口内自带实现 |
| `wdf_runtime` | bootstrap、事件总线、调度器 |
| `wdf_domain` | 运行模式、报警注册表、设备快照 |
| `wdf_application` | 命令网关、副作用路由、自检、恢复、遥测投影、报警与模式桥接 |
| `wdf_mechanism` | 单轴运动、流体路径（需项目提供 motor provider） |
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

`wdf_domain` 刻意不含机构控制与方案引擎：两者分别要求项目链接电机执行器实现与方案资产，最小接入（如框架自带 demo）并不需要，捆绑进核心会造成链接期缺符号。同理 `wdf_asset_contract`、`wdf_report_scheduler`、`wdf_observation_bridge` 各自独立，不接入的项目不必被迫链接 cloud、program_engine 或 observability。

Demo 的 `demo/CMakeLists.txt` 即按此方式装配，只额外补 `safety_sim` 与 `hw_estop_sim` 两个 sim 装配选择。

Snack 等 vendor provider 仍由根 CMake 开关以 STATIC 库提供，它们有外部 SDK 依赖，不适合无条件导出；电机执行器已纳入 `wdf_hal_components`：

| 开关 | 用途 |
|------|------|
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
| `test_device_ops_port` | `device_ops_t` 注册与命令副作用转发 |

`test_port_contract` 与 `test_asset_contract` 是接入契约的核心防线：两者都注入过
「探测函数恒返回 true」验证检查确有约束力。

用例数与通过情况以 `scripts/check_all.sh` 生成的 `build-check/test-results/report.html`
为准，本文不记录动态结论（原则见 `tests/reports/README.md`）。端到端的 `wdf_smoke`
同样在 `ctest` 内（标签 `framework;smoke`），覆盖范围见第 9.1 节。

---

## 9. 验证策略

### 9.1 Demo Smoke

`wdf_smoke` 是全仓唯一的跨层链路存在性证明（`../../contract/行为契约.md` 里唯一一条
L4）。它依次验证：

| 步骤 | 覆盖的链路 |
|------|-----------|
| `bootstrap_run()` | 7 阶段启动全过程 |
| `RECOVER` → IDLE | 命令网关 → 裁决 → `RECOVERY_REQUESTED` → `recovery_service` → `device_ops.home_device` → `EVT_OP_MODE_HOME_COMPLETED` → `EVT_OP_MODE_RECOVERY_COMPLETED` → IDLE |
| `STOP_OPERATION` | IDLE 下的停运裁决与运营开关 |
| 急停边沿 | `hw_estop_sim` → `estop_poll_thread` → `safety_cutout_execute` → `EVT_HW_ESTOP_ON` → 姿态收敛 |
| 报警触发 | `alarm_binding.trigger` → `alarm_bridge` → blocking 判定 |

全部通过时输出 `[Demo] All checks passed.`。

**已纳入 `ctest`**（标签 `framework;smoke`）。此前它不在 `ctest` 内，长期以退出码 1
的状态腐化而无人察觉——而"demo 用例与框架语义脱节"和"某次改动真的打断了急停链路"
这两种失败表现完全一样，都是退出码 1。不进门禁，这一级验证等于不存在。

两处历史失败已修，都在 demo 侧而非框架：

| 原失败 | 原因与修法 |
|--------|-----------|
| `STOP_OPERATION` 被拒 | 启动后是 `OP_MODE_STOPPED`，而矩阵中该命令仅 IDLE / WASH_DONE 允许——"停运"本就该从"在运营"发起。改为先 `RECOVER` 归位进 IDLE 再停运，顺带把归位链路纳入覆盖 |
| `EVT_HW_ESTOP_ON` 收不到 | `hw_estop_sim` 只维护状态，发布方是 `estop_poll_thread`，而 demo 的 `init_adapters()` 是空实现。改为在此接入该可选适配器 |

急停等待改为轮询而非固定睡眠：采集线程以 `SCHED_FIFO` 注册，非特权环境下会被
scheduler 降级为 `SCHED_OTHER`，边沿检测延迟随负载变化。固定等待会偶发失败，而
smoke 的偶发失败比不跑更糟——它会让真实回归被当成抖动忽略。

已注入验证两条链路确有约束力：`init_adapters` 不接急停采集时 smoke 失败；
`demo_home_device` 不回报归位完成时报 `mode should be IDLE after RECOVER+home`。

### 9.2 项目 Bring-up

建议按以下顺序逐步验证：

1. 只启 storage + sim HAL，确认 bootstrap。
2. 接入 device ops 空实现，确认 command gateway。
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
| 忘记注册 `device_ops` | HOME/MANUAL/STOP_ALL_OUTPUTS 命令副作用失败 |
| 报警目录晚于 detector 启动 | detector 触发未知报警码 |
| `project_start_runtime()` 后再注册周期任务 | 任务不会被当前 `scheduler_start_all()` 启动 |
| event handler 中执行阻塞 IO | 阻塞全局 event dispatch |
| 跳过 `project_hooks_t.assert_safe_outputs` 实现 | event bus fatal / IO panic 时无法兜底切断 |
| Demo stub 被误用于产品 | 命令看似成功但没有真实机构动作 |

---

## 11. 当前落地状态

| 能力 | 状态 |
|------|------|
| `bootstrap_run()` 7 阶段编排 | ✅ |
| `project_hooks_t` 15 钩子契约与 NULL 校验 | ✅ |
| 端口契约校验（`PORT_REQ_*`，14 位） | ✅ |
| 资产契约校验（`ASSET_REQ_*`，3 位） | ✅ |
| Demo 最小 wiring（sim HAL / storage / safety） | ✅ |
| `wdf_smoke` 端到端场景 | ✅ 已纳入 `ctest`，覆盖启动/命令/归位/急停/报警五条链路，见第 9.1 节 |
| 真机项目 wiring 参考实现 | 项目侧职责，框架只提供 Demo 作为对照 |

---

## 12. 相关文档

- `doc/module-design/runtime/Runtime模块设计.md` — bootstrap 和 project hooks 顺序
- `doc/module-design/ports-adapters/HAL端口与适配器模块设计.md` — HAL provider 与实例绑定
- `doc/module-design/ports-adapters/Storage端口与方案资产模块设计.md` — 参数、部署配置和方案资产
- `doc/module-design/domain/命令网关模块设计.md` — command gateway 与 side effect router
- `doc/module-design/domain/报警系统模块设计.md` — alarm catalog、binding、bridge
- `doc/module-design/ports-adapters/CloudModel模块设计.md` — cloud model/provider 接入
