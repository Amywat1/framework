/**
 * @file    safety_fsm.h
 * @brief   安全状态机：OK / WARNING / LOCKOUT
 * @author  HUWANGWEI
 * @date    2026-06-26
 *
 * @note    safety_fsm 订阅 EVT_ALARM_TRIGGERED / EVT_ALARM_CLEARED，
 *          在报警活跃集变化时向 alarm_core 查询聚合安全态，并在安全态发生
 *          跃迁时发布 EVT_SAFETY_LOCKOUT / EVT_SAFETY_WARNING / EVT_SAFETY_CLEARED。
 *          它把「整机安全姿态」从报警目录中解耦出来，供 emergency_handler、
 *          device_fsm 等执行机构/FSM 消费，使其无需感知具体报警码与等级。
 *          所有处理在 event_dispatch 线程上下文执行，内部状态无需额外加锁。
 */

#ifndef DOMAIN_SAFETY_SAFETY_FSM_H
#define DOMAIN_SAFETY_SAFETY_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  初始化安全状态机并订阅报警生命周期事件
 * @retval SW_OK / SW_ERR_NOT_INIT / SW_ERR_OVERFLOW
 */
sw_err_t safety_fsm_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SAFETY_SAFETY_FSM_H */
