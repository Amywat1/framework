# Runtime 模块设计

**版本**：v1.0  
**状态**：已落地（bootstrap 编排 + 线程注册表 + 周期任务 + 安全线程 + Demo hooks）  
**最后同步代码**：2026-07-12（`runtime/bootstrap`、`runtime/scheduler`、`runtime/platform`、Demo project hooks）  
**适用范围**：`runtime/bootstrap/`、`runtime/scheduler/`、`runtime/platform/`、`runtime/config/thread_config.h`、`demo/wiring/`  
**架构基线**：统一启动序列 + init 阶段注册线程 + scheduler 统一启动  
**关键词**：bootstrap_run、project_hooks、thread_registry、scheduler_start_all、periodic_task、safety_thread、event_dispatch

---

## 1. 设计目标与核心理念

Runtime 层负责把框架基础设施、项目 wiring、应用模块、适配器和后台线程按固定顺序启动。模块在 init 阶段只注册端口、订阅事件或登记线程，真正创建线程统一延后到 `scheduler_start_all()`。

真机入口层不参与设备 HAL 初始化编排。入口只负责进程级运行时配置，并在完成后调用 `bootstrap_run()`；具体 HAL 或外部 SDK 初始化由 provider 的 `init` 实现承接。

### 1.1 设计目标

- **启动顺序确定**：`bootstrap_run()` 固定 infra → safety → application → adapters → threads。
- **项目扩展受控**：项目只能通过 `wiring()` 与 `project_hooks` 填充装配点，不改框架主流程。
- **线程统一创建**：框架线程通过 `thread_register()` 登记，最后由 scheduler 创建并 detach。
- **周期任务统一模型**：周期任务用 `periodic_task_register()` 转成线程注册表条目。
- **致命故障安全停机**：event bus fatal 回调先执行 `project_assert_safe_outputs()`，再 `abort()`。

### 1.2 当前生命周期模型

| 阶段 | 规则 |
|------|------|
| 注册期 | wiring/init/project hooks 注册 port、订阅、线程和周期任务 |
| 启动期 | `scheduler_start_all()` 一次性创建所有已注册线程 |
| 运行期 | 线程 detach，当前不提供 join/stop/restart 语义 |
| 致命故障 | 安全输出兜底后终止进程，交由外部 supervisor 拉起 |

---

## 2. 启动序列

`bootstrap_run()` 是通用框架入口，当前顺序如下：

```text
bootstrap_run()
    ├─ bootstrap_init_infra()
    │    ├─ time_util_init()
    │    ├─ event_bus_init()
    │    ├─ event_bus_set_fatal_cb(system_panic_safe_stop)
    │    ├─ wiring()
    │    ├─ hal_io / hal_vfd / hal_voice init
    │    ├─ project_hal_extra_setup()
    │    └─ svc_param_init()
    │
    ├─ project_safety_init()
    │
    ├─ bootstrap_init_application()
    │    ├─ project_machine_setup()
    │    ├─ alarm_registry_init()
    │    ├─ safety_posture_init()
    │    ├─ project_alarm_catalog_init()
    │    ├─ alarm_event_bridge_init()
    │    ├─ safety_thread_init()
    │    ├─ operational_mode_init()
    │    ├─ command_gateway_init()
    │    ├─ self_check_service_init()
    │    ├─ op_mode_bridge_init()
    │    └─ project_report_scheduler_init()
    │
    ├─ bootstrap_init_adapters()
    │    ├─ deploy_store.load()
    │    └─ project_adapters_init()
    │
    └─ bootstrap_start_threads()
         ├─ thread_register("event_dispatch", ...)
         ├─ project_start_threads()
         └─ scheduler_start_all()
```

### 2.1 Infra 阶段

| 步骤 | 说明 |
|------|------|
| `time_util_init()` | 初始化单调时间源 |
| `event_bus_init()` | 初始化异步事件总线 |
| `wiring()` | 注册端口 provider、存储适配器、sim/真机 HAL 等 |
| `hal_*_bootstrap_init()` | 若对应 port 已注册且有 `init`，则调用 |
| `project_hal_extra_setup()` | 项目侧 HAL 参数下发或实例绑定后的补充配置 |
| `svc_param_init()` | 加载运行期参数；`SW_ERR_STORAGE` 被允许继续 |

