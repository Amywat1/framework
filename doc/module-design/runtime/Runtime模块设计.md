# Runtime 模块设计

**版本**：v1.0  
**状态**：已落地（bootstrap 编排 + 线程注册表 + 周期任务 + 安全线程 + Demo hooks）  
**最后同步代码**：2026-07-14（`runtime/bootstrap`、`runtime/scheduler`、`runtime/platform`、Demo project hooks）  
**适用范围**：`runtime/bootstrap/`、`runtime/scheduler/`、`runtime/platform/`、`runtime/config/thread_config.h`、`demo/wiring/`  
**架构基线**：统一启动序列 + runtime tasks 阶段注册线程 + scheduler 统一启动  
**关键词**：bootstrap_run、project_hooks、thread_registry、scheduler_start_all、periodic_task、estop_poll、event_dispatch

---

## 1. 设计目标与核心理念

Runtime 层负责把框架基础设施、项目 wiring、应用模块、适配器和后台线程按固定顺序启动。启动生命周期按 `register → configure_storage → load → configure → bind → validate → init_hal → init_services → start` 拆分；模块在 `register/bind/init_hal/init_services` 阶段只注册端口、订阅事件或登记线程，真正创建线程统一延后到 `scheduler_start_all()`。

真机入口层不参与设备 HAL 初始化编排。入口只负责进程级运行时配置，并在完成后调用 `bootstrap_run()`；具体 HAL 或外部 SDK 初始化由 provider 的 `init` 实现承接。

### 1.1 设计目标

- **启动顺序确定**：`bootstrap_run()` 固定 register → configure_storage → load → configure → bind → validate → init_hal → init_services → start。
- **项目扩展受控**：项目只能通过 `wiring()` 与 `project_hooks` 填充装配点，不改框架主流程。
- **线程统一创建**：框架线程通过 `thread_register()` 登记，最后由 scheduler 创建并 detach。
- **周期任务统一模型**：周期任务用 `periodic_task_register()` 转成线程注册表条目。
- **致命故障安全停机**：event bus fatal 回调先执行 `project_assert_safe_outputs()`，再 `abort()`。

### 1.2 当前生命周期模型

| 阶段 | 规则 |
|------|------|
| 注册期 | `wiring()` 只注册 port/provider/loader，不注入项目参数、不绑定实例、不初始化硬件 |
| 配置期 | 项目 hooks 注入存储路径、HAL 参数、安全默认态和适配器参数 |
| 加载期 | `svc_param_init()` 与 `deploy_store.load()` 读取存储，`SW_ERR_STORAGE` 允许继续 |
| 绑定期 | 项目 hooks 绑定 HAL 实例、`machine_ops`、报警目录等依赖关系 |
| 校验期 | 项目执行启动前一致性校验，不启动 watcher、线程或外部连接 |
| HAL 初始化期 | HAL port `init`（io/vfd/voice）及项目 HAL 组合层 init |
| 服务初始化期 | 应用服务 init、适配器 init、项目运行期任务注册 |
| 启动期 | `scheduler_start_all()` 一次性创建所有已注册线程 |
| 运行期 | 线程 detach，当前不提供 join/stop/restart 语义 |
| 致命故障 | 安全输出兜底后终止进程，交由外部 supervisor 拉起 |

---

## 2. 启动序列

`bootstrap_run()` 是通用框架入口，当前顺序如下：

