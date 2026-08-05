# Runtime 模块设计

**版本**：v1.1  
**状态**：已落地（bootstrap 编排 + 线程注册表 + 周期任务 + Demo hooks）  
**最后同步代码**：2026-08-05（`runtime/bootstrap`、`runtime/scheduler`、`runtime/config`、Demo project hooks）  
**适用范围**：`runtime/bootstrap/`、`runtime/scheduler/`、`runtime/config/thread_config.h`、`demo/wiring/`  
**架构基线**：统一启动序列 + runtime tasks 阶段注册线程 + scheduler 统一启动  
**关键词**：bootstrap_run、project_hooks、thread_registry、scheduler_start_all、periodic_task、event_dispatch

---

## 1. 设计目标与核心理念

Runtime 层负责把框架基础设施、项目 wiring、应用模块、适配器和后台线程按固定顺序启动。启动生命周期按 `register → configure_storage → load_storage → configure → bind → validate → init_hal → init_machine → init_safety → init_services → start` 拆分；模块在注册、绑定和各 init 阶段只注册端口、订阅事件或登记线程，真正创建线程统一延后到 `scheduler_start_all()`。

真机入口层不参与设备 HAL 初始化编排。入口只负责进程级运行时配置，并在完成后调用 `bootstrap_run()`；具体 HAL 或外部 SDK 初始化由 provider 的 `init` 实现承接。

### 1.1 设计目标

- **启动顺序确定**：`bootstrap_run()` 固定 register → configure_storage → load_storage → configure → bind → validate → init_hal → init_machine → init_safety → init_services → start。
- **项目扩展受控**：项目只能通过 `wiring()` 与 `project_hooks` 填充装配点，不改框架主流程。
- **线程统一创建**：框架线程通过 `thread_register()` 登记，最后由 scheduler 创建并 detach。
- **周期任务统一模型**：周期任务用 `periodic_task_register()` 转成线程注册表条目。
- **致命故障安全停机**：event bus fatal 回调先执行 `project_hooks_t.assert_safe_outputs`，再 `abort()`。

### 1.2 当前生命周期模型

| 阶段 | 规则 |
|------|------|
| 注册期 | `wiring()` 只注册 port/provider/loader，不注入项目参数、不绑定实例、不初始化硬件 |
| 配置期 | 项目 hooks 注入存储路径、HAL 参数、安全默认态和适配器参数 |
| 加载期 | `svc_param_init()` 与 `deploy_store.load()` 读取存储，`SW_ERR_STORAGE` 允许继续 |
| 绑定期 | 项目 hooks 绑定 HAL 实例、`machine_ops`、报警目录等依赖关系 |
| 校验期 | 项目执行启动前一致性校验，不启动 watcher、线程或外部连接 |
| HAL 初始化期 | HAL port `init`（io/vfd/voice）及项目 HAL 组合层 init |
| 机构初始化期 | 项目机构与执行器 init，前置为 HAL 已就绪 |
| 安全初始化期 | 项目故障安全状态建立，前置为 HAL 与机构已就绪 |
| 服务初始化期 | 应用服务 init、适配器 init、项目运行期任务注册 |
| 启动期 | `scheduler_start_all()` 一次性创建所有已注册线程 |
| 运行期 | 线程 detach，当前不提供 join/stop/restart 语义 |
| 致命故障 | 安全输出兜底后终止进程，交由外部 supervisor 拉起 |

---

## 2. 文件清单

| 文件 | 职责 |
|------|------|
| `runtime/bootstrap/bootstrap.h` | `bootstrap_run()` 入口、启动失败后的进程约束契约 |
| `runtime/bootstrap/bootstrap.c` | 11 阶段启动编排、`BOOT_CHECK` 失败即返回 |
| `runtime/bootstrap/project_hooks.h` | `project_hooks_t` 15 个必填钩子与阶段语义 |
| `runtime/bootstrap/wiring.h` | `wiring()` 声明：项目只在此注册 provider |
| `runtime/scheduler/thread_registry.{h,c}` | 线程登记表、容量核算口径、`*_reset_for_test()` |
| `runtime/scheduler/scheduler.{h,c}` | `scheduler_start_all()` 统一创建并 detach 线程 |
| `runtime/scheduler/periodic_task.{h,c}` | 绝对下一拍唤醒的周期任务，跳过而不追赶 |
| `runtime/config/thread_config.h` | 各线程栈大小与优先级、周期档常量 |
| `tests/runtime/test_bootstrap_hooks.c` | 钩子校验与启动序列 |
| `tests/runtime/test_scheduler.c` | 线程登记、复位、scheduler 启动 |
| `tests/runtime/test_periodic_deadline.c` | 周期时序纯函数验证 |

