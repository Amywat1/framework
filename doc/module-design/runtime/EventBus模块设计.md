# EventBus 模块设计

**版本**：v1.2  
**状态**：已落地（核心实现 + scheduler + Demo bootstrap）  
**最后同步代码**：2026-08-21（`event_bus_drain()` 同步排空、dispatch 共用出队路径）  
**适用范围**：`common/event_types.h`、`runtime/event_bus/`、`runtime/scheduler/`、`runtime/bootstrap/`、`application/` 订阅方  
**架构基线**：Ports & Adapters + 单 dispatch 线程 + 编译期固定容量  
**关键词**：event_bus、发布订阅、双队列、复合事件编码、`event_dispatch` 线程

---

## 1. 设计目标与核心理念

事件总线是框架运行期的**异步通知骨干**，负责把跨模块的状态变化以统一契约广播给订阅方，而不承担业务决策。

### 1.1 设计目标

- **统一异步语义**：命令结果、安全姿态、报警生命周期、流程完成、云端边沿等通知走同一套 API。
- **单分发线程**：所有 handler 在 `event_dispatch` 线程串行执行，降低业务模块间的锁竞争。
- **安全优先**：`SAFETY` 类、硬件急停边沿、IO 子板在线/离线边沿走高优先级队列，不被普通业务事件阻塞。
- **零动态内存**：队列、订阅表容量在编译期固定，适合嵌入式长期运行。
- **发布方轻量化**：发布方只传 `type + param`；`timestamp_ms` 由总线入队时自动填充。

### 1.2 核心理念

| 原则 | 规定 |
|------|------|
| 总线不决策 | event_bus 只负责入队、出队、回调；业务规则留在 domain / application |
| 载荷极简 | `param` 为 `uint32_t` 标量（报警码、模式枚举、点位索引等）；复杂状态通过领域模块查询 |
| 不适合总线的场景 | 同一用例内部顺序步骤、简单即时动作、需同步返回结果的调用 |
| 与急停双通道并存 | 急停快速通道负责即时切断；event_bus 负责记录、上报、姿态广播（见 §6.4） |

### 1.3 核心概念一览

```text
事件类型（event_type_t）
  ├── 类别（event_category_t，高 8 位）
  └── 类内编号（local_id，低 8 位）

事件实例（event_t）
  ├── type
  ├── param（发布方载荷）
  └── timestamp_ms（总线入队时填充）

事件总线（event_bus）
  ├── 高优先级队列（SAFETY + ESTOP + IO 在线/离线）
  ├── 普通优先级队列
  ├── 订阅表（按 type 索引 handler 列表）
  └── 分发循环（event_bus_dispatch_loop，由 dispatch 线程驱动）
```

---

## 2. 分层归属与依赖方向

### 2.1 模块落位

| 层次 | 路径 | 职责 |
|------|------|------|
| **类型契约** | `common/event_types.h` | 事件类别、ID 宏、`event_t` 结构体、编解码辅助 |
| **时钟依赖** | `common/time_util.h` | 单调毫秒时间戳；须在 `event_bus_init()` 之前初始化 |
| **总线实现** | `runtime/event_bus/event_bus.{h,c}` | 发布、订阅、分发、统计、shutdown、fatal 回调 |
| **容量配置** | `runtime/event_bus/event_bus_config.h` | 队列深度、每事件 handler 上限（编译期固定） |
| **线程栈配置** | `runtime/config/thread_config.h` | `THD_EVENT_DISPATCH_STACK` 等（完整框架 scheduler 使用） |
| **线程接入** | `runtime/scheduler/`（完整框架） | 注册 `event_dispatch` 线程，调用 `event_bus_dispatch_loop()` |
| **业务发布方** | `domain/`、`application/`、`adapters/` | 调用 `event_publish()` |
| **业务订阅方** | `application/`、`domain/`（少数） | 调用 `event_subscribe()` / `event_subscribe_table()` |

### 2.2 依赖方向

```text
application / domain / adapters
        │ event_publish / event_subscribe
        ▼
   runtime/event_bus
        │
        ▼
   common/event_types + common/time_util
```

**依赖禁令**：

- `event_bus` 不得依赖 `adapters/`、`domain/` 或任何项目业务头文件。
- `event_bus` 不得承载设备业务语义（不得解析 `param` 含义）。
- 业务模块不得绕过总线直接调用其他模块的 handler。
- handler 内不得调用 `event_publish()` / `event_subscribe()`（避免死锁；fatal 回调同样禁止）。

