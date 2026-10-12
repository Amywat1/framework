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
 * @brief  占用由 0 变为至少 1（STANDBY → ACTIVE）
 */
void op_mode_on_job_started(void);

/**
 * @brief  占用回到 0（ACTIVE → HOLD 或 STANDBY，由 profile 决定）
 */
void op_mode_on_last_job_finished(void);

/**
 * @brief  整机清障请求已成立（仅 ACTIVE 时进入 CLEARING）
 */
void op_mode_on_clear_requested(wash_abort_cause_t cause);

/**
 * @brief  场区已清空（HOLD → STANDBY）
 */
void op_mode_on_area_cleared(void);

/**
 * @brief  自检完成后的落点决策
 * @param  land_fault  true 表示自检判失败（仍落到 STOPPED，故障由旗标表达）；false 同样 → STOPPED
 */
void op_mode_on_self_check_completed(bool land_fault);

/**
 * @brief  阻塞告警触发运行模式收敛（离开接单/静态可运营态 → STOPPED）。
 * @note   CRITICAL 也走本入口（LOCKOUT ⊂ blocking）；`EVT_SAFETY_LOCKOUT` 只驱动 abort_jobs。
 */
void op_mode_on_blocking_alarm(void);

/**
 * @brief  急停状态变更
 * @param  active  true → 置急停并收敛 STOPPED；false → 清标志（不自动进 STANDBY）
 * @note   旗标实际变化时发布 CONTEXT_SYNC，保证已 STOPPED 时快照仍能跟上 estop_active。
 */
void op_mode_on_estop(bool active);

/**
 * @brief  恢复流程结束（RECOVERING → STANDBY 或 STOPPED）
 */
void op_mode_on_recovery_completed(recovery_result_t result);

/**
 * @brief  整机清障完成（CLEARING → STOPPED）
 */
void op_mode_on_clear_done(void);

/**
 * @brief  静态命令许可矩阵格点（不含急停、运营开关、blocking 等运行期附加条件）
 * @param  kind  命令种类
 * @param  mode  运行模式
 * @return 矩阵格点；非法 kind/mode 返回 DENIED
 */
op_cmd_perm_t op_mode_cmd_matrix_perm(dev_cmd_kind_t kind, operational_mode_t mode);

/**
 * @brief  当前整机是否允许放行下一单（STANDBY 或 ACTIVE）
 */
bool op_mode_can_admit(void);

/* -------------------------------------------------------------------------
 * 直读接口
 *
 * 单字段读已由内部互斥保护，可从 cmd_control / event_dispatch / 测试线程调用。
 * 需要「模式 + 安全 + 作业」同一时刻组合视图时，仍用 device_snapshot_get()。
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
 * @brief  设置运营总开关。
 * @param  enabled  true 营业；false 停业。停业时若当前为 STANDBY/HOLD 则落到 STOPPED。
 * @note   上电默认开启。项目可在 init 阶段用已持久化的停业状态覆盖。
 */
void op_mode_set_service_enabled(bool enabled);

/**
 * @brief  是否处于非运营接单态（STOPPED/恢复/清障等，或总开关已关）
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()
 *         配合 operational_snapshot_is_stopping()。
 */
bool op_mode_is_stopping(void);

/**
 * @brief  是否运营待机（STANDBY；可停车检测与接单）
 * @note   仅限 event_dispatch 线程调用；跨线程请用 device_snapshot_get()
 *         配合 operational_snapshot_is_standby()。
 */
bool op_mode_is_standby(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_OPERATIONAL_MODE_H */
