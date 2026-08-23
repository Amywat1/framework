## Context

报警域已经有单一事实源（`alarm_registry`）、行为矩阵（`safety_matrix.h`）和 50ms 周期 drain。命令裁决同步读 registry，所以开洗拒绝是即时的；洗车中的停机却走 `EVT_SAFETY_LOCKOUT` → `safety_session_coordinator.abort_wash`。电机/流体不读 registry。急停热路径是采集线程上同步切断，与通用 CRITICAL 不是同一条时效。

本设计只收紧这条已经存在的事件链，不把能量切断并进通用 trigger。项目侧「通讯 CRITICAL 在 trigger 前 cutout」保持不变。

约束：domain 不得依赖 event_bus；drain 已由 ALRM-19 串行化；活跃池与 pending 容量保持 32，不靠加大队列掩盖问题。仓库处于开发期，不做向后兼容包装。

## Goals / Non-Goals

**Goals:**

- LOCKOUT 所需的 `EVT_SAFETY_LOCKOUT` 在 binding 的 trigger/clear 返回前完成入队，不再等待 50ms 周期。
- lockout 等级在活跃池满时仍能进入，代价是可见地驱逐一条非 lockout。
- 会话 journal 可被遥测快照读取，满池丢弃可观测；洗后是否 STOPPED 仍只看当前 blocking。

**Non-Goals:**

- 不在通用 trigger 路径调用 `safety_cutout_execute` 或 `safety_output_hold_request`。
- 不把去抖收回框架，不恢复 overflow 元告警，不做丢弃事件重放缓冲。
- 不放开 ON_MOTION binding 一对多分组，不改 `top_alarm` 的序号比较。
- 不把 `load_catalog` 做成运行期热加载（清空活动表且不发 CLEARED 仍视为启动期契约）。
- 不为非 LOCKOUT 报警做立即 drain；MINOR/MAJOR 的模式与投影延迟仍由 50ms + RESYNC 覆盖。

## Decisions

### D1. 立即 drain 放在 binding 包装，不放进 domain

`alarm_binding_bridge` 不再把函数指针直接接到 `alarm_registry_trigger/clear`，改为包装函数：registry 调用成功返回后，若 `alarm_registry_safety_posture() == SAFETY_POSTURE_LOCKOUT` 则调用 `alarm_bridge_drain()`。registry 在返回前已放锁，drain 的锁序仍是 bridge → registry → event_bus，与现网 ALRM-19 一致。

`event_publish` 只入队，handler 仍在 dispatch 线程执行，detector/采集线程不会跑 `abort_wash`。

**备选：**

- domain 回调发布事件：违反「registry 不依赖 event_bus」。
- 条件变量唤醒 50ms 任务：多一个等待点，最坏仍受调度延迟，对 lockout 没有同步入队的确定性。
- 每次 trigger/clear 都 drain：MINOR 抖动会让采集线程频繁取 event_bus 锁；LOCKOUT 才是运动路径需要的。

周期 50ms drain 保留：`reset_all`、`reevaluate_group`、`load_catalog` 不走 binding 包装。

判定用「调用后姿态是 LOCKOUT」而不是「本次是否新插入 CRITICAL」：第二条 CRITICAL 仍要立刻发出 `EVT_ALARM_TRIGGERED`；clear 使姿态回到 NOMINAL 时不立即 drain（多停最多 50ms 是故障安全侧）。

### D2. 池满时驱逐非 lockout，不预留空槽

插入前若池满且新条目 `alarm_level_forces_lockout`：

1. 在活动表中找 `!forces_lockout && !blocks_wash`（MINOR）中 `triggered_at_ms` 最早者；
2. 若无，找其余 `!forces_lockout`（MAJOR）中最早者；
3. 驱逐：`clear_active_at_locked`（发 CLEARED + 日志，标明因 lockout 腾槽）；
4. 再插入新条目。

32 条都是 lockout 时仍 `SW_ERR_OVERFLOW`。未知等级走矩阵最严格值，视为 lockout，享受同一准入。

**备选：** 预留 N 个 lockout 槽。稳态会浪费诊断容量，且 N 没有实测依据。驱逐把全部 32 槽留给当前最严重的集合。

不驱逐 lockout 去插入另一条 lockout：没有更「紧急」的等级，FIFO 拒绝比随意替换更可预测。

### D3. journal 并入快照，不新增聚合视图结构

扩展 `alarm_registry_get_session_journal`：增加 `dropped_out`（与 `pull_events` 相同，「取出即清零」只适用于丢弃计数的诊断增量——journal 丢弃计数改为累积直到会话结束或被快照读走）。

选定：丢弃计数**随会话**：会话开始清零；满时累加；`get_session_journal` 拷贝码表并**同时交出当前累计丢弃数但不清零**（码表是状态，丢弃数也是状态）。投影每次刷新都看到同一累计值，避免「两次读取把计数清掉、云端看到 0」。这与 pending 的「取出即清」不同：pending 有唯一消费者（drain），journal 有快照周期多读。

`safety_snapshot_t` 增加：

- `uint32_t session_journal[ALARM_SESSION_JOURNAL_MAX]`
- `unsigned session_journal_count`
- `uint32_t session_journal_dropped`

投影在现有 `copy_safety_view` 之后调用 journal 拷贝。两次取锁之间可能撕裂，1000ms 对账会收敛；journal 是取证字段，不参与开洗/LOCKOUT 裁决。不为它引入第三套 view 结构。

满池时 `append_session_journal_locked` 打一条 WARN（仅首次或按计数，避免刷屏）并累加 `s_session_journal_dropped`。

### D4. 文档与契约同步，不改安全端口

行为契约新增 ALRM 条目（立即 drain、lockout 驱逐、journal 可见丢弃）。模块设计与快照设计改为与实现一致。`safety_port` / `output_hold` / cutout 不改。

## Risks / Trade-offs

- [采集线程调用 drain 与急停争 event_bus 锁] → 与今日 50ms drain 相同竞争面；publish 非阻塞入队，持锁时间有界。不把 drain 登记为 RT：急停切断仍不取 drain 锁。
- [驱逐 MINOR 后 detector 下一拍再 trigger，pending 抖动] → 可接受；RESYNC 仍在。不把被驱逐码加入抑制名单。
- [立即 drain 只保证入队，abort_wash 仍等 dispatch] → 消除的是 50ms 周期排队，不是事件总线本身的调度；急停仍是唯一同步切断路径。文档必须写清这一层差异，避免把「入队前」写成「机构已停」。
- [journal 与活动表两次持锁] → 取证撕裂窗口；不用于裁决。若日后快照必须原子，再扩 `copy_safety_view`。

## Migration Plan

开发期直接替换：包装 binding、改 registry 插入与 journal、扩快照结构、补测试与文档。无运行期双路径。Demo 若在 trigger 后再手调 `alarm_bridge_drain`，与立即 drain 重叠，ALRM-19 保证边沿不双发。

回滚即还原本变更；无数据迁移。

## Open Questions

无。ON_MOTION 一对多与 `load_catalog` 热加载若要做，另开变更。