### 2.3 启动顺序契约

```text
time_util_init()
    ↓
event_bus_init() + event_bus_set_fatal_cb()
    ↓
wiring()（端口注册）
    ↓
project configure / bind / validate
    ↓
业务模块 init()（见 bootstrap_init）
    ↓
bootstrap_start()
    event_dispatch 线程 → event_bus_dispatch_loop()
    scheduler_start_all()
```

`wash-device-framework` Demo 经 `bootstrap_run()` 在 register 阶段登记 `event_dispatch` 线程，并在 start 阶段由 `scheduler_start_all()` 启动；项目周期任务应在 `project_register_runtime_tasks()` 登记，单元测试仍由测试代码手动起 dispatch 线程。

---

## 3. 文件清单

| 文件 | 职责 |
|------|------|
| `common/event_types.h` | 事件类别枚举、复合 ID 宏、`event_t`、`event_type_is_valid()` |
| `common/time_util.h` | `time_util_get_ms()` → `uint64_t` 单调毫秒 |
| `runtime/event_bus/event_bus_config.h` | `EVENT_BUS_QUEUE_SIZE`、`EVENT_BUS_HI_QUEUE_SIZE`、`EVENT_BUS_MAX_SUBS_PER_EVT` |
| `runtime/config/thread_config.h` | dispatch / 业务线程栈大小（与总线容量无编译耦合） |
| `runtime/event_bus/event_bus.h` | 对外 API、统计结构、fatal 回调契约 |
| `runtime/event_bus/event_bus.c` | 双队列、订阅表、信号量唤醒、分发循环 |
| `tests/runtime/test_event_bus.c` | 总线行为单元测试（手动起 dispatch 线程） |
| `tests/reports/event_bus.md` | 单元测试报告 |

---

## 4. 数据模型

### 4.1 事件类型编码

`event_type_t` 为 `uint16_t`，采用复合编码：

```c
#define EVT_MAKE(category, local_id) \
    ((event_type_t)((((uint16_t)(category) & 0xFFU) << 8) | \
                    ((uint16_t)(local_id) & 0xFFU)))
```

| 字段 | 位宽 | 说明 |
|------|------|------|
| `category` | 高 8 位 | `event_category_t`，取值 `1 … EVT_CAT_MAX-1` |
| `local_id` | 低 8 位 | 类内事件编号，取值 `0 … EVT_PER_CAT_MAX-1`（当前上限 32） |

辅助函数：

| 函数 | 返回值 |
|------|--------|
| `event_type_category(type)` | 类别枚举 |
| `event_type_local_id(type)` | 类内编号 |
| `event_type_is_valid(type)` | `type != EVT_NONE` 且类别、编号在合法范围 |

### 4.2 事件载荷

```c
typedef struct
{
    event_type_t type;
    uint32_t     param;         /* 报警码、错误码、模式等；无载荷传 0 */
    uint64_t     timestamp_ms;  /* 入队时间戳，由 event_bus 填充 */
} event_t;
```

| 字段 | 发布方 | 说明 |
|------|--------|------|
| `type` | 必填 | 须通过 `event_type_is_valid()` |
| `param` | 选填 | 仅承载标量；不得传递指针或结构体地址 |
| `timestamp_ms` | 禁止填写 | 由 `event_publish()` 调用 `time_util_get_ms()` 写入 |

### 4.3 事件类别与 ID 契约

每类预留 `EVT_PER_CAT_MAX`（32）个槽位；新增事件只在对应类别下扩展 `local_id`，**不得**修改已有类别编号。

