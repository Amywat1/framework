# CloudModel 模块设计

**版本**：v1.0  
**状态**：已落地（物模型核心 + 端口契约 + 上报调度器 + Snack MQTT 适配器）  
**最后同步代码**：2026-07-14（`cloud_point_*`、`cloud_model`、`report_scheduler`、Snack cloud provider）  
**适用范围**：`cloud/`、`ports/**/cloud/`、`application/orchestrators/report_scheduler.*`、`adapters/**/cloud/providers/snack/`  
**架构基线**：Ports & Adapters（六边形）+ 物模型点位表（复用 `point_table` 引擎）  
**关键词**：cloud_point、物模型、属性下发、变更上报、DEVICE_CMD、`EVT_CLOUD_*`

---

## 1. 设计目标与核心理念

云端通信模块负责把**云平台物模型**与设备内部状态/命令解耦：上行序列化为 JSON 属性，下行 JSON 解析后按语义路由到读/写/服务/标准设备命令。

### 1.1 设计目标

- **物模型表驱动**：项目层登记 `cloud_point_entry_t[]`，框架负责校验、编解码与语义 dispatch。
- **语义分流**：同一 JSON 键根据 `semantic` 走遥测只读、脉冲命令、云端服务或手动点动四条路径。
- **命令不直连领域**：`DEVICE_CMD` 语义经 `cloud_point_set_device_cmd_submit` 回调转交 `device_command_port`，与 CLI 等来源共用命令网关。
- **变更驱动上报**：`ON_CHANGE` 策略点位由 watcher 周期 poll，变更时发布 `EVT_CLOUD_POINT_DIRTY`，供上报调度器增量上报。
- **传输与模型分离**：MQTT/连接边沿经 `cloud_link_port`；属性 JSON 应用经 `cloud_property_port`；上报触发经 `cloud_report_port`。

### 1.2 核心理念

| 原则 | 规定 |
|------|------|
| 复用 point_table | `cloud_point_entry_t.base` 即 `point_table_entry_t`；JSON 解析/序列化委托通用引擎 |
| 分阶段生命周期 | `cloud_model_register()` 只登记 bundle，`cloud_model_validate()` 做静态校验，`cloud_model_init()` 初始化 watcher，`cloud_model_register_scheduler()` 注册上报任务 |
| 脉冲命令模型 | `DEVICE_CMD` 通常为 `bool` WO 点位；`true` 触发，`false` 忽略；`get` 用 `cloud_point_get_echo_idle` 回显空闲 |
| 与命令网关正交 | cloud 层只识别 `dev_cmd_kind_t`；裁决与副作用在 `command_gateway` / `operational_mode` |
| 事件通知边沿 | 连接上下线、点位脏标记走 `event_bus`；poll 线程不直接调用 MQTT |

### 1.3 端到端数据流（概念）

```text
                    ┌─────────────────────────────────────────┐
  云平台 MQTT 下行   │  adapters（可选 provider / 项目 wiring）  │
        │           │  recv → cloud_property.on_property_set  │
        ▼           └──────────────────┬──────────────────────┘
  JSON payload                         │
                                       ▼
                          cloud_point_apply_json()
                                       │
              ┌────────────────────────┼────────────────────────┐
              ▼                        ▼                        ▼
        TELEMETRY(拒写)          DEVICE_CMD              MANUAL_ACT / CLOUD_SERVICE
              │                        │                        │
              │              cloud_device_cmd_submit           base.set / service()
              │                        │                        │
              │                        ▼                        │
              │           device_command_port.submit()          │
              │              （command_gateway 仲裁）            │
              ▼                        ▼                        ▼
        point_apply_result      dev_cmd_receipt            领域/适配器回调

  上行：
  cloud_point_to_json() / cloud_point_to_json_filtered()
        │
        ▼
  cloud_report_port.publish_properties[_delta]()  ← report_scheduler
        │
        ▼
  cloud_link_port.publish(topic, payload)
```

---

## 2. 分层归属与依赖方向

### 2.1 模块落位