```text
bootstrap_run()
    ├─ bootstrap_register()
    │    ├─ time_util_init()
    │    ├─ event_bus_init()
    │    ├─ event_bus_set_fatal_cb(system_panic_safe_stop)
    │    ├─ wiring()
    │    └─ thread_register("event_dispatch", ...)
    │
    ├─ bootstrap_configure_storage()
    │    └─ project_configure_storage()
    │
    ├─ bootstrap_load_storage()
    │    ├─ svc_param_init()
    │    └─ deploy_store.load()
    │
    ├─ bootstrap_configure()
    │    ├─ project_configure_hal()
    │    ├─ project_configure_safety()
    │    └─ project_configure_adapters()
    │
    ├─ bootstrap_bind()
    │    ├─ project_bind_hal()
    │    ├─ project_bind_machine()
    │    ├─ alarm_registry_init()
    │    └─ project_bind_alarm_catalog()
    │
    ├─ bootstrap_validate()
    │    └─ project_validate()
    │
    ├─ bootstrap_init_hal()
    │    ├─ hal_io_bootstrap_init()
    │    ├─ hal_vfd_bootstrap_init()
    │    ├─ hal_voice_bootstrap_init()
    │    └─ project_init_hal()
    │
    ├─ bootstrap_init_services()
    │    ├─ alarm_event_bridge_init()
    │    ├─ estop_poll_thread_init()   # 可选
    │    ├─ operational_mode_init()
    │    ├─ command_gateway_init()
    │    ├─ self_check_service_init()
    │    ├─ alarm_lifecycle_bridge_init()
    │    ├─ op_mode_bridge_init()
    │    ├─ telemetry_projection_init()
    │    ├─ project_init_adapters()
    │    └─ project_register_runtime_tasks()
    │
    └─ bootstrap_start()
         ├─ hal_io.start()
         ├─ project_start_runtime()
         └─ scheduler_start_all()
```

### 2.1 Register 阶段

| 步骤 | 说明 |
|------|------|
| `time_util_init()` | 初始化单调时间源 |
| `event_bus_init()` | 初始化异步事件总线 |
| `wiring()` | 注册端口 provider、存储适配器、sim/真机 HAL、engine loader 等 |
| `thread_register("event_dispatch", ...)` | 登记事件分发线程，尚不创建线程 |

补充边界约束：

- `wiring()` 只做注册，不读取配置文件、不绑定项目实例、不初始化硬件、不启动线程。
- 入口层只做进程级配置，通过 runtime glue 调用进程 API，不直接调用 HAL SDK init。
- provider 若依赖外部 SDK，应在自身 `init` 内完成 SDK 初始化与日志桥接。

### 2.2 Configure / Load / Bind 阶段

配置与绑定阶段用于把项目参数和实例关系注入到已注册的通用 provider 中：

- `project_configure_storage()` 在存储 load 前注入 JSON 路径或后端参数。
- `project_configure_hal()` 只下发 HAL 参数，例如 IO 子板配置、VFD/voice 串口参数。
- `project_configure_adapters()` 可读取已加载的 deploy/param 配置，但不启动连接或线程。
- `project_bind_hal()` 绑定传感器通道、VFD 实例、backend、事件回调等。
- `project_bind_machine()` 注册 `machine_ops_t`。
- `project_bind_alarm_catalog()` 在 `alarm_registry_init()` 后加载项目报警目录。

### 2.3 Validate 阶段

`project_validate()` 执行启动前一致性校验，例如云物模型表、部署配置、必选端口是否齐备。该阶段禁止初始化 watcher、读取实时 getter、发布事件或注册任务。

### 2.4 Init HAL 阶段

Init HAL 阶段初始化 HAL port 层及项目 HAL 组合层。顺序约束：`hal_io/vfd/voice` port init 早于 `project_init_hal()`，保证 port ops 就绪后再做项目级传感器预热和组合层初始化。

### 2.5 Init Services 阶段

Init Services 阶段初始化所有应用服务、适配器，并注册运行期任务。关键顺序约束：

- `operational_mode_init()` 必须早于 `command_gateway_init()` / `op_mode_bridge_init()`。
- `estop_poll_thread_init()` 只注册线程，不立即启动；项目自行采集急停时不接入。
- `project_init_adapters()` 初始化项目入站适配器，禁止启动后台线程。
- `project_register_runtime_tasks()` 只注册周期任务和运行期线程，禁止直接启动线程，线程统一由 Start 阶段的 `scheduler_start_all()` 创建。

### 2.6 Start 阶段

Start 阶段先调用 `hal_io.start()`，再调用 `project_start_runtime()` 启动无法纳入 scheduler 的项目运行期线程，最后 `scheduler_start_all()` 创建线程表中的所有线程。新增后台任务应优先接入 `project_register_runtime_tasks()`，不要放到 `project_start_runtime()`。

---

## 3. Project Hooks

项目钩子定义在 `runtime/bootstrap/project_hooks.h`。