事件总线虽在 `runtime/` 下，但契约独立，见 `EventBus模块设计.md`。

---

## 3. 启动序列

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
    │    ├─ hal_io_bootstrap_init()      → 并注册 IO panic 回调
    │    ├─ hal_vfd_bootstrap_init()
    │    ├─ hal_voice_bootstrap_init()
    │    └─ project_init_hal()
    │
    ├─ bootstrap_init_machine()
    │    └─ project_init_machine()
    │
    ├─ bootstrap_init_safety()
    │    └─ project_init_safety()
    │
    ├─ bootstrap_init_services()
    │    ├─ alarm_event_bridge_init()
    │    ├─ operational_mode_init()
    │    ├─ command_gateway_init()
    │    ├─ self_check_service_init()
    │    ├─ recovery_service_init()
    │    ├─ safety_cutout_coordinator_init()
    │    ├─ abort_home_coordinator_init()
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

每个阶段由 `bootstrap_run_phase()` 包裹，进入和完成都打 INFO 日志，失败时记录阶段名与错误码后立即返回，不继续后续阶段。

### 3.1 Register 阶段

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

### 3.2 Configure / Load / Bind 阶段

配置与绑定阶段用于把项目参数和实例关系注入到已注册的通用 provider 中：

- `project_configure_storage()` 在存储 load 前注入 JSON 路径或后端参数。
- `project_configure_hal()` 只下发 HAL 参数，例如 IO 子板配置、VFD/voice 串口参数。
- `project_configure_adapters()` 可读取已加载的 deploy/param 配置，但不启动连接或线程。
- `project_bind_hal()` 绑定传感器通道、VFD 实例、backend、事件回调等。
- `project_bind_machine()` 注册 `machine_ops_t`。
- `project_bind_alarm_catalog()` 在 `alarm_registry_init()` 后加载项目报警目录。

### 3.3 Validate 阶段

`project_validate()` 执行启动前一致性校验，例如云物模型表、部署配置、必选端口是否齐备。该阶段禁止初始化 watcher、读取实时 getter、发布事件或注册任务。

### 3.4 Init HAL / Machine / Safety 阶段

三个阶段按固定顺序拆开，因为它们的前置条件是递进的：

| 阶段 | 内容 | 为什么在这个位置 |
|------|------|------------------|
| `init_hal` | `hal_io/vfd/voice` port init，然后 `project_init_hal()` | port ops 就绪后才能做项目级传感器预热和组合层初始化 |
| `init_machine` | `project_init_machine()` | 机构与执行器初始化要读写 HAL，必须晚于 HAL init |
| `init_safety` | `project_init_safety()` | 建立故障安全输出态要求 HAL 与机构都已就绪，因此排在最后 |

`hal_io_bootstrap_init()` 在 IO port init 成功后，若 provider 提供 `register_panic_cb()`，会把项目的 `assert_safe_outputs` 注册为 IO panic 回调。三个 HAL port 的 `init` 均为可选：ops 未注册或未提供 `init` 时跳过并继续。

### 3.5 Init Services 阶段

Init Services 阶段初始化所有应用服务、适配器，并注册运行期任务。关键顺序约束：

- `operational_mode_init()` 必须早于 `command_gateway_init()` / `op_mode_bridge_init()`。
- `telemetry_projection_init()` 须晚于 `operational_mode_init()` 与 `alarm_registry_init()`，其初始同步要读这两个事实源。
- `project_init_adapters()` 初始化项目入站适配器，禁止启动后台线程。
- `project_register_runtime_tasks()` 只注册周期任务和运行期线程，禁止直接启动线程，线程统一由 Start 阶段的 `scheduler_start_all()` 创建。

**急停采集不在此阶段**：bootstrap 不引用 `estop_poll_thread_init()`。框架把它作为可选入站适配器提供，需要轮询采集的项目自行在 `project_init_adapters()` 中调用；已有自己采集通路（例如 DI detector 采样后经报警链路发布同样事件）的项目不接入，避免同一物理输入产生两条并发事件源。

**可观测桥接同样不在此阶段**：`observation_event_bridge_init()` 由项目在 `project_init_adapters()` 中决定是否调用。自带事件投影的项目不调，以免同一事件被两个订阅者各记一条。bootstrap 不代替项目做这个选择，也就不引用该符号。

