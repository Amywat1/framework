## 1. registry 直接发布

- [x] 1.1 删除 pending 队列、`enqueue_event_locked`、`alarm_registry_pull_events`、`ALARM_PENDING_EVENT_MAX` 与 `alarm_domain_event_t` 待发用途；头文件去掉 `pull_events`
- [x] 1.2 变位路径（trigger/clear/reset_all/reevaluate_group，含 lockout 驱逐）持锁只改活动表，把本轮码与姿态边沿记到栈上数组，放锁后 `event_publish_required`；先逐码后 `EVT_SAFETY_*`；持锁 MUST NOT 调总线
- [x] 1.3 `load_catalog` / `init` 仍清空运行态且不补发 CLEARED（启动期契约不变）
- [x] 1.4 改 `test_alarm_registry`：断言改为总线入队（或 `event_bus_drain` 后订阅计数）；删除 pending 溢出 / dropped 用例；覆盖 MINOR 返回前已入队、最后一条 CRITICAL 返回前已有 NOMINAL、reset_all 批量 CLEARED、重复 trigger 不重复 TRIGGERED

## 2. 安全视图

- [x] 2.1 增加 `alarm_safety_view_t`，一次持锁填充活动表、blocking、top_code、posture、journal 与 `journal_dropped`；`has_blocking_active` / `safety_posture` 改为薄封装
- [x] 2.2 `telemetry_projection` 刷新安全快照只调一次视图拷贝；取消 `EVT_ALARM_RESYNC` 订阅
- [x] 2.3 更新 `test_alarm_registry` 视图用例与 `test_telemetry_projection`：journal 与活动表同锁；连续读取丢弃数不清零

## 3. 拆 drain、合桥

- [x] 3.1 删除 `alarm_bridge_drain`、50ms 周期任务、drain 互斥、`ALARM_BRIDGE_UNIT_TEST` 临界区钩子；`alarm_bridge_init` 只订会话生命周期（及既有 reeval 入口）
- [x] 3.2 将 `alarm_binding_bridge_bind` 并入 `alarm_bridge`（ops 直转 `alarm_registry_*`）；删除 `alarm_binding_bridge.{c,h}`；`cmake/wdf_targets.cmake`、bootstrap、demo、端口注释同步；`PORT_REQ_ALARM_BINDING` 仍在
- [x] 3.3 删除 `EVT_ALARM_RESYNC` 及 observation / op_mode / 文档引用；`demo_main` 去掉手调 drain
- [x] 3.4 更新 R16：`alarm_bridge.c` 不再持 drain 锁则移出或改注释；`alarm_registry.c` 仍登记为采集线程可达

## 4. 运行模式单一收敛

- [x] 4.1 合并 `op_mode_on_critical_alarm` 与 `op_mode_on_blocking_alarm`；`op_mode_bridge` 订 `EVT_ALARM_TRIGGERED` 做 blocking 收敛，不再为 CRITICAL 二次切模式；保留 `EVT_SAFETY_LOCKOUT` → coordinator `abort_wash`
- [x] 4.2 更新 `test_op_mode_bridge`：CRITICAL 从 IDLE 进入 STOPPED 只收敛一次

## 5. 测试改写

- [x] 5.1 改写 `test_alarm_event_bridge`：无 drain/RESYNC/并发 drain 钩子；binding trigger CRITICAL/MINOR 返回前已入队；第二条 CRITICAL 不重复 LOCKOUT
- [x] 5.2 改写 `test_alarm_critical_path_adversarial` 与 `test_safety_posture`：驱逐可见 CLEARED 走总线；去掉 pull_events / RESYNC / drain
- [x] 5.3 `test_alarm_registry_concurrent` 改为并发 trigger/clear 下投影自洽 + 入队不丢账（不再 pull pending）
- [x] 5.4 `test_alarm_binding_bridge` 改为验证合入后的 bind（或并入 event bridge 测试后删除独立目标）；`tests/CMakeLists.txt` 与 bootstrap 桩符号同步
- [x] 5.5 保留 `test_alarm_lifecycle_bridge` / `test_alarm_reeval_bridge` 语义，去掉对 drain 的依赖

## 6. 契约与文档

- [x] 6.1 重写 `doc/module-design/domain/报警系统模块设计.md`：单一事实源 + 放锁后发布；删除三座桥、pending、立即/周期 drain、RESYNC；注明入队≠机构已停
- [x] 6.2 更新架构 01/05、模块总览、EventBus、快照、Demo 接入、命令网关中 RESYNC/binding_bridge/drain 表述
- [x] 6.3 修订 `doc/contract/行为契约.md`：删除或改写 ALRM-13/14/18/19/22；ALRM-17/20/21 对齐视图与总线 CLEARED；补充「变位返回前入队、持锁不发布」
- [x] 6.4 `alarm_types.h` 容量注释去掉 pending；整治记录追加本节决策（不改 3.3 容量数字）
- [x] 6.5 运行 `python3 scripts/gen_doc_index.py` 后执行 `./scripts/check_all.sh`（无 cmake 则 `--skip-tests` 并记录）