| Hook | 调用时机 | 典型职责 |
|------|----------|----------|
| `project_configure_storage()` | storage load 前 | 注入参数/部署配置路径或后端参数 |
| `project_configure_hal()` | storage load 后、HAL bind 前 | 下发 HAL 参数，禁止绑定和初始化 |
| `project_bind_hal()` | HAL init 前 | 绑定传感器通道、VFD 实例、backend、事件回调 |
| `project_init_hal()` | init_hal 阶段，HAL port init 后 | 初始化项目 HAL 组合层、预热传感器 |
| `project_configure_safety()` | configure 阶段 | 建立项目安全默认态 |
| `project_configure_adapters()` | storage load 后 | 配置云端、CLI 等适配器，禁止启动连接 |
| `project_bind_machine()` | bind 阶段 | 注册 `machine_ops`、机构装配 |
| `project_bind_alarm_catalog()` | registry/posture 后 | 加载项目报警目录 |
| `project_validate()` | init 前 | 启动前一致性校验 |
| `project_init_adapters()` | init_services 阶段 | 初始化云端、CLI 等入站适配器 |
| `project_register_runtime_tasks()` | init_services 阶段末 | 注册项目周期任务和运行期线程 |
| `project_start_runtime()` | scheduler 启动前 | 启动无法纳入 scheduler 的项目线程 |
| `project_assert_safe_outputs()` | fatal / panic | 切断安全输出 |

Demo 实现位于 `demo/wiring/project_hooks_sim.c`：当前只注册 JSON 存储路径、demo machine/alarm catalog 和 `alarm_bridge` 50ms 周期 drain，其他 hook 为空。

### 3.1 何时使用 project hook

满足以下任意一条，该操作应放入 project hook，而非在项目侧直接调接口：

1. **框架需要强制时序**：操作必须在 bootstrap 某个阶段完成，且后续阶段依赖它已完成。例如 `configure_storage` 必须先于 `load_storage`，由框架代码保证顺序，不依赖项目开发者的调用纪律。

2. **不同场景行为不同**：target / sim / demo / test 需要不同实现，框架不能直接依赖某一套具体代码。hook 使框架只依赖函数指针，场景差异封装在各自的 `project_hooks.c` 中。

3. **框架需要检出缺失**：操作缺失会导致后续阶段静默失败，必须在启动最早期强制校验。`bootstrap_register_hooks()` 对全部 13 个函数指针做非空断言，缺一即返回 `SW_ERR_PARAM`。

**不需要 hook 的情况**：操作发生在 bootstrap 完成后的运行期（框架不再编排时序）；各场景实现完全相同；操作是单向的、无依赖的，框架不需要知道它是否发生。

判断流程：

```
这个操作是否必须在 bootstrap 某个具体阶段发生？
  否 → 不需要 hook，运行期直接调框架接口
  是 ↓
不同部署场景（target / sim / demo）实现是否不同？
  否 → 可在对应 bootstrap 阶段直接调，不需要 hook
  是 ↓
→ 放入 project_hooks_t，由框架编排时序并校验完整性
```

---

## 4. Scheduler 模型

### 4.1 Thread Registry

`thread_registry` 是静态表，容量为 `THREAD_REGISTRY_MAX = 16`。

| API | 行为 |
|-----|------|
| `thread_register(name, fn, policy, prio, stack)` | 登记无参数线程 |
| `thread_register_arg(name, fn, arg, policy, prio, stack)` | 登记带参数线程 |
| `thread_registry_count()` | 返回已登记数量 |
| `thread_registry_get(idx)` | 返回只读线程条目 |

参数非法返回 `SW_ERR_PARAM`，表满返回 `SW_ERR_OVERFLOW`。注册阶段只写表，不创建线程。

### 4.2 Scheduler Start

`scheduler_start_all()` 顺序遍历线程表，为每个条目设置栈大小和调度策略，然后 `pthread_create()`。

| 行为 | 说明 |
|------|------|
| 栈大小 | `stack_size > 0` 时调用 `pthread_attr_setstacksize()` |
| `SCHED_OTHER` | 普通线程，`prio` 固定传 0 |
| `SCHED_FIFO` | 设置显式调度与优先级；创建失败时降级重试 `SCHED_OTHER` |
| detach | 成功创建后立即 `pthread_detach()` |
| 失败 | 任一线程创建失败返回 `SW_ERR_HW` |