| 层次 | 路径 | 职责 |
|------|------|------|
| **物模型核心** | `cloud/cloud_point.h` | 点位元模型、`cloud_point_entry_t`、对外 API 声明 |
| **登记期校验** | `cloud/cloud_point_validate.c` | id 唯一性、access/semantic/get/set/cmd_kind 一致性 |
| **下行 dispatch** | `cloud/cloud_point_dispatch.c` | JSON 应用、上行序列化、`DEVICE_CMD` 提交回调 |
| **变更检测** | `cloud/cloud_point_watcher.{h,c}` | `ON_CHANGE` shadow 比对，发布 `EVT_CLOUD_POINT_DIRTY` |
| **通用引擎** | `common/point_table/` | id 查找、JSON 解析/序列化、apply 结果汇总 |
| **入站端口** | `ports/inbound/cloud/property/property_port.h` | 属性下发 / 应答 |
| **出站端口** | `ports/outbound/cloud/link/cloud_link_port.h` | 传输 init/online/publish/recv/poll |
| **出站端口** | `ports/outbound/cloud/report/report_port.h` | 全量/增量属性上报触发 |
| **端口注册** | `ports/port_registry.c` | `cloud_*_register()` / `get_ops()` |
| **物模型注册** | `cloud/cloud_model.{h,c}` | 物模型 bundle 注册、property_port 安装、JSON 构建入口 |
| **上报调度** | `application/orchestrators/report_scheduler.{h,c}` | 周期/事件策略驱动全量或增量上报 |
| **命令词汇** | `domain/command_gateway/device_command.h` | `dev_cmd_kind_t`（DEVICE_CMD 映射目标） |
| **事件契约** | `common/event_types.h` | `EVT_CLOUD_*` |
| **项目 wiring/hooks** | `projects/*/wiring.c`、`project_hooks.c` | 物模型表注册、validate、watcher init、上报任务注册、port 适配器注册 |

### 2.2 依赖方向

```text
adapters / report_scheduler / cloud_model
        │ cloud_*_port
        ▼
   cloud/cloud_point_*
        │ point_table、device_command（仅 kind 枚举）
        ▼
   common/point_table、common/event_types
```

**依赖禁令**：

- `cloud/` 不得依赖 MQTT SDK、cJSON 以外的云平台 SDK，或 `application/` 业务模块。
- `cloud_point` 不得直接调用 `operational_mode` / `wash_orchestrator`；`DEVICE_CMD` 必须经注册的 submit 回调。
- 端口头文件不得包含适配器实现；适配器在 `adapters/` 注册 ops。

---

## 3. 文件清单

| 文件 | 职责 |
|------|------|
| `cloud/cloud_point.h` | 枚举、结构体、validate/to_json/apply_json API |
| `cloud/cloud_point_validate.c` | 登记期表项校验 |
| `cloud/cloud_point_dispatch.c` | dispatch_set、JSON 编解码、device_cmd 回调注册 |
| `cloud/cloud_point_watcher.h` | watcher init/poll 接口 |
| `cloud/cloud_point_watcher.c` | shadow 状态、变更发布 |
| `ports/inbound/cloud/property/property_port.h` | 属性下发 port |
| `ports/outbound/cloud/link/cloud_link_port.h` | 链路 port |
| `ports/outbound/cloud/report/report_port.h` | 上报 port |
| `ports/port_registry.c` | 三端 cloud port + command port 注册器 |
| `tests/cloud/test_cloud_point_validate.c` | 校验单元测试 |
| `tests/cloud/test_cloud_point_dispatch.c` | dispatch / JSON 单元测试 |
| `tests/cloud/test_cloud_point_watcher.c` | ON_CHANGE + event 单元测试 |
| `tests/ports/test_cloud_ports.c` | port register/get 单元测试 |

---

## 4. 数据模型

### 4.1 云端点位条目

```c
typedef struct {
    point_table_entry_t    base;          /* id / type / get / set */
    cloud_point_access_t   access;        /* RO / RW / WO */
    cloud_point_semantic_t semantic;      /* 语义分流 */
    cloud_report_policy_t  report_policy; /* 上报策略 */
    dev_cmd_kind_t         cmd_kind;      /* DEVICE_CMD 专用 */
    cloud_service_fn_t     service;       /* CLOUD_SERVICE 专用 */
} cloud_point_entry_t;
```

### 4.2 访问权限（access）

