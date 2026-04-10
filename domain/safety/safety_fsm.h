/**
 * @file    safety_fsm.h
 * @brief   安全状态机接口（OK / WARNING / LOCKOUT）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    safety_fsm 订阅 EVT_ALARM_TRIGGERED / EVT_ALARM_CLEARED，
 *          根据报警等级驱动安全状态机，并发布安全事件。
 *          safety_fsm 只发事件，不直接写 dev_ctx（由 safety_supervisor 完成）。
 *
 *          状态转移：
 *            OK ──ERROR 报警──→ LOCKOUT
 *            OK ──WARNING 报警──→ WARNING
 *            WARNING ──ERROR 报警──→ LOCKOUT
 *            LOCKOUT ──手动复位后无激活报警──→ OK（由 manual_reset 调用 safety_fsm_reevaluate）
 *            WARNING ──报警清除后无 WARNING──→ OK
 */

#ifndef DOMAIN_SAFETY_FSM_H
#define DOMAIN_SAFETY_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/model/safety_types.h"
#include "common/sw_error.h"

/* -------------------------------------------------------------------------
 * 接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化安全状态机，订阅报警事件
 * @retval SW_OK / SW_ERR_HW（event_subscribe 失败）
 */
sw_err_t safety_fsm_init(void);

/**
 * @brief  获取当前安全状态
 */
safety_state_t safety_fsm_get_state(void);

/**
 * @brief  重新评估安全状态（手动复位后主动触发，检查是否可以从 LOCKOUT 恢复到 OK）
 */
void safety_fsm_reevaluate(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_FSM_H */
