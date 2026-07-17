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
 * @brief  初始化运行模式聚合
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
 * @brief  洗车会话正常完成
 */
void op_mode_on_wash_session_completed(void);

/**
 * @brief  洗车会话中止
 * @param  cause  中止原因
 */
void op_mode_on_wash_session_aborted(wash_abort_cause_t cause);

/**
 * @brief  服务完成评估结束
 * @param  enter_exception  true 时 IDLE → EXCEPTION
 */
void op_mode_on_post_wash_assessment(bool enter_exception);

/**
 * @brief  自检完成后的落点决策
 * @param  land_exception  true → EXCEPTION，false → IDLE
 */
void op_mode_on_self_check_completed(bool land_exception);

/**
 * @brief  非急停 CRITICAL 报警（LOCKOUT 路径）
 */
void op_mode_on_critical_alarm(void);

/**
 * @brief  急停触发
 */
void op_mode_on_estop_triggered(void);

/**
 * @brief  急停清除
 */
void op_mode_on_estop_cleared(void);

/**
 * @brief  恢复流程结束
 */
void op_mode_on_recovery_completed(recovery_result_t result);

/**
 * @brief  读取当前运行模式
 */
operational_mode_t op_mode_get_current(void);

/**
 * @brief  急停是否激活
 */
bool op_mode_is_estop_active(void);

/**
 * @brief  运营是否接单
 */
bool op_mode_is_service_enabled(void);

/**
 * @brief  是否处于停机态（!service_enabled 或 INIT/EXCEPTION/RECOVERING）
 */
bool op_mode_is_stopping(void);

/**
 * @brief  是否待机（IDLE 且 service_enabled）
 */
bool op_mode_is_standby(void);

/**
 * @brief  设置运营接单开关（Stop/Resume Operation）
 */
void op_mode_set_service_enabled(bool enabled);

/**
 * @brief  人工复位报警后尝试回到 IDLE（DEV_CMD_RESET_FAULT 副作用）
 */
void op_mode_on_legacy_reset_fault(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_OP_MODE_OPERATIONAL_MODE_H */