| 枚举 | 含义 | get/set 约束 |
|------|------|--------------|
| `CLOUD_POINT_ACCESS_RO` | 只读遥测 | 必须有 `get`；不得有 `set` |
| `CLOUD_POINT_ACCESS_RW` | 读写 | 按 semantic 决定 |
| `CLOUD_POINT_ACCESS_WO` | 只写 | 通常无 `get`（脉冲命令除外，用 echo_idle） |

### 4.3 语义类型（semantic）

| 枚举 | 下行行为 | 典型用途 |
|------|----------|----------|
| `CLOUD_POINT_SEM_TELEMETRY` | 拒绝 set（只读） | 速度、状态、计数 |
| `CLOUD_POINT_SEM_DEVICE_CMD` | `val.b == true` 时提交 `cmd_kind` | 启动/停止洗车、复位等标准命令 |
| `CLOUD_POINT_SEM_CLOUD_SERVICE` | 调用 `service(val)` | 云平台专有服务（参数下发、OTA 触发等） |
| `CLOUD_POINT_SEM_MANUAL_ACT` | 调用 `base.set(val)` | 手动点动、调试写线圈 |

### 4.4 上报策略（report_policy）

| 枚举 | 含义 |
|------|------|
| `CLOUD_REPORT_PERIODIC` | 周期全量/批次上报（由 report_scheduler 驱动） |
| `CLOUD_REPORT_ON_CHANGE` | watcher poll 检测变更 → `EVT_CLOUD_POINT_DIRTY` |
| `CLOUD_REPORT_RESYNC_ONLY` | 仅重连后全量同步 |
| `CLOUD_REPORT_NEVER` | 不上报（本地调试或内部点位） |

### 4.5 容量常量

| 宏 | 值 | 说明 |
|----|-----|------|
| `CLOUD_POINT_TABLE_MAX` | 96 | 单次 JSON 序列化栈缓冲上限 |
| `CLOUD_REPORT_JSON_MAX` | 4096 | 上报 JSON 缓冲建议上限（项目层使用） |

### 4.6 云端相关事件

| 事件 | 发布方 | `param` 含义 |
|------|--------|--------------|
| `EVT_CLOUD_CONNECTED` | link 适配器 `poll()` 检测上升沿 | 0 |
| `EVT_CLOUD_DISCONNECTED` | link 适配器 `poll()` 检测下降沿 | 0 |
| `EVT_CLOUD_POINT_DIRTY` | `cloud_point_watcher_poll()` | 点位表索引 `i` |

---

## 5. 核心模块行为契约

### 5.1 `cloud_point_validate`

| 检查项 | 失败条件 |
|--------|----------|
| 表非空 | `entries == NULL` 或 `count == 0` |
| id 唯一 | 重复 id |
| id 非空 | 空字符串 |
| RO | 缺 `get` 或存在 `set` |
| TELEMETRY | 缺 `get` |
| DEVICE_CMD | `cmd_kind == DEV_CMD_NONE` |
| CLOUD_SERVICE | `service == NULL` |
| MANUAL_ACT | `set == NULL` |

校验失败返回 `SW_ERR_PARAM` 并打 `LOG_ERROR`；项目应在 `project_validate()` 中调用 `cloud_model_validate()`，失败则中止启动。

### 5.2 `cloud_point_apply_json`（下行）

1. 解析 JSON 对象为 key-value 迭代。
2. 按 id 在表中查找（条目 stride 为 `sizeof(cloud_point_entry_t)`）。
3. 未知 id → `rejected++`，记录错误，**继续**下一 key（部分成功模型）。
4. RO 或 TELEMETRY → 拒写，`SW_ERR_STATE`。
5. 类型解析失败 → `rejected++`。
6. 调用 `dispatch_set()` 按 semantic 路由。
7. 函数总返回值恒为 `SW_OK`（除非入参非法或 JSON 解析失败）；逐 key 成败看 `point_apply_result_t`。

**DEVICE_CMD dispatch 规则**：

- `val.b == false` → 直接 `SW_OK`（脉冲空闲，不提交命令）。
- `val.b == true` → 调用 `s_device_cmd_submit(cmd_kind)`；未注册回调返回 `SW_ERR_NOT_INIT`。