| 类别 | 枚举 | 典型发布方 | 已定义事件（节选） |
|------|------|------------|-------------------|
| 硬件异步 | `EVT_CAT_HW` | HAL 适配器 | `EVT_HW_ESTOP_ON/OFF`、`EVT_HW_IO_OFFLINE`、`EVT_HW_IO_ONLINE` |
| 组件完成 | `EVT_CAT_COMP` | 机构领域层 | `EVT_COMP_HOME_DONE`、`EVT_COMP_MOTION_COMPLETED` |
| 安全姿态 | `EVT_CAT_SAFETY` | `alarm_bridge`（边沿）；`safety_session_coordinator` 发完成事件 | `EVT_SAFETY_LOCKOUT`、`EVT_SAFETY_NOMINAL`、`EVT_ABORT_HOME_DONE` |
| 报警生命周期 | `EVT_CAT_ALARM` | `alarm_bridge` | `EVT_ALARM_TRIGGERED`、`EVT_ALARM_CLEARED`、`EVT_ALARM_RESYNC` |
| 外部命令 | `EVT_CAT_CMD` | （历史/测试） | `EVT_CMD_ORDER` 仅测试；网关唤醒已改 `cmd_control` 信号量 |
| 云端 | `EVT_CAT_CLOUD` | 云链路适配器 | `EVT_CLOUD_CONNECTED`、`EVT_CLOUD_DISCONNECTED`、`EVT_CLOUD_POINT_DIRTY` |
| 洗车流程 | `EVT_CAT_WASH` | 项目洗车编排器 | `EVT_WASH_DONE`、`EVT_WASH_ABORTED`、`EVT_WASH_SESSION_STARTED`、`EVT_WASH_CHECKPOINT_REACHED` |
| 运行模式 | `EVT_CAT_OP_MODE` | `operational_mode` 聚合 | `EVT_OP_MODE_CHANGED`、`EVT_ABORT_HOME_REQUESTED`、`EVT_OP_MODE_RECOVERY_*` 等 |

**明确不走总线的能力**（由注释固化，不得改为 event）：

- 限位信号等高频 IO 状态：由持有该机构的模块直接轮询，不逐次广播。
- 运动命令是否被接受：由 `motor_exec_run()` 等端口调用的同步返回值表达。
- 运动过程结局（含故障）：由 `motion_lifecycle_opts_t.on_motion_end` 回调传递 `motor_event_t`。

判据是「需不需要跨模块异步通知事实」。机构完成这类事实走总线（`EVT_COMP_MOTION_COMPLETED`），而每拍都在变的输入状态、以及需要立即拿到结果的调用不走。

### 4.4 运行统计

`event_bus_stats_t` 字段语义：

| 字段 | 含义 |
|------|------|
| `published_count` | 成功入队总数（高 + 普通） |
| `dispatched_count` | 成功出队并进入分发数 |
| `dropped_count` | 队列满丢弃数 |
| `subscribe_count` | 已登记订阅槽数（同一 handler 重复订阅幂等，不重复计数） |
| `queue_depth` / `queue_peak_depth` | 普通队列当前/峰值深度 |
| `hi_queue_depth` / `hi_queue_peak_depth` | 高优先级队列当前/峰值深度 |
| `sem_post_fail_count` / `sem_wait_fail_count` | 信号量异常计数 |

---

## 5. 核心模块行为契约

### 5.1 容量与配置

**总线容量**（`runtime/event_bus/event_bus_config.h`，由 `event_bus.c` 直接 include）：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `EVENT_BUS_QUEUE_SIZE` | 64 | 普通队列容量 |
| `EVENT_BUS_HI_QUEUE_SIZE` | 16 | 高优先级队列容量 |
| `EVENT_BUS_MAX_SUBS_PER_EVT` | 8 | 每种事件最多 handler 数 |

**dispatch 线程栈**（`runtime/config/thread_config.h`，完整框架 scheduler 使用）：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `THD_EVENT_DISPATCH_STACK` | 16 KiB | `event_dispatch` 线程栈 |

队列满时：`event_publish()` 返回 `SW_ERR_OVERFLOW`，事件**丢弃**，`dropped_count++`，并打 WARN 日志（高/普通队列分别告警，日志中带 type 与 param）。

### 5.2 优先级路由

| 路由目标 | 条件 |
|----------|------|
| 高优先级队列 | `event_type_category(type) == EVT_CAT_SAFETY`，或 `type` 为 `EVT_HW_ESTOP_ON` / `EVT_HW_ESTOP_OFF` / `EVT_HW_IO_ONLINE` / `EVT_HW_IO_OFFLINE` |
| 普通队列 | 其余合法事件 |

实现见 `event_bus.c` 中 `event_is_high_priority()`。

分发时**必须先排空高优先级队列**，再处理普通队列；同队列内 FIFO。

### 5.3 API 行为表