### 3.6 Start 阶段

Start 阶段先调用 `hal_io.start()`，再调用 `project_start_runtime()` 启动无法纳入 scheduler 的项目运行期线程，最后 `scheduler_start_all()` 创建线程表中的所有线程。新增后台任务应优先接入 `project_register_runtime_tasks()`，不要放到 `project_start_runtime()`。

---

## 4. Project Hooks

项目钩子定义在 `runtime/bootstrap/project_hooks.h`。

`project_hooks_t` 共 15 个函数指针，按阶段归组：

| Hook | 调用时机 | 典型职责 |
|------|----------|----------|
| `configure_storage` | storage load 前 | 注入参数/部署配置路径或后端参数 |
| `configure_hal` | storage load 后、HAL bind 前 | 下发 HAL 参数，禁止绑定和初始化 |
| `configure_safety` | configure 阶段 | 注入项目安全策略参数，禁止访问硬件 |
| `configure_adapters` | storage load 后 | 配置云端、CLI 等适配器，禁止启动连接 |
| `bind_hal` | HAL init 前 | 绑定传感器通道、VFD 实例、backend、事件回调 |
| `bind_machine` | bind 阶段 | 注册 `machine_ops` 等设备装配接口 |
| `bind_alarm_catalog` | `alarm_registry_init()` 后 | 加载项目报警目录 |
| `validate` | 各 init 之前 | 启动前一致性校验 |
| `init_hal` | init_hal 阶段，HAL port init 后 | 初始化项目 HAL 组合层、预热传感器 |
| `init_machine` | init_machine 阶段 | 初始化项目机构与执行器 |
| `init_safety` | init_safety 阶段 | 建立项目故障安全状态 |
| `init_adapters` | init_services 阶段 | 初始化云端、CLI 等入站适配器；可选启用观测桥接、急停轮询 |
| `register_runtime_tasks` | init_services 阶段末 | 注册项目周期任务和运行期线程 |
| `start_runtime` | scheduler 启动前 | 启动无法纳入 scheduler 的项目线程 |
| `assert_safe_outputs` | fatal / IO panic | 切断安全输出（可以是空操作） |

`bootstrap_register_hooks()` 对全部 15 个指针做非空断言，缺一即返回 `SW_ERR_PARAM`。项目必须实现 `project_hooks_register()`，在其中填充结构体并调用注册函数；bootstrap 在 register 阶段调用它。

Demo 实现位于 `demo/wiring/project_hooks_sim.c`：只有 `configure_storage`（注入 JSON 路径）、`bind_machine`（`demo_machine_ops_register()`）、`bind_alarm_catalog`（`demo_alarm_catalog_load()`）和 `validate`（端口契约校验）有实质内容，其余为空实现。

### 4.1 何时使用 project hook

满足以下任意一条，该操作应放入 project hook，而非在项目侧直接调接口：

1. **框架需要强制时序**：操作必须在 bootstrap 某个阶段完成，且后续阶段依赖它已完成。例如 `configure_storage` 必须先于 `load_storage`，由框架代码保证顺序，不依赖项目开发者的调用纪律。

2. **不同场景行为不同**：target / sim / demo / test 需要不同实现，框架不能直接依赖某一套具体代码。hook 使框架只依赖函数指针，场景差异封装在各自的 `project_hooks.c` 中。

3. **框架需要检出缺失**：操作缺失会导致后续阶段静默失败，必须在启动最早期强制校验。`bootstrap_register_hooks()` 对全部 15 个函数指针做非空断言，缺一即返回 `SW_ERR_PARAM`。

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

## 5. Scheduler 模型

### 5.1 Thread Registry

`thread_registry` 是静态表，容量为 `THREAD_REGISTRY_MAX = 24`。槽位构成的实测口径记在 `thread_registry.h` 头部：框架固定占用 1（`event_dispatch`，接入急停轮询适配器再加 1）、引擎会话 worker 1~2、周期任务 9，合计约 13，余量留给项目新增周期任务，避免项目为加一个任务去改框架常量。

| API | 行为 |
|-----|------|
| `thread_register(name, fn, policy, prio, stack)` | 登记无参数线程 |
| `thread_register_arg(name, fn, arg, policy, prio, stack)` | 登记带参数线程 |
| `thread_registry_count()` | 返回已登记数量 |
| `thread_registry_get(idx)` | 返回只读线程条目 |
| `thread_registry_reset_for_test()` | 清空登记表，**仅供单元测试** |