补充边界约束：

- 入口层只做进程级配置，通过 runtime glue 调用进程 API，不直接调用 HAL SDK init。
- provider 若依赖外部 SDK，应在自身 `init` 内完成 SDK 初始化与日志桥接。

### 2.2 Application 阶段

Application 阶段初始化领域聚合、应用服务和事件订阅。关键顺序约束：

- `alarm_registry_init()` 必须早于 `project_alarm_catalog_init()`。
- `safety_posture_init()` 在报警目录加载前订阅报警事件。
- `operational_mode_init()` 必须早于 `command_gateway_init()` / `op_mode_bridge_init()`。
- `safety_thread_init()` 只注册线程，不立即启动。
- `project_report_scheduler_init()` 由项目决定是否配置云端上报周期任务。

### 2.3 Adapter 阶段

`deploy_store.load()` 在项目入站适配器初始化前执行，用于 MQTT 凭证、SN、topic 等部署期配置。文件缺失或损坏返回 `SW_ERR_STORAGE` 时允许继续，其他错误中止启动。

### 2.4 Threads 阶段

线程启动前最后注册 `event_dispatch`，再调用 `project_start_threads()` 注册项目周期任务，最后 `scheduler_start_all()` 创建线程表中的所有线程。

---

## 3. Project Hooks

项目钩子定义在 `runtime/bootstrap/project_hooks.h`。

| Hook | 调用时机 | 典型职责 |
|------|----------|----------|
| `project_hal_extra_setup()` | HAL port init 后 | VFD/传感器/IO 额外参数、启动安全态 |
| `project_safety_init()` | infra 后、application 前 | 项目安全默认态、传感器预热 |
| `project_machine_setup()` | application 起始 | 注册 `machine_ops`、机构装配 |
| `project_alarm_catalog_init()` | registry/posture 后 | 加载项目报警目录 |
| `project_report_scheduler_init()` | app 服务后 | 注册云端 report scheduler 策略 |
| `project_adapters_init()` | deploy store load 后 | 云端、CLI 等入站适配器初始化 |
| `project_start_threads()` | scheduler 启动前 | 注册项目周期任务 |
| `project_assert_safe_outputs()` | fatal / panic | 切断安全输出 |

Demo 实现位于 `demo/wiring/project_hooks_sim.c`：当前只注册 `alarm_bridge` 50ms 周期 drain，其他 hook 为空或注册 demo machine/alarm catalog。

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
| `event_dispatch` | `bootstrap_start_threads()` | 线程 | 调用 `event_bus_dispatch_loop()` |
| `safety_thread` | `safety_thread_init()` | 线程 | 轮询硬件急停边沿 |
| `wash_worker` | `wash_orchestrator_init()` | 线程 | 洗车 engine worker tick |
| `cloud_report` | `report_scheduler_init()` | 周期任务 | 云端周期/重同步上报 |
| `hal_sensor_poll` | `hal_sensor_poll_register_task()` | 周期任务 | DI 滤波推进 |
| `vfd_manager_poll` | `hal_vfd_manager_poll_register_task()` | 周期任务 | VFD fault/current/RST 监测 |
| `fluid_path_poll` | `fluid_path_init()` | 周期任务 | 流体路径阀泵时序推进 |
| `alarm_bridge` | Demo `project_start_threads()` | 周期任务 | drain alarm registry pending 事件 |

这些线程是否出现取决于模块是否初始化、项目是否注册对应任务，以及真机 provider 是否启用。

---

## 6. Safety Thread 与 Panic

### 6.1 Safety Thread

`safety_thread_init()` 注册 `safety_thread`，调度配置来自 `thread_config.h`：

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

`bootstrap_init_infra()` 注册 `system_panic_safe_stop()` 作为 event bus fatal 回调。发生不可恢复故障时：

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
| `tests/runtime/test_safety_thread.c` | safety_thread 注册、急停边沿事件 |
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

1. 优先选择已有 `project_hooks` 调用点。
2. 不在 `bootstrap.c` 中加入项目分支。
3. 需要先加载部署配置的适配器放在 `project_adapters_init()`。
4. 需要随 scheduler 启动的后台任务放在 `project_start_threads()`。

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
