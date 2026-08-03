/**
 * @file    operational_mode.h
 * @brief   OperationalMode 聚合根接口
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    只在 event_dispatch 线程访问；不订阅 event_bus，模式变更时主动 event_publish。
 */

#ifndef DOMAIN_OP_MODE_OPERATIONAL_MODE_H
#define DOMAIN_OP_MODE_OPERATIONAL_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "domain/op_mode/command_types.h"
#include "domain/op_mode/device_command.h"
#include "domain/op_mode/op_mode_types.h"

#include <stdbool.h>

/**
 * @brief  初始化运行模式聚合（上电默认进入 STOPPED）
 */
sw_err_t operational_mode_init(void);

/**
 * @brief  处理外部命令（命令矩阵 + 模式转移 + 副作用枚举）
 * @param  cmd  入站命令
 * @return 裁决结果（含 pending_effect）
 */
dev_cmd_decision_t op_mode_handle_command(const dev_cmd_t *cmd);

/**
 * @brief  洗车会话已进入 RUNNING
 */
void op_mode_on_wash_session_started(void);

/**
 * @brief  洗车会话正常完成（WASHING → WASH_DONE；若仍有 MAJOR+ 则 → EXCEPTION）
 */
void op_mode_on_wash_session_completed(void);

/**
 * @brief  洗车会话中止
 * @param  cause  中止原因（ESTOP 时模式已由 on_estop 切换，此处无操作）
 */
void op_mode_on_wash_session_aborted(wash_abort_cause_t cause);

/**
 * @brief  洗车区域清空，客户离场（WASH_DONE → IDLE）
 */
void op_mode_on_wash_customer_gone(void);

/**
 * @brief  自检完成后的落点决策
 * @param  land_exception  true → EXCEPTION；false → 回到进入自检前的状态（STOPPED）
 */
void op_mode_on_self_check_completed(bool land_exception);

/**
 * @brief  阻塞告警触发运行模式收敛。
 */
void op_mode_on_blocking_alarm(void);

/**
 * @brief  CRITICAL 告警触发运行模式收敛。
 * @note   保留为安全事件桥接入口，行为与阻塞告警一致。
 */
void op_mode_on_critical_alarm(void);

/**
 * @brief  急停状态变更
 * @param  active  true → 急停触发（→ EXCEPTION）；false → 急停清除（清标志，发布 CONTEXT_SYNC）
 */
void op_mode_on_estop(bool active);

/**
 * @brief  恢复流程结束（RECOVERING → IDLE 或 EXCEPTION）
 */
void op_mode_on_recovery_completed(recovery_result_t result);

/**
 * @brief  归位完成（HOMING → IDLE/EXCEPTION；ABORT_HOMING → EXCEPTION）
 * @param  success  归位是否成功（ABORT_HOMING 路径忽略此参数，始终进入 EXCEPTION）
 */
void op_mode_on_home_done(bool success);

/* -------------------------------------------------------------------------
 * 直读接口（线程约束）
 *
 * 以下读接口直接返回聚合根的内部状态，不加锁：本模块的状态只在 event_dispatch
 * 线程内被修改（命令裁决与各事件桥接都在该线程执行），因此同线程内读取总是
 * 一致的，无需同步开销。
 *
 * 其他线程（周期任务、项目工作线程、CLI）不得调用这些接口——那会构成无保护
 * 的跨线程读，可能读到撕裂或过期的组合状态。跨线程读一律改用
 * `domain/telemetry/device_snapshot.h` 的 device_snapshot_get()：它是加锁的
 * 原子快照，且能保证运行模式、安全状态、洗车模式来自同一时刻。
 * ------------------------------------------------------------------------- */

/**
 * @brief  读取当前运行模式
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()。
 */
operational_mode_t op_mode_get_current(void);

/**
 * @brief  急停是否激活
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()。
 */
bool op_mode_is_estop_active(void);

/**
 * @brief  运营总开关是否开启（关则禁止 RECOVER；上电默认开启）
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()。
 */
bool op_mode_is_service_enabled(void);

/**
 * @brief  是否处于非运营接单态（STOPPED/HOMING/故障处理等，或总开关已关）
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()
 *         配合 operational_snapshot_is_stopping()。
 */
bool op_mode_is_stopping(void);

/**
 * @brief  是否运营待机（IDLE；可停车检测与接单）
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()
 *         配合 operational_snapshot_is_standby()。
 */
bool op_mode_is_standby(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_OPERATIONAL_MODE_H */
