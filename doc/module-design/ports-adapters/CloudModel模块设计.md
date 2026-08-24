# CloudModel 模块设计

**版本**：v2.0  
**状态**：已落地（物模型核心 + 单一链路端口 + 上报调度器 + Snack MQTT 适配器）  
**最后同步代码**：2026-08-24  
**适用范围**：`domain/cloud/`、`application/ports/outbound/cloud/link/`、`adapters/outbound/cloud/`、`application/orchestrators/report_scheduler.*`、`adapters/outbound/cloud/providers/snack/`  
**架构基线**：Ports & Adapters + 物模型点位表（复用 `point_table` 引擎）  
**关键词**：cloud_point、kind、on_change、属性下发、变更上报、COMMAND、`EVT_CLOUD_*`

---

## 1. 设计目标与核心理念

云端通信把**云平台物模型**与设备内部状态/命令解耦：上行序列化为 JSON 属性，下行 JSON 解析后按 kind 路由到遥测拒写、脉冲命令或写入。

公开概念只有：`kind`、`on_change`、`cloud_model`、`cloud_json`、`cloud_link`、`scheduler`。

### 1.1 设计目标

- **物模型表驱动**：项目层登记 `cloud_point_entry_t[]`，框架负责校验、编解码与分派。
- **命令不直连领域**：`CLOUD_KIND_COMMAND` 经提交回调转交 `device_command_port`。
- **变更驱动上报**：`on_change` 点位由 watcher 记脏集合，调度器每拍批量增量上报。
- **单一云端口**：MQTT/连接边沿、属性全量/增量、下行 recv 都在 `cloud_link_port`。

### 1.2 端到端数据流

```text
  云平台 MQTT 下行
        │
        ▼
  cloud_json_install 绑定的 recv
        │
        ▼
  cloud_point_apply_json() → cloud_point_apply_value()
        │
        ├── TELEMETRY 拒写
        ├── COMMAND  → device_command_port.submit_async()
        └── WRITE    → base.set()

  上行：
  watcher poll → 脏集合 → publish_properties_delta
  重连 / 周期  → publish_properties
```

---

## 2. 分层归属与依赖方向

| 层次 | 路径 | 职责 |
|------|------|------|
| 物模型核心 | `domain/cloud/` | kind、校验、分派、watcher 脏集合 |
| JSON | `adapters/outbound/cloud/cloud_point_json.*`、`cloud_json.*` | 编解码、`cloud_json_install` |
| 链路端口 | `application/ports/outbound/cloud/link/cloud_link_port.h` | 传输、属性上报、recv |
| 调度 | `application/orchestrators/report_scheduler.*` | 单一周期任务 |
| provider | `adapters/outbound/cloud/providers/snack/` | Snack MQTT |

**依赖禁令**：`domain/cloud/` 不得解析 JSON、不得依赖 MQTT SDK 或 `application/`。COMMAND 必须经提交回调。

---

## 3. 文件清单

| 文件 | 职责 |
|------|------|
| `domain/cloud/cloud_point.h` | kind、条目、validate / apply_value |
| `domain/cloud/cloud_point_validate.c` | 登记期校验 |
| `domain/cloud/cloud_point_dispatch.c` | 按 kind 分派 |
| `domain/cloud/cloud_point_watcher.*` | shadow 与脏集合 |
| `domain/cloud/cloud_model.*` | 注册（含校验）、查询 |
| `adapters/outbound/cloud/cloud_json.*` | 安装下行、构建属性 JSON |
| `adapters/outbound/cloud/cloud_point_json.*` | 点位表 JSON 编解码 |
| `runtime/ports/port_registry_cloud.c` | `cloud_link_register` |
| `tests/cloud/test_cloud_point_validate.c` | 校验 |
| `tests/cloud/test_cloud_point_dispatch.c` | 分派 / JSON |
| `tests/cloud/test_cloud_point_watcher.c` | 脏集合 |
| `tests/cloud/test_cloud_model_report_scheduler.c` | 模型 + 调度 |

---

## 4. 数据模型

```c
typedef enum {
    CLOUD_KIND_TELEMETRY = 0,
    CLOUD_KIND_COMMAND,
    CLOUD_KIND_WRITE,
} cloud_point_kind_t;

typedef struct {
    point_table_entry_t base;
    cloud_point_kind_t  kind;
    bool                on_change;
    dev_cmd_kind_t      cmd_kind;
} cloud_point_entry_t;
```

| kind | 下行 | 约束 |
|------|------|------|
| TELEMETRY | 拒写 | 必须有 get，不得有 set |
| COMMAND | `val.b == true` 时提交 `cmd_kind` | 必须是 bool，`cmd_kind ≠ NONE` |
| WRITE | 调用 `base.set` | 必须有 set |

`on_change`：必须有 get，禁止 `POINT_TYPE_FLOAT`。

容量：`CLOUD_POINT_TABLE_MAX` 96；`CLOUD_REPORT_JSON_MAX` 4096。

