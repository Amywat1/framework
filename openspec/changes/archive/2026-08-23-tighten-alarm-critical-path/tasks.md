## 1. 活跃池 lockout 准入

- [x] 1.1 在 `alarm_registry_trigger` 池满分支按 D2 驱逐非 lockout（先 MINOR 最早，再 MAJOR 最早），驱逐走 `clear_active_at_locked` 并打 ERROR 日志
- [x] 1.2 全员 lockout 或新码非 lockout 时仍返回 `SW_ERR_OVERFLOW`，已活跃码只刷新 `condition_active`
- [x] 1.3 在 `test_alarm_registry` 增加满池 MINOR→CRITICAL 驱逐、满员 MAJOR 驱逐、满员 CRITICAL 拒绝、满池新 MINOR 拒绝、已活跃刷新不驱逐

## 2. 会话 journal 可观测

- [x] 2.1 `append_session_journal_locked` 满且新码时累加 `s_session_journal_dropped`，会话开始与 `reset_runtime_state_locked` 清零；打有界 WARN
- [x] 2.2 扩展 `alarm_registry_get_session_journal` 增加 `dropped_out`（可为 NULL），读取 MUST NOT 清零丢弃计数；更新头文件中文 Doxygen
- [x] 2.3 在 `test_alarm_registry` 与 `test_alarm_lifecycle_bridge` 覆盖去重、满池丢弃、MINOR 不影响、新会话清零、连续两次读取计数不变

## 3. LOCKOUT 立即 drain

- [x] 3.1 `alarm_binding_bridge` 包装 `trigger`/`clear`：registry 成功且姿态为 LOCKOUT 时，返回前调用 `alarm_bridge_drain`
- [x] 3.2 确认包装不在持 registry 锁时调用 drain；周期 50ms drain 仍注册
- [x] 3.3 在 `test_alarm_event_bridge`（或 binding 测试）覆盖：停周期任务后经 binding trigger CRITICAL，返回前已有 `EVT_SAFETY_LOCKOUT`；MINOR 不强制立即入队；第二条 CRITICAL 不重复 LOCKOUT；并发 drain 仍满足 ALRM-19

## 4. 快照并入 journal

- [x] 4.1 `safety_snapshot_t` 增加 `session_journal`、`session_journal_count`、`session_journal_dropped`，投影刷新时从 registry 拷贝
- [x] 4.2 在 `test_telemetry_projection` 断言会话中 MAJOR 出现在快照、丢弃计数可见；确认洗后 STOPPED 仍只断言 `has_blocking_active`

## 5. 契约与文档

- [x] 5.1 更新 `doc/module-design/domain/报警系统模块设计.md`：立即 drain、驱逐策略、journal 丢弃与快照字段；保留「cutout 不进通用 trigger」
- [x] 5.2 更新 `doc/module-design/domain/状态投影与设备快照模块设计.md` 安全子域
- [x] 5.3 在 `doc/contract/行为契约.md` 增加对应 ALRM 条目（立即 drain、lockout 驱逐、journal 可见丢弃）
- [x] 5.4 若容量语义变化，同步 `alarm_types.h` 注释与 `doc/history/整治记录.md` 相关表
- [x] 5.5 运行 `python3 scripts/gen_doc_index.py` 后执行 `./scripts/check_all.sh`（无 cmake 则 `--skip-tests` 并记录）