| API | 调用上下文 | 成功条件 | 失败返回 | 副作用 |
|-----|------------|----------|----------|--------|
| `event_bus_init()` | 启动期；dispatch 线程已停止 | 清空队列与订阅表；`sem_init` 成功 | `SW_ERR_HW` | `s_initialized = 1` |
| `event_bus_shutdown()` | 任意 | 已初始化 | `SW_ERR_NOT_INIT` / `SW_ERR_HW` | 置 shutdown 标志；`sem_post` 唤醒 dispatch 排空后退出 |
| `event_publish(type, param)` | 任意线程 | 已初始化；type 合法；队列未满；`sem_post` 成功 | `SW_ERR_NOT_INIT` / `SW_ERR_PARAM` / `SW_ERR_OVERFLOW` / `SW_ERR_HW` | 入队并填充 `timestamp_ms` |
| `event_subscribe(type, handler)` | 启动期（推荐） | 已初始化；type 合法；handler 非 NULL；有空槽 | `SW_ERR_NOT_INIT` / `SW_ERR_PARAM` / `SW_ERR_OVERFLOW` | 同 handler 重复订阅幂等返回 `SW_OK`，不增加 `subscribe_count` |
| `event_subscribe_table(subs, count)` | 启动期 | 表内每项订阅成功 | 遇首个失败即返回 | 批量注册 |
| `event_bus_get_stats(stats)` | 任意 | `stats != NULL` | `SW_ERR_PARAM` | 拷贝统计快照 |
| `event_bus_dispatch_loop()` | **仅** dispatch 线程 | — | `sem_wait` 致命失败时调用 fatal_cb 后返回 | 出队 → 快照 handler → 逐一回调 |
| `event_bus_drain()` | 测试 / 单线程推进；dispatch **未**运行 | 已初始化且 dispatch 未运行 | `SW_ERR_NOT_INIT` / `SW_ERR_STATE` | 循环出队直到两队列皆空；handler 再发布的事件一并排空 |
| `event_bus_set_fatal_cb(cb)` | `init` 之后、dispatch 启动之前 | — | — | 注册不可恢复故障回调 |

### 5.4 handler 回调契约

```c
typedef void (*event_handler_t)(const event_t *evt);
```

| 约束 | 说明 |
|------|------|
| 执行线程 | 生产路径仅 `event_dispatch` 线程；测试可通过 `event_bus_drain()` 在调用线程分发 |
| 参数生命周期 | `evt` 指向分发栈副本，回调返回后失效；不得保存指针 |
| 耗时 | 不得长时间阻塞；重活应投递到工作线程或周期任务 |
| 重入 | 不得调用 `event_publish` / `event_subscribe` |
| 并发写 | 若 handler 修改共享状态，须遵守模块自身线程约束 |

分发实现先将订阅表**快照**到局部数组再回调，避免持锁期间 subscribe 导致死锁。

### 5.5 shutdown 与 fatal

**正常 shutdown**：

1. `event_bus_shutdown()` 置 `s_shutdown_requested`，`s_initialized = 0`。
2. 后续 `event_publish` / `event_subscribe` 返回 `SW_ERR_NOT_INIT`。
3. dispatch 线程被唤醒后排空两队列，遇空队列且 shutdown 已请求则 `dispatch_loop` 返回。

**不可恢复故障**（`sem_wait` 非 `EINTR` 失败）：

- 调用 `event_bus_set_fatal_cb()` 注册的回调。
- 回调**必须**以 `abort()` 或 `_exit()` 终止进程（detach 线程无法 join，仅 return 会导致进程假活）。
- 回调内禁止调用 event_bus API 或依赖总线的业务逻辑。

---

## 6. 与运行模型的衔接

### 6.1 dispatch 线程与订阅方

当前工程在 `bootstrap_run()` 中注册名为 `event_dispatch` 的线程，入口调用 `event_bus_dispatch_loop()`。
单元测试优先 `event_bus_drain()` 在同一线程确定推进；需要验证异步 dispatch 的用例才启动该线程。

| 订阅方 | 典型订阅事件 | wdf 落地 |
|--------|--------------|----------|
| `op_mode_bridge` | `EVT_WASH_*`、`EVT_HW_ESTOP_*`、`EVT_SAFETY_LOCKOUT`、`EVT_ALARM_*` 等 | ✅ |
| `alarm_bridge` | （发布方）`EVT_ALARM_*`、`EVT_SAFETY_LOCKOUT/NOMINAL` | ✅ |
| `estop_poll` / `op_mode_bridge` | 安全与报警相关事件 | ✅ |
| `recovery_service` | 恢复流程事件 | ✅ |
| `safety_session_coordinator` | 安全切断与中止归位 | ✅ |
| `telemetry_projection` | `EVT_ALARM_*`、`EVT_WASH_*`、`EVT_OP_MODE_*` 等 | ✅ |

**`operational_mode`** 写路径来自 `cmd_control`（命令）与 `event_dispatch`（钩子），内部互斥串行；命令网关不再订阅总线唤醒。

### 6.2 典型发布链（概念级）