### 5.3 `cloud_point_to_json` / `cloud_point_to_json_filtered`（上行）

- 收集所有 `get != NULL` 的 base 条目，委托 `point_table_to_json_ex` / `point_table_to_json_filtered`。
- `cloud_point_set_get_fail_policy()` 控制某个 get 失败时是 omit / abort / null。

### 5.4 `cloud_point_watcher`

| API | 行为 |
|-----|------|
| `cloud_point_watcher_init(entries, count)` | 保存表指针；对 `ON_CHANGE` 且可读点位建立初始 shadow |
| `cloud_point_watcher_poll()` | 周期调用；值变化或首次有效读 → `event_publish(EVT_CLOUD_POINT_DIRTY, index)` |

watcher **不**直接上报 MQTT；消费方（report_scheduler）订阅 `EVT_CLOUD_POINT_DIRTY` 后调用 `cloud_report_get_ops()->publish_properties_delta()`。

### 5.5 端口 API 摘要

**cloud_property_ops_t**

| 方法 | 职责 |
|------|------|
| `on_property_set(json, result)` | 适配器入口：通常转调 `cloud_point_apply_json` |
| `reply_property_set(request_json, result)` | 可选：向云端回复 apply 结果 |

**cloud_link_ops_t**

| 方法 | 职责 |
|------|------|
| `init()` | 读取 deploy 凭证，建立 MQTT 等 |
| `is_online()` | 当前链路是否可用 |
| `poll()` | 检测连接边沿 → 发布 `EVT_CLOUD_*` |
| `publish(topic, payload)` | 发送上行消息 |
| `set_recv_handler(cb)` | 注册下行 JSON 回调 |

**cloud_report_ops_t**

| 方法 | 职责 |
|------|------|
| `publish_properties()` | 全量属性上报 |
| `publish_properties_delta(ids, count)` | 指定 id 增量上报 |

---

## 6. 与命令网关的衔接

云端 `DEVICE_CMD` 语义**不**在 cloud 层构造完整 `dev_cmd_t`，只传递 `dev_cmd_kind_t`。项目 wiring 典型接法：

```text
cloud_point_set_device_cmd_submit(my_submit);

my_submit(kind):
    dev_cmd_t cmd = dev_cmd_make_simple(kind);
    cmd.meta.source = DEV_CMD_SOURCE_CLOUD;
    return device_command_port_get_ops()->submit(&cmd, &receipt, timeout_ms);
```

这样云端脉冲命令与 CLI、仿真测试共用同一套 `command_gateway` → `operational_mode` → `side_effect_router` 流水线。详见 `doc/module-design/domain/命令网关模块设计.md`。

---

## 7. 完整接入示例

> **示例（完整框架 wiring，仅供参考）**

```c
static sw_err_t cloud_cmd_submit(dev_cmd_kind_t kind)
{
    dev_cmd_t         cmd = dev_cmd_make_simple(kind);
    dev_cmd_receipt_t receipt;

    cmd.meta.source = DEV_CMD_SOURCE_CLOUD;
    return device_command_port_get_ops()->submit(&cmd, &receipt, 5000U);
}

void wiring_cloud(void)
{
    static const cloud_point_entry_t s_model[] = { /* 项目物模型表 */ };
    static const report_policy_entry_t s_policies[] = { /* 项目上报策略 */ };
    static const cloud_model_bundle_t s_bundle = {
        .entries = s_model,
        .count = ARRAY_SIZE(s_model),
        .report_policies = s_policies,
        .policy_count = ARRAY_SIZE(s_policies),
        .property_reply = NULL,
    };

    cloud_model_register(&s_bundle);
    cloud_point_set_device_cmd_submit(cloud_cmd_submit);
    /* cloud_link_register / cloud_property_register / cloud_report_register */
}
```

生命周期 hook：

```text
project_validate()
    └─ cloud_model_validate()

project_init_adapters()
    └─ cloud_model_init()

project_register_runtime_tasks()
    └─ cloud_model_register_scheduler()
```

---

## 8. 实现职责边界

### 8.1 固化在 cloud 核心中

| 职责 | 说明 |
|------|------|
| 语义 dispatch | TELEMETRY 拒写、DEVICE_CMD 脉冲、service/manual 分流 |
| JSON 编解码 | 基于 point_table + cJSON |
| 登记期 validate | 表项静态约束 |
| ON_CHANGE shadow | 变更检测与 `EVT_CLOUD_POINT_DIRTY` |