当前实现不保存 `pthread_t`，所以不提供停止、join 或重启接口。

### 4.3 Periodic Task

`periodic_task_register()` 把周期任务封装为一个线程注册表条目。周期任务表容量 `PERIODIC_TASK_MAX = 8`。

```text
periodic_task_thread_fn(slot)
    for (;;) {
        slot->fn(slot->ctx);
        usleep(slot->period_ms * 1000);
    }
```

| 约束 | 说明 |
|------|------|
| `period_ms > 0` | 0 返回 `SW_ERR_PARAM` |
| 回调不应长阻塞 | 回调耗时会直接拉长实际周期 |
| 无停止语义 | 线程启动后固定循环 |
| 表满 | 返回 `SW_ERR_OVERFLOW` |

---

## 5. 已注册线程与周期任务

| 名称 | 注册方 | 类型 | 职责 |
|------|--------|------|------|
| `event_dispatch` | `bootstrap_register()` | 线程 | 调用 `event_bus_dispatch_loop()` |
| `estop_poll` | `estop_poll_thread_init()` | 线程 | 轮询硬件急停边沿（可选） |
| `wash_worker` | `wash_orchestrator_init()` | 线程 | 洗车 engine worker tick |
| `cloud_report` | `report_scheduler_register()` | 周期任务 | 云端链路 poll、watcher poll、周期/重同步上报 |
| `hal_sensor_poll` | `hal_sensor_poll_register_task()` | 周期任务 | DI 滤波推进 |
| `vfd_manager_poll` | `hal_vfd_manager_poll_register_task()` | 周期任务 | VFD fault/current/RST 监测 |
| `fluid_path_poll` | `fluid_path_init()` | 周期任务 | 流体路径阀泵时序推进 |
| `alarm_bridge` | Demo `project_register_runtime_tasks()` | 周期任务 | drain alarm registry pending 事件 |

这些线程是否出现取决于模块是否初始化、项目是否注册对应任务，以及真机 provider 是否启用。

---

## 6. Safety Thread 与 Panic

### 6.1 Safety Thread

`estop_poll_thread_init()` 注册 `estop_poll`，调度配置来自 `thread_config.h`：

| 配置 | 值 |
|------|----|
| `THD_SAFETY_THREAD_STACK` | 8 KiB |
| `THD_SAFETY_THREAD_PRIO` | 90 |
| `THD_SAFETY_THREAD_POLL_US` | 5000 us |
| 调度策略 | `SCHED_FIFO`，失败降级 `SCHED_OTHER` |

运行逻辑：

```text
loop:
    active = hw_estop_port_is_active()
    if rising edge:
        device_stop_all_actuators()
        event_publish(EVT_HW_ESTOP_ON)
    if falling edge:
        event_publish(EVT_HW_ESTOP_OFF)
    usleep(THD_SAFETY_THREAD_POLL_US)
```

`device_stop_all_actuators()` 委托 `safety_cutout_execute()`。`hw_estop_port_is_active()` 与 `safety_cutout_execute()` 都有弱符号默认实现，项目可覆盖。

### 6.2 Event Bus Fatal

`bootstrap_register()` 注册 `system_panic_safe_stop()` 作为 event bus fatal 回调。发生不可恢复故障时：

1. 打印 fatal 日志。
2. 调用 `project_assert_safe_outputs()`。
3. `abort()` 终止进程。

fatal 回调不尝试恢复 event bus，也不继续运行，因为 dispatch 线程为 detach 线程，返回会留下不完整运行状态。

---

## 7. Thread Config

`runtime/config/thread_config.h` 统一定义栈大小、周期和实时优先级。