云事件：`EVT_CLOUD_CONNECTED` / `EVT_CLOUD_DISCONNECTED`（link 适配器 poll 边沿）。脏点不走事件。

---

## 5. 核心模块行为契约

### 5.1 `cloud_point_validate` / `cloud_model_register`

`cloud_model_register()` 先校验再登记，失败不留下半份注册。空表、超 96、id 重复、kind 约束失败均返回 `SW_ERR_PARAM`。

### 5.2 `cloud_point_apply_json`（下行）

根必须是 JSON 对象；载荷长于 `CLOUD_REPORT_JSON_MAX` 返回 `SW_ERR_OVERFLOW`。未知 id 继续下一 key（部分成功）。函数在解析成功时返回 `SW_OK`，逐 key 成败看 `point_apply_result_t`。

COMMAND：`false` 空操作；`true` 调提交回调，未注册返回 `SW_ERR_NOT_INIT`。

### 5.3 `cloud_point_watcher`

`poll` 比对 shadow 后置脏。`take_dirty` 交出点位 id 并清除标记；上报失败由调度器 `restore_dirty`。不发布事件、不发 MQTT。

### 5.4 `cloud_link_ops_t`

`init` / `is_online` / `poll` / `publish` / `publish_properties` / `publish_properties_delta` / `set_recv_handler`。字段可选，调用方逐个判空。

`PORT_REQ_CLOUD_LINK` 检查 `is_online`、`publish_properties`、`set_recv_handler` 均已填。属性上报与下行不再是独立端口位。

---

## 6. 与命令网关的衔接

COMMAND 不在 cloud 层构造完整 `dev_cmd_t`，只传 `dev_cmd_kind_t`。项目 wiring：

```c
static sw_err_t cloud_cmd_submit(dev_cmd_kind_t kind)
{
    dev_cmd_t cmd = dev_cmd_make_simple(kind);
    uint64_t  request_id;

    cmd.meta.source = DEV_CMD_SOURCE_CLOUD;
    return device_command_port_get_ops()->submit_async(&cmd, &request_id);
}
```

`cloud_json_install(cloud_cmd_submit, snack_cloud_property_reply)` 同时登记该回调。

---

## 7. 完整接入示例

> **示例（完整框架 wiring，仅供参考）**

```c
void wiring_cloud(void)
{
    static const cloud_point_entry_t s_model[] = { /* 项目物模型表 */ };
    snack_cloud_adapter_register();
    snack_cloud_adapter_configure(pk, sn, secret, topic_up, topic_reply);
    cloud_model_register(s_model, ARRAY_SIZE(s_model));
    cloud_json_install(cloud_cmd_submit, snack_cloud_property_reply);
}

/* project_register_runtime_tasks */
report_scheduler_start(500U, 30000U);
```

`full_period_ms == 0` 表示只在重连时全量。`report_scheduler_start` 初始化 watcher，并订阅 `EVT_CLOUD_CONNECTED` 做全量重同步。

---

## 8. 实现职责边界

固化在框架：kind 分派、JSON 编解码、登记期校验、on_change 脏集合、单一 poll 写者。

由项目 / 适配器决定：物模型表、MQTT topic / 凭证、`poll_ms` / `full_period_ms`、COMMAND 到 `dev_cmd_t` 的填充。

---

## 9. 测试覆盖

| 测试 | 覆盖点 |
|------|--------|
| `test_cloud_point_validate` | id 唯一、kind 约束、on_change 禁止 FLOAT |
| `test_cloud_point_dispatch` | 分派、遥测拒写、COMMAND 脉冲、非对象根、部分成功 |
| `test_cloud_point_watcher` | 脏集合与 restore |
| `test_cloud_model_report_scheduler` | 属性构建、重连全量、脏点增量 |
| `test_cloud_ports` | link 注册 / NULL 解除 / 复位 |
| `test_snack_cloud_adapters` | Snack MQTT（`WDF_TEST_VENDOR_PROVIDERS=ON`） |

---

## 10. 扩展指南

新增属性：在项目表追加条目，选 kind 与 `on_change`，保证 `cloud_model_register` 能通过。

新增 COMMAND 种类：先扩 `dev_cmd_kind_t` 与命令矩阵，再在物模型表加 bool 点位。

禁止：在 dispatch 里写机型分支；COMMAND 绕过 `device_command_port`；在 watcher 里发 MQTT。

---

## 11. 当前落地状态

| 能力 | 状态 |
|------|------|
| 物模型 kind / validate / dispatch / watcher | ✅ |
| 单一 `cloud_link_port` | ✅ |
| `cloud_json_install` | ✅ |
| `report_scheduler_start` | ✅ |
| Snack MQTT 适配器 | ✅ 可选 `WDF_ENABLE_SNACK_CLOUD_PROVIDER` |
| Demo 完整物模型表 | 项目侧职责 |

---

## 12. 相关文档

- `doc/module-design/domain/命令网关模块设计.md`
- `doc/module-design/runtime/EventBus模块设计.md`
- `common/point_table/point_table.h`