### 8.2 由项目 / 适配器决定

| 职责 | 决定方 |
|------|--------|
| 物模型表内容 | 项目 wiring 传入 `cloud_model_bundle_t` |
| MQTT Topic / 凭证 | deploy_store + link 适配器 |
| 上报时机与节流 | report_scheduler policy，由 `cloud_model_register_scheduler()` 注册 |
| DEVICE_CMD → dev_cmd_t 填充 | wiring 中 submit 回调（载荷、request_id） |
| 连接边沿检测细节 | link 适配器 `poll()` |

---

## 9. 扩展指南

### 9.1 新增物模型属性

1. 在项目物模型表追加 `cloud_point_entry_t`。
2. 选择 `access`、`semantic`、`report_policy`。
3. 实现 `get`/`set`/`service`/`cmd_kind` 之一。
4. 确保 `project_validate()` 调用 `cloud_model_validate()`。
5. 若 `ON_CHANGE`，确保 `project_init_adapters()` 调用 `cloud_model_init()`，并在 `project_register_runtime_tasks()` 调用 `cloud_model_register_scheduler()`。

### 9.2 新增 DEVICE_CMD 种类

1. 在 `device_command.h` 扩展 `dev_cmd_kind_t`（domain 闭集）。
2. 在 `operational_mode` 命令矩阵与 `side_effect_router` 中登记（见命令网关文档）。
3. 物模型表新增 WO bool 点位，`semantic = DEVICE_CMD`，`cmd_kind` 指向新枚举。

### 9.3 禁止的扩展方式

- 在 `cloud_point_dispatch.c` 内硬编码机型分支或云平台字段名。
- `DEVICE_CMD` 绕过 `device_command_port` 直接调 orchestrator。
- 在 watcher 内同步发送 MQTT（阻塞 poll 线程）。
- 在 `param` 未定义的 event 载荷中传递 JSON 指针。

---

## 10. 当前落地状态（wash-device-framework）

| 能力 | 状态 |
|------|------|
| `cloud_point` 核心（validate/dispatch/watcher） | ✅ 已迁入 |
| cloud 三端 port 契约 + port_registry | ✅ 已迁入 |
| `cloud_model` | ✅ 已迁入 |
| `report_scheduler` | ✅ 已迁入 |
| 单元测试 | ✅ validate(9) / dispatch(8) / watcher(3) / ports(4) / model+scheduler(2) |
| Snack MQTT link 适配器 | ✅ 已迁入，`WDF_ENABLE_SNACK_CLOUD_PROVIDER` 可选启用 |
| Snack cloud_property 适配器 | ✅ 已迁入，负责下行属性转 `cloud_model_apply_property_set()` |
| Demo wiring / 完整物模型表 | 项目侧职责，当前 Demo 未接入完整云物模型 |
| 与 command_gateway 联调 | 测试层分别覆盖；端到端 DEVICE_CMD 由项目 wiring 连接 |

---

## 11. 相关文档

- `doc/module-design/domain/命令网关模块设计.md` — `device_command_port` 与 `DEVICE_CMD` 衔接
- `doc/module-design/domain/报警系统模块设计.md` — 与命令/安全域正交；报警态可通过 `safety_snapshot` 读取
- `doc/module-design/runtime/EventBus模块设计.md` — `EVT_CLOUD_*` 分发语义
- `doc/module-design/ports-adapters/Storage端口与方案资产模块设计.md` — deploy_store（MQTT 凭证等）
- `common/point_table/point_table.h` — 通用点位引擎 API

---

## 附录 A：自检对照（module-design-spec B 类）

| 检查项 | 状态 |
|--------|------|
| 分层图 + 依赖禁令 | §2 |
| 文件清单 | §3 |
| 数据模型 + API 行为 | §4、§5 |
| 与命令网关衔接 | §6 |
| 扩展指南 | §9 |
| 落地状态 | §10 |
| 无函数级 walkthrough | 已遵守 |
| 无测试/构建命令 | 已遵守 |
| 示例已标注「仅供参考」 | §7 |