参数非法返回 `SW_ERR_PARAM`，表满返回 `SW_ERR_OVERFLOW`。注册阶段只写表，不创建线程。

`thread_registry_reset_for_test()` 禁止在生产路径调用：线程一经 `scheduler_start_all()` 创建即 detach 且无法回收，清空登记表不会停止已启动的线程，只会让后续注册从 0 号槽开始，造成登记与实际线程不一致。

### 5.2 Scheduler Start

`scheduler_start_all()` 顺序遍历线程表，为每个条目设置栈大小和调度策略，然后 `pthread_create()`。

| 行为 | 说明 |
|------|------|
| 栈大小 | `stack_size > 0` 时调用 `pthread_attr_setstacksize()` |
| `SCHED_OTHER` | 普通线程，`prio` 固定传 0 |
| `SCHED_FIFO` | 设置显式调度与优先级；创建失败时降级重试 `SCHED_OTHER` |
| detach | 成功创建后立即 `pthread_detach()` |
| 失败 | 任一线程创建失败返回 `SW_ERR_HW` |

当前实现不保存 `pthread_t`，所以不提供停止、join 或重启接口。

### 5.3 Periodic Task

`periodic_task_register()` 把周期任务封装为一个线程注册表条目。周期任务表容量 `PERIODIC_TASK_MAX` 直接取 `THREAD_REGISTRY_MAX`（当前 24）：每个周期任务必然占一个线程槽，两者取同一上限就不会出现"周期任务表还有位、线程表已满"的半失败，容量调整也只需改一处。该关系由编译期断言固定。

周期以**绝对截止时间**推进，不是"回调返回后再睡固定时长"：

```text
periodic_task_thread_fn(slot)
    deadline = now(CLOCK_MONOTONIC)
    for (;;) {
        slot->fn(slot->ctx)
        now = clock_gettime(CLOCK_MONOTONIC)
        periodic_task_next_deadline(&deadline, slot->period_ms, &now)
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL)
    }
```

`periodic_task_next_deadline()` 独立导出，语义是：先推进一个周期；若推进后仍不晚于 `now`（说明回调耗时超过一个周期），继续推进直到严格晚于 `now`，返回本次跳过的拍数。因此回调耗时**不累加进下一拍**，超时的拍被**跳过而非追赶**。把这段时间推进逻辑做成纯函数，是为了不依赖真实 sleep 就能验证时序行为。

| 约束 | 说明 |
|------|------|
| `period_ms > 0` | 0 返回 `SW_ERR_PARAM` |
| `name` 生命周期 | 只保存指针不拷贝，必须是字面量或覆盖整个运行期的静态存储，不得传栈上缓冲 |
| 回调不应长阻塞 | 超过一个周期会导致跳拍 |
| 无停止语义 | 线程以 detach 创建且不可 join，进程退出即终止 |
| 表满 | 返回 `SW_ERR_OVERFLOW` |
| `clock_nanosleep` 失败 | `EINTR` 重试，其他错误打 ERROR 日志 |

---

## 6. 已注册线程与周期任务

| 名称 | 注册方 | 类型 | 职责 |
|------|--------|------|------|
| `event_dispatch` | `bootstrap_register()` | 线程 | 调用 `event_bus_dispatch_loop()` |
| `alarm_bridge` | `alarm_event_bridge_init()` | 周期任务（50ms） | drain alarm registry pending 事件并算姿态边沿 |
| 会话 worker | `engine_session_init()`，名称与栈由调用方配置传入 | 线程 | 驱动方案引擎 tick |
| `cloud_report_<period>ms` | `report_scheduler_register()` | 周期任务 | 云端链路 poll、watcher poll、周期/重同步上报；每种周期一个任务 |
| `hal_sensor_poll` | `hal_sensor_poll_register_task()` | 周期任务 | DI 滤波推进 |
| `vfd_manager_poll` | `hal_vfd_manager_poll_register_task()` | 周期任务 | VFD fault/current/RST 监测 |
| `estop_poll` | `estop_poll_thread_init()`，由项目在 `init_adapters` 选择接入 | 线程 | 轮询硬件急停边沿 |

只有 `event_dispatch` 与 `alarm_bridge` 是 bootstrap 无条件登记的；其余取决于对应模块是否初始化、项目是否注册该任务，以及真机 provider 是否启用。

`fluid_path` 不自建周期任务：领域层不创建线程，`fluid_path_poll(now_ms)` 需由调用方登记为周期任务驱动（推荐周期见 `THD_FLUID_PATH_POLL_PERIOD_MS`）。未周期调用时 `fluid_path_set/enable/disable` 的请求会停留在 pending 而不生效。