| 来源 | 发布事件 | `param` 含义（示例） |
|------|----------|----------------------|
| `alarm_bridge` | `EVT_ALARM_TRIGGERED` | 报警码 |
| `alarm_bridge` | `EVT_ALARM_RESYNC` | 丢弃的域事件条数（仅供诊断） |
| `alarm_bridge` | `EVT_SAFETY_LOCKOUT` | 0 |
| wash session | `EVT_WASH_DONE` | 结果码 |
| `snack_cloud_link_adapter` | `EVT_CLOUD_CONNECTED` | 0 |
| HAL / 信号滤波 | `EVT_HW_IO_OFFLINE` | 子板号等标量 |

以上为领域衔接示例；具体 `param` 编码由各领域模块文档规定，总线不解析。

### 6.3 命令网关与总线

外部命令经 `command_port` 注入后，由 `command_gateway` 在独立 **`cmd_control` 线程**仲裁执行。总线仍承载模式变更、`CMD_HANDLED` 等广播，但不再负责命令唤醒。

### 6.4 急停双通道

| 通道 | 机制 | 职责 |
|------|------|------|
| 快速通道 | 急停采集通路（`SCHED_FIFO`）同步切断 | 立即停止执行机构；更新运行模式/急停标志 |
| 普通通道 | `EVT_HW_ESTOP_ON` → 报警记录 → `EVT_ALARM_*` / 上报 | 记录、广播、云端与日志 |

允许极短窗口期：快速通道已生效时，普通通道可能尚未完成报警实例记录；`Recover` 合法性以 `operational_mode` 中的急停标志为准，该标志由快速通道经 `op_mode_bridge` 置位。

---

## 7. 完整接入示例

> **示例（完整框架，仅供参考）**

启动期批量订阅：

```c
static const event_subscription_t s_subs[] = {
    { EVT_ALARM_TRIGGERED, on_alarm_triggered },
    { EVT_SAFETY_LOCKOUT,  on_safety_lockout },
    { EVT_WASH_DONE,       on_wash_done },
};

sw_err_t bridge_init(void)
{
    return event_subscribe_table(s_subs, sizeof(s_subs) / sizeof(s_subs[0]));
}
```

发布方：

```c
(void)event_publish(EVT_ALARM_TRIGGERED, alarm_code);
```

单元测试中手动驱动分发（`wash-device-framework`）：

```c
/* 测试线程入口 */
event_bus_dispatch_loop();
```

生产环境不得依赖测试式手动 loop；须由 scheduler 注册专用 dispatch 线程。

---

## 8. 实现职责边界

### 8.1 固化在 event_bus 实现中

| 职责 | 说明 |
|------|------|
| 双队列 FIFO | 高优先级队列优先排空 |
| 入队时间戳 | 统一调用 `time_util_get_ms()` |
| 订阅幂等 | 同 type + 同 handler 重复注册返回 `SW_OK` |
| 队列满丢弃 | 返回 `SW_ERR_OVERFLOW`，不阻塞发布方 |
| handler 快照分发 | 避免 subscribe 死锁 |
| 统计计数 | `published` / `dispatched` / `dropped` 等 |

### 8.2 由项目 / 业务层决定

| 职责 | 决定方 |
|------|--------|
| 订阅哪些事件 | 各 `application` / `domain` 模块在 init 或 wiring 阶段 |
| `param` 编码含义 | 各领域模块（报警码、模式枚举等） |
| 队列容量与 handler 上限 | `event_bus_config.h`（可按机型调整宏） |
| dispatch 线程栈 | `thread_config.h`（完整框架 scheduler） |
| fatal 回调具体安全动作 | 项目在 `bootstrap` 注册（默认仅日志 + dispatch 退出） |
| 哪些能力走总线 vs 回调/轮询 | 各模块设计说明（见 §4.3 禁止项） |

实现**不得**在 `event_bus.c` 内硬编码机型分支或解析具体 `param` 业务语义。

---

## 9. 命名与分层约定

| 约定 | 规定 |
|------|------|
| 事件 ID 宏 | `EVT_<CAT>_<NAME>`，由 `EVT_MAKE(EVT_CAT_*, EVT_*_ID_*)` 生成 |
| 新增类别 | 在 `event_category_t` 末尾追加，不改动已有类别值 |
| 新增类内事件 | 使用该类未占用的 `local_id`；在 `event_types.h` 集中声明 |
| 头文件守卫 | 与路径一致：`common/event_types.h` → `COMMON_EVENT_TYPES_H`；`runtime/event_bus/event_bus.h` → `RUNTIME_EVENT_BUS_EVENT_BUS_H`（由 `check_arch_boundary.sh` R15 检查） |
| 日志 | 经 `EVT_LOG_WARN` / `EVT_LOG_ERROR` 宏转 `common/log.h`，不直接依赖外部 SDK |

