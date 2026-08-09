/**
 * @file    operational_mode.h
 * @brief   OperationalMode 聚合根接口
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    不订阅 event_bus，模式变更时主动 event_publish。
 *          写路径来自 cmd_control（命令裁决）与 event_dispatch（生命周期钩子），
 *          内部以互斥量串行化；跨线程组合读仍建议用 device_snapshot_get()。
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
 * @brief 命令许可矩阵格点（不含运行期条件）
 */
typedef enum {
    OP_CMD_PERM_DENIED = 0, /**< 拒绝 */
    OP_CMD_PERM_ALLOWED,    /**< 允许 */
    OP_CMD_PERM_CONDITIONAL /**< 允许，但需通过急停运行期检查 */
} op_cmd_perm_t;

/**
 * @brief  初始化运行模式聚合（上电默认进入 STOPPED）
 */
sw_err_t operational_mode_init(void);

/**
 * @brief  处理外部命令（命令矩阵 + 模式转移）
 * @param  cmd  入站命令
 * @return 裁决结果（许可与拒绝原因；同步 HAL 由 application 按 kind 执行）
 */
dev_cmd_decision_t op_mode_handle_command(const dev_cmd_t *cmd);

/**
 * @brief  洗车会话已进入 RUNNING
 */
void op_mode_on_wash_session_started(void);

/**
 * @brief  洗车会话正常完成（WASHING → WASH_DONE；若仍有 MAJOR+ 则 → STOPPED）
 */
void op_mode_on_wash_session_completed(void);

/**
 * @brief  洗车会话中止
 * @param  cause  中止原因（急停 / STOP_ALL 时模式已先切至 STOPPED，此处无操作）
 */
void op_mode_on_wash_session_aborted(wash_abort_cause_t cause);

/**
 * @brief  洗车区域清空，客户离场（WASH_DONE → IDLE）
 */
void op_mode_on_wash_customer_gone(void);

/**
 * @brief  自检完成后的落点决策
 * @param  land_fault  true 表示自检判失败（仍落到 STOPPED，故障由旗标表达）；false 同样 → STOPPED
 */
void op_mode_on_self_check_completed(bool land_fault);

/**
 * @brief  阻塞告警触发运行模式收敛（离开接单/静态可运营态 → STOPPED）。
 */
void op_mode_on_blocking_alarm(void);

/**
 * @brief  CRITICAL 告警触发运行模式收敛。
 * @note   保留为安全事件桥接入口，行为与阻塞告警一致（洗中等流程态不立刻切）。
 */
void op_mode_on_critical_alarm(void);

/**
 * @brief  急停状态变更
 * @param  active  true → 置急停并收敛 STOPPED；false → 清标志（不自动进 IDLE）
 * @note   旗标实际变化时发布 CONTEXT_SYNC，保证已 STOPPED 时快照仍能跟上 estop_active。
 */
void op_mode_on_estop(bool active);

/**
 * @brief  恢复流程结束（RECOVERING → IDLE 或 STOPPED）
 */
void op_mode_on_recovery_completed(recovery_result_t result);

/**
 * @brief  中止归位完成（ABORT_HOMING → STOPPED）
 * @note   仅消费 EVT_ABORT_HOME_DONE；运营归位由 recovery_service 经
 *         EVT_OP_MODE_RECOVERY_COMPLETED 收口，不经本接口。
 */
void op_mode_on_home_done(void);

/**
 * @brief  静态命令许可矩阵格点（不含急停、运营开关、blocking 等运行期附加条件）
 * @param  kind  命令种类
 * @param  mode  运行模式
 * @return 矩阵格点；非法 kind/mode 返回 DENIED
 */
op_cmd_perm_t op_mode_cmd_matrix_perm(dev_cmd_kind_t kind, operational_mode_t mode);

/* -------------------------------------------------------------------------
 * 直读接口
 *
 * 单字段读已由内部互斥保护，可从 cmd_control / event_dispatch / 测试线程调用。
 * 需要「模式 + 安全 + 洗车」同一时刻组合视图时，仍用 device_snapshot_get()。
 * ------------------------------------------------------------------------- */

/**
 * @brief  读取当前运行模式
 */
operational_mode_t op_mode_get_current(void);

/**
 * @brief  急停是否激活
 */
bool op_mode_is_estop_active(void);

/**
 * @brief  运营总开关是否开启（关则禁止 RECOVER；上电默认开启）
 */
bool op_mode_is_service_enabled(void);

/**
 * @brief  是否处于非运营接单态（STOPPED/恢复/中止清障等，或总开关已关）
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