---

## 7. Safety Thread 与 Panic

### 7.1 急停轮询适配器（可选）

急停采集方式由项目决定，框架只提供一种可选实现：`adapters/inbound/safety/estop_poll_thread.c`。它属入站适配器而非运行时核心，bootstrap 不引用它；需要轮询采集的项目在 `project_init_adapters()` 中调用 `estop_poll_thread_init()` 登记线程。已有自采集通路的项目不接入。

调度配置来自 `thread_config.h`：

| 配置 | 值 |
|------|----|
| `THD_SAFETY_THREAD_STACK` | 8 KiB |
| `THD_SAFETY_THREAD_PRIO` | 90 |
| `THD_SAFETY_THREAD_POLL_US` | 5000 us |
| 调度策略 | `SCHED_FIFO`，创建失败由 scheduler 降级 `SCHED_OTHER` |

运行逻辑：

```text
loop:
    active = hw_estop_port_is_active()
    首次采样：记录初值；若已 active 按上升沿处理
    上升沿：safety_cutout_execute() → event_publish(EVT_HW_ESTOP_ON)
    下降沿：event_publish(EVT_HW_ESTOP_OFF)
    usleep(THD_SAFETY_THREAD_POLL_US)
```

两点行为约定：

- **切断失败不重试**，只记录 ERROR。重试会延长动力输出未确认切断的窗口，而失败原因通常在硬件链路本身（总线离线、板卡无响应），重试无从改变。
- **切断失败不阻断事件发布**。`EVT_HW_ESTOP_ON` 必须照常送出，否则领域层不会进入急停态，一次故障会同时丢掉切断与状态收敛两条路径。

`safety_cutout_execute()` 与 `hw_estop_port_is_active()` 均由项目通过 `safety_port_register()` 注册 `safety_ops_t` 提供实现，未注册时故障安全并首次告警，可由 `port_contract_validate(PORT_REQ_SAFETY)` 在启动期拦住。该线程注册后不可停止，与周期任务一致。

### 7.2 Event Bus Fatal

`bootstrap_register()` 注册 `system_panic_safe_stop()` 作为 event bus fatal 回调。发生不可恢复故障时：

1. 打印 fatal 日志。
2. 调用 `project_hooks_t.assert_safe_outputs`。
3. `abort()` 终止进程。

fatal 回调不尝试恢复 event bus，也不继续运行，因为 dispatch 线程为 detach 线程，返回会留下不完整运行状态。

---

## 8. Thread Config

`runtime/config/thread_config.h` 统一定义栈大小、周期和实时优先级。

| 宏 | 当前值 | 用途 |
|----|--------|------|
| `THD_EVENT_DISPATCH_STACK` | 16 KiB | event dispatch |
| `THD_WASH_WORKER_STACK` | 32 KiB | 会话 worker |
| `THD_HOME_WORKER_STACK` | 32 KiB | 项目归位 worker |
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

## 9. 错误处理策略

| 场景 | 行为 |
|------|------|
| 启动步骤返回非 `SW_OK` | `BOOT_CHECK` 记录日志并中止启动 |
| `svc_param_init()` 返回 `SW_ERR_STORAGE` | 允许继续，由业务默认值兜底 |
| `deploy_store.load()` 返回 `SW_ERR_STORAGE` | 允许继续，provider 可在后续初始化中处理缺省配置 |
| 线程注册表满 | 返回 `SW_ERR_OVERFLOW`，启动中止 |
| `SCHED_FIFO` 创建失败 | 降级 `SCHED_OTHER` 重试 |
| 普通线程创建失败 | 返回 `SW_ERR_HW`，启动中止 |
| event bus fatal | 安全输出兜底后 abort |
| `bootstrap_run()` 返回非 `SW_OK` | **调用方必须终止进程**，见下 |

### 9.1 启动失败后的进程约束

启动序列没有回滚路径：失败点之前的副作用全部保留（端口已注册、报警目录已载入、HAL 可能已初始化、线程可能已登记但未启动）。不提供 teardown 是有意的取舍——为覆盖任意阶段失败而维护一套对称的反初始化路径，其自身正确性比"失败即退出"更难保证，而嵌入式设备由进程管理器重启即可回到确定状态。