---

## 10. 扩展指南

### 10.1 新增一种事件

1. 在 `common/event_types.h` 选择类别，分配 `EVT_<CAT>_ID_<NAME>`。
2. 定义 `EVT_<CAT>_<NAME>` 宏。
3. 在发布方模块调用 `event_publish()`。
4. 在消费方模块通过 `event_subscribe()` 或订阅表注册 handler。
5. 若事件属于安全关键路径，评估是否应归入 `EVT_CAT_SAFETY`，或在 `event_is_high_priority()` 中显式加入（如 ESTOP、IO 在线/离线边沿）。

### 10.2 新增事件类别

1. 在 `event_category_t` 追加枚举值（置于 `EVT_CAT_MAX` 之前）。
2. 无需修改 `event_bus.c` 路由逻辑，除非该类全部事件都应走高优先级队列（须在 `event_is_high_priority()` 显式规定）。
3. 更新本文档 §4.3 类别表。

### 10.3 调整容量

修改 `runtime/event_bus/event_bus_config.h` 中 `EVENT_BUS_*` 宏，重新编译 `event_bus.c`；无运行时配置接口。dispatch 线程栈在 `thread_config.h` 中独立调整。

### 10.4 禁止的扩展方式

- 在 `param` 中传递指针或变长数据。
- 在 handler 内同步等待其他线程完成业务（易造成 dispatch 饥饿）。
- 用 event_bus 替代 port 同步查询接口。
- 在 `domain` 多处重复订阅同一事件并各自维护矛盾状态——应设单一事实源再广播。

---

## 11. 当前落地状态（wash-device-framework）

| 能力 | 状态 |
|------|------|
| `event_bus` 核心实现 | ✅ 已迁入 |
| `event_bus_config.h` 容量配置 | ✅ 已从 `thread_config.h` 拆出 |
| `event_types.h` 类型契约 | ✅ 已迁入（HW 类当前仅 ESTOP + IO 在线/离线） |
| `runtime/scheduler/` | ✅ 已迁入（`test_scheduler` 覆盖） |
| 业务订阅（gateway / bridge / posture） | ✅ bootstrap + 单测 |
| Demo bootstrap + dispatch 线程 | ✅ `bootstrap_run()` |
| 单元测试 | ✅ `test_event_bus` 覆盖发布/订阅/优先级/shutdown，`test_event_bus_capacity` 按真机密度压测并断言余量；cloud/command/alarm 等模块单测亦覆盖订阅 |
| 单测未覆盖 | fatal 回调路径；高优先级路由只按 `EVT_CAT_SAFETY` 验证，`EVT_HW_ESTOP_*` / `EVT_HW_IO_*` 四个显式入列事件未单独断言 |

用例数与通过情况以 `scripts/check_all.sh` 生成的 `build-check/test-results/report.html` 为准，本文不记录动态结论（原则见 `tests/reports/README.md`）。

---

## 12. 相关文档

- `doc/module-design/ports-adapters/Storage端口与方案资产模块设计.md` — 运行期 KV 存储（与总线正交）
- `doc/module-design/domain/报警系统模块设计.md` — `EVT_ALARM_*` / `EVT_SAFETY_*` 发布与消费
- `doc/module-design/domain/命令网关模块设计.md` — `EVT_CMD_*` / `EVT_OP_MODE_*` / `op_mode_bridge`
- `tests/reports/event_bus.md` — 单元测试报告
- `doc/module-design/ports-adapters/CloudModel模块设计.md` — `EVT_CLOUD_*` 与上报调度
- `doc/module-design/domain/命令网关模块设计.md` — 命令唤醒、模式变更与回执事件

---

## 附录 A：结构自检对照

对照 `../README.md`「单篇文档建议结构」逐项自检。

| 检查项 | 状态 |
|--------|------|
| 分层图 + 依赖禁令 | §2 |
| 文件清单 | §3 |
| 数据模型 + API 行为表 | §4、§5 |
| 扩展指南 | §10 |
| 落地状态与测试覆盖 | §11 |
| 无函数级 walkthrough | 已遵守 |
| 无测试/构建命令 | 已遵守 |
| 示例已标注「仅供参考」 | §7 |
