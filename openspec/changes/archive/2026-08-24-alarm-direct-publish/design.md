## Context

报警事实源在 `alarm_registry`，命令路径已经同步读它。观察与停机却走另一条链：pending → 50ms drain →（LOCKOUT 时 binding 再 drain 一次）→ `EVT_ALARM_*` / `EVT_SAFETY_*`。这是为了遵守报警模块自己写的「registry 不准碰 event_bus」，比分层总表更严。总表允许 domain 依赖 `runtime/event_bus`；`operational_mode`、`wash_events`、`actuator_events` 都是放锁后发布。

ALRM-22 的 D1 曾否决「domain 回调发布」，理由有两条：纯度，以及 MINOR 抖动在采集线程抢总线锁。前者与仓惯例冲突；后者里 `event_publish` 本就是非阻塞入队，LOCKOUT 路径今天已经在采集线程上拿这把锁。为 MINOR 单独保留 pending / RESYNC / drain 互斥，理解成本高于收益。

本设计取消该特例，不新造出站 notify 端口（优化方向已把「domain event 再包一层」标为低性价比）。

约束：domain 仍不得依赖 `application/`；入站端口仍由 application 注册；采集线程不得 `abort_wash` / cutout；开发期无兼容包袱。

## Goals / Non-Goals

**Goals:**

- 活动表变位在 `alarm_registry_*` 返回前完成总线入队（含 MINOR 与 NOMINAL 边沿）。
- 删除 pending、drain、`EVT_ALARM_RESYNC` 与 binding 上的立即 drain。
- 安全读接口一次持锁（含 journal）；运行模式对报警只收敛一次。
- 应用桥只做端口注册、journal 窗口、可选 ON_MOTION。

**Non-Goals:**

- 不改三等级、三清除、`safety_matrix`、lockout 驱逐策略、容量数字。
- 不把 `safety_cutout_execute` / `safety_output_hold` 并进 trigger。
- 不让 detector 直接 include registry；不删除 `alarm_binding_port`。
- 不把 journal 用于洗后 STOPPED。
- 不预留 lockout 空槽、不加大活动池来「代替」驱逐。
- 不在采集线程同步停机构。

## Decisions

### D1. registry 直接发布，不加 notify 端口

变位函数持锁修改，把本轮码与姿态边沿记到栈上数组（容量 `ALARM_ACTIVE_MAX`），放锁后再 `event_publish_required`。先逐码、后姿态边沿，顺序与今日 drain 相同。

**备选：**

- 保留 pending + 只把立即 drain 扩到所有等级：管道还在，理解问题几乎不减。
- `domain/ports/outbound` 增加 notify：比现状多一层，且 D1 已否决「domain 回调」；`safety/` 出站树当前约定 domain 不 include。
- 观察面纯 pull、只留 `EVT_SAFETY_LOCKOUT`：op_mode 的 IDLE→STOPPED 与观测桥要改订阅面，范围大于「对齐 op_mode 发布惯例」。

### D2. 所有变位同步入队，不分 MINOR/CRITICAL

不再用 50ms 任务给 MINOR 做批处理。发布失败遵循 `event_publish_required` 既有契约，不在报警域做第二套丢件/RESYNC。遥测 1s 全量对账与总线 dropped 统计保留。

**备选：** 只对 LOCKOUT 同步发布、其余仍 pending——双路径正是要删的。

### D3. 姿态边沿从桥挪回 registry

拥有活动表的聚合根发布 `EVT_SAFETY_*`，与 op_mode 发布 `EVT_OP_MODE_CHANGED` 对称。桥不再保存 `s_posture`，ALRM-19 的 drain 互斥随 drain 一起删除。边沿唯一性由「持锁算出 next≠prev，放锁后发一次」保证；并发 trigger 仍由 registry 互斥串行化变异。

**备选：** 桥继续订 `EVT_ALARM_*` 再读姿态——多一次跳转，且 RESYNC 删掉后桥更容易漏边沿。

### D4. binding 并入 `alarm_bridge`，只转发

`alarm_binding_bridge_bind` 的逻辑迁入 `alarm_bridge`（或 `alarm_bridge_bind`）。ops 指针直接转到 `alarm_registry_trigger/clear/load_catalog`。bootstrap 仍先 bind 端口再 `project_bind_alarm_catalog`。`PORT_REQ_ALARM_BINDING` 不变。

### D5. 安全视图结构体，journal 同锁

用 `alarm_safety_view_t` 替换五参数 `copy_safety_view`。`has_blocking_active` / `safety_posture` 保留为薄封装。投影一次调用填满 `safety_snapshot_t` 的报警相关字段。

**备选：** 只改封装、journal 仍第二次取锁——D3（旧）已承认撕裂，本变更正好收掉。

### D6. 运行模式单一 blocking 入口

删除与 `op_mode_on_blocking_alarm` 同体的 `op_mode_on_critical_alarm`。`EVT_SAFETY_LOCKOUT` 只留给 coordinator 的 `abort_wash`。CRITICAL ⊂ blocking，LOCKOUT 事件不再承担第二次模式切换。

## Risks / Trade-offs

- [采集线程上 MINOR 抖动更频繁地抢 event_bus 锁] → 入队非阻塞、持锁时间有界；与今日 LOCKOUT drain 同一竞争面。不把发布登记为 RT 切断路径。
- [总线满导致漏通知] → 活动表仍是事实源；开洗/洗后仍同步读；投影 1s 重建。不再用 `EVT_ALARM_RESYNC` 假装能告诉边沿订阅者「漏了哪几条」。
- [测试同步习惯依赖 `alarm_bridge_drain`] → 单测在 trigger 后改用 `event_bus_drain()`（既有 EBUS-13）再断言订阅方。
- [demo 手调 drain] → 删除调用；trigger 返回即已入队。
- [domain 开始 include event_bus] → 与 op_mode 一致，门禁已允许；禁止反向 include application。

## Migration Plan

开发期直接替换：先改 registry 发布与删除 pending，再拆 drain / 合桥 / 改 view / 合 op_mode 入口，最后改测试与 ALRM 契约、模块设计、`gen_doc_index.py`、`check_all.sh`。

回滚即还原本变更。无运行期双路径、无数据迁移。

项目侧：detector 仍只调 `alarm_binding_port`；若有代码在 trigger 后等 50ms 才认为事件已入队，改为返回后即可（或 `event_bus_drain` 用于测试）。ON_MOTION 绑定表 API 不变。

## Open Questions

无。容量与驱逐策略不在本变更重开。