因此调用方不得重试 `bootstrap_run()`，也不得继续运行业务逻辑。重复调用的具体后果：`event_bus_init()` 会在 dispatch 线程可能已运行时重置队列与订阅表；线程登记表会累积重复条目，`scheduler_start_all()` 随后按整表创建线程，同一任务被启动多次。两者都不会立即报错，而是表现为难以定位的运行期异常。

多数接入错误应在 validate 阶段由 `port_contract_validate()` 拦住并给出完整缺失清单——此时尚未初始化硬件、也未启动线程，是最干净的失败点。项目应把必需端口声明写全，使失败尽量落在该阶段而非更晚。

---

## 10. 测试覆盖

| 测试 | 覆盖 |
|------|------|
| `tests/runtime/test_scheduler.c` | thread registry、periodic task 注册、scheduler start、表满 |
| `tests/runtime/test_periodic_deadline.c` | `periodic_task_next_deadline()` 纯函数时序：正常推进、超时跳拍、参数非法 |
| `tests/runtime/test_bootstrap_hooks.c` | hook 非空校验与阶段顺序 |
| `tests/adapters/test_estop_poll_thread.c` | 急停轮询线程注册、急停边沿事件 |
| `tests/runtime/test_event_bus.c` | event dispatch 线程手动启动与 shutdown |
| 应用/云端/HAL 单测 | 间接覆盖周期任务注册与事件订阅 |

用例数与通过情况以 `scripts/check_all.sh` 生成的 `build-check/test-results/report.html` 为准，本文不记录动态结论（原则见 `tests/reports/README.md`）。

---

## 11. 扩展指南

### 11.1 新增框架线程

1. 在模块 init 中调用 `thread_register()` 或 `thread_register_arg()`。
2. 线程入口不得依赖尚未初始化的 port 或服务。
3. 栈大小放入 `thread_config.h`，不要在业务代码中散落魔法数。
4. 若需要周期行为，优先使用 `periodic_task_register()`。

### 11.2 新增项目启动逻辑

判断该在哪里加逻辑，先用 §4.1 的判断流程确认是否需要 hook；确认后再按以下规则选择具体调用点：

1. **不在 `bootstrap.c` 中加入项目分支**：框架主流程不感知项目差异，所有项目逻辑通过 hook 注入。
2. **优先复用已有调用点**：从 §4 的 hook 表格中找时机匹配的调用点，不新增 hook 阶段。
3. **需要先加载部署配置再配置的适配器**放在 `project_configure_adapters()`（storage load 后）。
4. **需要初始化外部连接但不创建线程的适配器**放在 `project_init_adapters()`。
5. **需要随 scheduler 启动的后台周期任务**放在 `project_register_runtime_tasks()`。
6. **只有无法纳入 scheduler 的项目线程**才放在 `project_start_runtime()`。

### 11.3 禁止的扩展方式

- 在 init 阶段直接创建框架线程，绕过 `thread_registry`。
- 在线程回调中执行长时间阻塞的同步网络/文件操作。
- 在 fatal 回调中调用 event bus API 或等待其他线程退出。
- 修改 `bootstrap_run()` 顺序来满足单一项目需求。

---

## 12. 当前落地状态

| 能力 | 状态 |
|------|------|
| `bootstrap_run()` 11 阶段编排 | ✅ |
| `project_hooks_t` 15 钩子全必填校验 | ✅ |
| 线程登记表 + 统一 detach 启动 | ✅（`THREAD_REGISTRY_MAX` = 24） |
| 周期任务绝对下一拍唤醒 | ✅（跳过而不追赶，不累积漂移） |
| `thread_registry_reset_for_test()` | ✅（仅测试可用；已启动线程无法回收） |
| 启动失败后的进程约束 | ✅ 文档与头文件均已写明：失败即终止进程，不提供 teardown |
| 事件总线 fatal → 安全停机 | ✅ 经 `project_hooks_t.assert_safe_outputs` 后 `abort()` |
| 急停轮询适配器 | ✅ 可选接入（项目已有采集通路时不接） |
| 启动阶段回滚 / teardown | ❌ 有意不做，理由见第 9.1 节 |

---

## 13. 相关文档

- `doc/module-design/runtime/EventBus模块设计.md` — `event_dispatch` 与 fatal 回调契约
- `doc/module-design/ports-adapters/HAL端口与适配器模块设计.md` — HAL init、周期任务与 provider 装配
- `doc/module-design/domain/报警系统模块设计.md` — `alarm_bridge` drain 与安全姿态
- `doc/module-design/domain/命令网关模块设计.md` — command gateway 在 dispatch 线程中的执行模型