| 宏 | 当前值 | 用途 |
|----|--------|------|
| `THD_EVENT_DISPATCH_STACK` | 16 KiB | event dispatch |
| `THD_WASH_WORKER_STACK` | 32 KiB | wash worker |
| `THD_CLOUD_STACK` | 32 KiB | cloud report |
| `THD_CLOUD_REPORT_PERIOD_MS` | 500 ms | 默认云端上报周期 |
| `THD_VFD_TICK_STACK` | 16 KiB | VFD manager |
| `THD_SENSOR_POLL_STACK` | 16 KiB | sensor filter |
| `THD_FLUID_PATH_POLL_STACK` | 16 KiB | fluid path |
| `THD_FLUID_PATH_POLL_PERIOD_MS` | 10 ms | fluid path poll |
| `THD_SAFETY_THREAD_STACK` | 8 KiB | safety thread |
| `THD_SAFETY_THREAD_PRIO` | 90 | safety thread realtime priority |
| `THD_SAFETY_THREAD_POLL_US` | 5000 us | estop poll interval |

IO 子板 provider 的后台线程由 `io_exp_driver` 自行管理，不走 core scheduler。

---

## 8. 错误处理策略

| 场景 | 行为 |
|------|------|
| 启动步骤返回非 `SW_OK` | `BOOT_CHECK` 记录日志并中止启动 |
| `svc_param_init()` 返回 `SW_ERR_STORAGE` | 允许继续，由业务默认值兜底 |
| `deploy_store.load()` 返回 `SW_ERR_STORAGE` | 允许继续，provider 可在后续初始化中处理缺省配置 |
| 线程注册表满 | 返回 `SW_ERR_OVERFLOW`，启动中止 |
| `SCHED_FIFO` 创建失败 | 降级 `SCHED_OTHER` 重试 |
| 普通线程创建失败 | 返回 `SW_ERR_HW`，启动中止 |
| event bus fatal | 安全输出兜底后 abort |

---

## 9. 测试覆盖

| 测试 | 覆盖 |
|------|------|
| `tests/runtime/test_scheduler.c` | thread registry、periodic task、scheduler start、表满 |
| `tests/adapters/test_estop_poll_thread.c` | 急停轮询线程注册、急停边沿事件 |
| `tests/runtime/test_event_bus.c` | event dispatch 线程手动启动与 shutdown |
| 应用/云端/HAL 单测 | 间接覆盖周期任务注册与事件订阅 |

验证命令：

```bash
cmake --build build-native -j4
ctest --test-dir build-native --output-on-failure
```

---

## 10. 扩展指南

### 10.1 新增框架线程

1. 在模块 init 中调用 `thread_register()` 或 `thread_register_arg()`。
2. 线程入口不得依赖尚未初始化的 port 或服务。
3. 栈大小放入 `thread_config.h`，不要在业务代码中散落魔法数。
4. 若需要周期行为，优先使用 `periodic_task_register()`。

### 10.2 新增项目启动逻辑

判断该在哪里加逻辑，先用 §3.1 的判断流程确认是否需要 hook；确认后再按以下规则选择具体调用点：

1. **不在 `bootstrap.c` 中加入项目分支**：框架主流程不感知项目差异，所有项目逻辑通过 hook 注入。
2. **优先复用已有调用点**：从 §3 的 hook 表格中找时机匹配的调用点，不新增 hook 阶段。
3. **需要先加载部署配置再配置的适配器**放在 `project_configure_adapters()`（storage load 后）。
4. **需要初始化外部连接但不创建线程的适配器**放在 `project_init_adapters()`。
5. **需要随 scheduler 启动的后台周期任务**放在 `project_register_runtime_tasks()`。
6. **只有无法纳入 scheduler 的项目线程**才放在 `project_start_runtime()`。

### 10.3 禁止的扩展方式

- 在 init 阶段直接创建框架线程，绕过 `thread_registry`。
- 在线程回调中执行长时间阻塞的同步网络/文件操作。
- 在 fatal 回调中调用 event bus API 或等待其他线程退出。
- 修改 `bootstrap_run()` 顺序来满足单一项目需求。

---

## 11. 相关文档

- `doc/module-design/runtime/EventBus模块设计.md` — `event_dispatch` 与 fatal 回调契约
- `doc/module-design/ports-adapters/HAL端口与适配器模块设计.md` — HAL init、周期任务与 provider 装配
- `doc/module-design/domain/报警系统模块设计.md` — `alarm_bridge` drain 与安全姿态
- `doc/module-design/domain/命令网关模块设计.md` — command gateway 在 dispatch 线程中的执行模型
